#include "CalibrationSolver.hpp"

#include <cstdio>

#include "Analysis.hpp"
#include "CalibrationMaps.hpp"
#include "TransformUtils.hpp"

namespace EasyOffset::CalibrationSolver {

using Analysis::Weighted;

static constexpr float MinimalAngularVelocity = 120.0f;  // deg/s, ignore slow movement
static constexpr float MinimalPlaneAngle = 15.0f;        // deg between the two planes
static constexpr float MaximalDirectionChange = 45.0f;   // deg from the controller default
static constexpr float MinimalQuality = 0.55f;
static constexpr float MaximalPivotChange = 0.10f;  // m from the controller default
static constexpr float MaximalPivotDistance = 0.25f;

static int Index(Hand hand) { return hand == Hand::Left ? 0 : 1; }

// Controller-local rotation axes of the fast frames inside the hand's window
static void CollectAxes(Recording const& recording, int step, Hand hand, std::vector<Weighted>& axes, Vec3& worldLeftSum, std::vector<std::pair<Quat, Vec3>>* worldAxes = nullptr) {
    float from, to;
    if (!CalibrationMaps::GetWindow(step, hand, from, to)) return;
    int h = Index(hand);
    for (size_t i = 1; i < recording.size(); i++) {
        auto const& a = recording[i - 1];
        auto const& b = recording[i];
        if (b.Time < from || b.Time > to) continue;
        if (!a.Valid[h] || !b.Valid[h] || b.DeltaTime <= 0) continue;
        float angle;
        Vec3 axis;
        ToAngleAxis(b.Rotation[h] * Inverse(a.Rotation[h]), angle, axis);
        if (angle > 180) {
            angle = 360 - angle;
            axis = -axis;
        }
        float velocity = angle / b.DeltaTime;
        if (velocity < MinimalAngularVelocity || velocity > 5000) continue;
        Vec3 local = Inverse(b.Rotation[h]) * axis;
        axes.push_back({local, velocity * velocity});
        worldLeftSum += b.Head * Vec3::Left();
        if (worldAxes) worldAxes->push_back({b.Rotation[h], local});
    }
}

static void CollectPivotPairs(Recording const& recording, int step, Hand hand, Analysis::RotationPointTracker& tracker) {
    float from, to;
    if (!CalibrationMaps::GetWindow(step, hand, from, to)) return;
    int h = Index(hand);
    for (size_t i = 1; i < recording.size(); i++) {
        auto const& a = recording[i - 1];
        auto const& b = recording[i];
        if (b.Time < from || b.Time > to) continue;
        if (!a.Valid[h] || !b.Valid[h] || b.DeltaTime <= 0) continue;
        if (Angle(a.Rotation[h], b.Rotation[h]) / b.DeltaTime < 200.0f) continue;
        tracker.AddPair(a.Position[h], a.Rotation[h], b.Position[h], b.Rotation[h], b.DeltaTime);
    }
}

static void Append(std::string& notes, char const* text) {
    if (!notes.empty()) notes += "\n";
    notes += text;
}

HandResult SolveHand(Hand hand, std::array<Recording, 3> const& recordings, Config::HandOffsets const& prior, Config::HandOffsets const& current) {
    HandResult result;
    result.Offsets = current;
    Vec3 priorForward = TransformUtils::DirectionFromRotation(prior.Rotation);

    // ---- swing planes ----
    std::vector<Weighted> verticalAxes, diagonalAxes;
    std::vector<std::pair<Quat, Vec3>> verticalWorld;
    Vec3 headLeft;
    CollectAxes(recordings[0], 0, hand, verticalAxes, headLeft, &verticalWorld);
    CollectAxes(recordings[1], 1, hand, verticalAxes, headLeft, &verticalWorld);
    Vec3 unused;
    CollectAxes(recordings[2], 2, hand, diagonalAxes, unused);
    result.SwingSamples = (int) (verticalAxes.size() + diagonalAxes.size());

    Vec3 verticalNormal, diagonalNormal;
    if (verticalAxes.size() >= 6) verticalNormal = Analysis::PrincipalAxis(verticalAxes, &result.VerticalQuality);
    if (diagonalAxes.size() >= 3) diagonalNormal = Analysis::PrincipalAxis(diagonalAxes, &result.DiagonalQuality);

    bool verticalOk = verticalNormal.SqrMagnitude() > 0.5f && result.VerticalQuality >= MinimalQuality;
    bool diagonalOk = diagonalNormal.SqrMagnitude() > 0.5f && result.DiagonalQuality >= MinimalQuality;

    Vec3 direction;
    if (verticalOk && diagonalOk) {
        Vec3 cross = Cross(verticalNormal, diagonalNormal);
        result.PlaneAngleDeg = std::asin(std::clamp(cross.Magnitude(), 0.0f, 1.0f)) * Rad2Deg;
        if (result.PlaneAngleDeg >= MinimalPlaneAngle) {
            direction = cross.Normalized();
            result.UsedTwoPlanes = true;
        } else {
            Append(result.Notes, "Diagonal swing too close to vertical: used one plane.");
        }
    } else if (verticalOk) {
        Append(result.Notes, "Diagonal swing not detected: used the vertical plane only.");
    }

    if (!result.UsedTwoPlanes && verticalOk) {
        // keep the default direction, only remove its curve relative to the vertical plane
        direction = (priorForward - verticalNormal * Dot(priorForward, verticalNormal)).Normalized();
    }

    if (direction.SqrMagnitude() > 0.5f) {
        if (Dot(direction, priorForward) < 0) direction = -direction;
        float change = AngleBetween(direction, priorForward);
        if (change <= MaximalDirectionChange) {
            Quat rotation = FromToRotation(priorForward, direction) * prior.Rotation;
            result.Offsets.Rotation = Normalize(rotation);
            result.RotationMeasured = true;
        } else {
            char buffer[128];
            std::snprintf(buffer, sizeof buffer, "Measured direction is %.0f deg from the default: rotation kept.", change);
            Append(result.Notes, buffer);
        }
    } else {
        Append(result.Notes, "Not enough clean swings: rotation kept.");
    }

    // ---- reference (vertical swing plane), oriented to the player's left like PC ----
    if (result.RotationMeasured && verticalOk) {
        Vec3 normal = verticalNormal;
        float side = 0;
        for (auto const& [rotation, local] : verticalWorld) side += Dot(rotation * normal, headLeft);
        if (side < 0) normal = -normal;
        Vec3 forward = TransformUtils::DirectionFromRotation(result.Offsets.Rotation);
        result.Offsets.HasReference = true;
        result.Offsets.ReferenceRotation = LookRotation(forward, normal) * Euler(0, 0, 90);
    }

    // ---- pivot (wrist) from the inline flicks ----
    Analysis::RotationPointTracker tracker(150);
    CollectPivotPairs(recordings[1], 1, hand, tracker);
    if (tracker.Count() >= 12) {
        Vec3 pivot = tracker.GetLocalOrigin(prior.PivotPosition, 0.15f);
        if (pivot.IsFinite() && Distance(pivot, prior.PivotPosition) <= MaximalPivotChange && pivot.Magnitude() <= MaximalPivotDistance) {
            result.Offsets.PivotPosition = pivot;
            result.PivotMeasured = true;
        } else {
            Append(result.Notes, "Pivot measurement out of range (arm swings instead of wrist?): pivot set to the controller default.");
            result.Offsets.PivotPosition = prior.PivotPosition;
        }
    } else {
        Append(result.Notes, "Not enough fast inline swings for the pivot: pivot set to the controller default.");
        result.Offsets.PivotPosition = prior.PivotPosition;
    }

    result.RotationChangeDeg = AngleBetween(TransformUtils::DirectionFromRotation(result.Offsets.Rotation), TransformUtils::DirectionFromRotation(current.Rotation));
    result.PivotChangeCm = Distance(result.Offsets.PivotPosition, current.PivotPosition) * 100.0f;
    return result;
}
}
