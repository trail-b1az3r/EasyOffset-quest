#include "AdjustmentModes.hpp"

#include <memory>
#include <vector>

#include "Analysis.hpp"
#include "Config.hpp"
#include "Gizmos.hpp"
#include "Tracking.hpp"
#include "TransformUtils.hpp"
#include "main.hpp"

namespace EasyOffset::AdjustmentModes {

#pragma region Base

// PC: AbstractAdjustmentModeManager
class ModeManager {
public:
    ModeManager(AdjustmentMode mode, float positionalSmoothing, float rotationalSmoothing)
        : mode(mode), positionalSmoothing(positionalSmoothing), rotationalSmoothing(rotationalSmoothing) {}
    virtual ~ModeManager() = default;

    void Subscribe() {
        Tracking::AssignedButtonPressedEvent.Subscribe([this](Hand h) { OnPressed(h); });
        Tracking::AssignedButtonReleasedEvent.Subscribe([this](Hand h) { OnReleased(h); });
        Tracking::TransformsUpdatedEvent.Subscribe([this]() { OnUpdated(); });
        Config::AdjustmentModeChangedEvent.Subscribe([this](AdjustmentMode) { ForceRelease(); });
        Config::AssignedButtonChangedEvent.Subscribe([this](ControllerButton) { ForceRelease(); });
    }

protected:
    virtual void OnGrabStarted(Hand hand, ReeTransform const& adjustment, ReeTransform const& free) = 0;
    virtual void OnGrabUpdated(Hand hand, ReeTransform const& adjustment, ReeTransform const& free) = 0;
    virtual void OnGrabFinished(Hand hand, ReeTransform const& adjustment, ReeTransform const& free) = 0;

    AdjustmentMode mode;

private:
    float positionalSmoothing, rotationalSmoothing;
    bool pressed = false;
    Hand adjustmentHand = Hand::Left;

    static Hand GetAdjustmentHand(Hand pressedHand) { return Config::GetUseFreeHand() ? OtherHand(pressedHand) : pressedHand; }

    void OnPressed(Hand hand) {
        if (pressed || Config::GetAdjustmentMode() != mode) return;
        Config::SmoothingEnabled = true;
        Config::PositionalSmoothing = positionalSmoothing;
        Config::RotationalSmoothing = rotationalSmoothing;
        adjustmentHand = GetAdjustmentHand(hand);
        pressed = true;
        OnGrabStarted(adjustmentHand, Tracking::Get(adjustmentHand), Tracking::Get(OtherHand(adjustmentHand)));
    }

    void OnUpdated() {
        if (!pressed) return;
        OnGrabUpdated(adjustmentHand, Tracking::Get(adjustmentHand), Tracking::Get(OtherHand(adjustmentHand)));
    }

    void OnReleased(Hand hand) {
        if (!pressed || adjustmentHand != GetAdjustmentHand(hand)) return;
        Config::SmoothingEnabled = false;
        Config::PositionalSmoothing = 0;
        Config::RotationalSmoothing = 0;
        pressed = false;
        OnGrabFinished(adjustmentHand, Tracking::Get(adjustmentHand), Tracking::Get(OtherHand(adjustmentHand)));
        Config::Save();
    }

    void ForceRelease() {
        OnReleased(Hand::Left);
        OnReleased(Hand::Right);
    }
};

static std::string Describe(std::string_view action, Hand hand, std::string_view what) {
    return std::string(action) + " " + std::string(HandName(hand)) + " " + std::string(what);
}

#pragma endregion

#pragma region Basic

class BasicMode : public ModeManager {
public:
    BasicMode() : ModeManager(AdjustmentMode::Basic, 4, 4) {}

protected:
    Quat grabWorldRotation;
    Vec3 grabPosition;

    void OnGrabStarted(Hand hand, ReeTransform const& a, ReeTransform const&) override {
        auto const& h = Config::Get(hand);
        Gizmos::SetOrthonormalFocus(hand, true);
        grabWorldRotation = a.LocalToWorldRotation(h.Rotation);
        grabPosition = a.LocalToWorldPosition(h.PivotPosition);
        Config::CreateUndoStep(Describe("Change", hand, "Offset"));
    }
    void OnGrabUpdated(Hand hand, ReeTransform const& a, ReeTransform const&) override {
        Config::SetRotation(hand, a.WorldToLocalRotation(grabWorldRotation));
        Config::SetPivotPosition(hand, a.WorldToLocalPosition(grabPosition));
    }
    void OnGrabFinished(Hand hand, ReeTransform const&, ReeTransform const&) override { Gizmos::SetOrthonormalFocus(hand, false); }
};

#pragma endregion

#pragma region Position

class PositionMode : public ModeManager {
public:
    PositionMode() : ModeManager(AdjustmentMode::Position, 3, 6) {}

protected:
    Vec3 grabWorldPosition;

    void OnGrabStarted(Hand hand, ReeTransform const& a, ReeTransform const&) override {
        Gizmos::SetOrthonormalFocus(hand, true);
        grabWorldPosition = a.LocalToWorldPosition(Config::Get(hand).PivotPosition);
        Config::CreateUndoStep(Describe("Change", hand, "Position"));
    }
    void OnGrabUpdated(Hand hand, ReeTransform const& a, ReeTransform const&) override {
        Config::SetPivotPosition(hand, a.WorldToLocalPosition(grabWorldPosition));
    }
    void OnGrabFinished(Hand hand, ReeTransform const&, ReeTransform const&) override { Gizmos::SetOrthonormalFocus(hand, false); }
};

#pragma endregion

#pragma region Rotation

class RotationMode : public ModeManager {
public:
    static constexpr float ZoomedOutSmoothing = 3;
    static constexpr float ZoomedInSmoothing = 1;
    RotationMode() : ModeManager(AdjustmentMode::Rotation, 6, ZoomedOutSmoothing) {}

protected:
    Range zoomRange{1, 4};
    Range heightRange{0.05f, 0.4f};
    Range smoothingRange{ZoomedOutSmoothing, ZoomedInSmoothing};
    Quat grabWorldRotation;
    float grabFreeY = 0;

    void OnGrabStarted(Hand hand, ReeTransform const& a, ReeTransform const& f) override {
        Gizmos::SetSphericalFocus(hand, true);
        grabWorldRotation = a.LocalToWorldRotation(Config::Get(hand).Rotation);
        grabFreeY = f.Position.y;
        Config::CreateUndoStep(Describe("Change", hand, "Rotation"));
    }
    void OnGrabUpdated(Hand hand, ReeTransform const& a, ReeTransform const& f) override {
        float ratio = heightRange.GetRatioClamped(f.Position.y - grabFreeY);
        Config::RotationalSmoothing = smoothingRange.SlideBy(ratio);
        Config::SetRotation(hand, a.WorldToLocalRotation(grabWorldRotation));
        Gizmos::Zoom(hand, zoomRange.SlideBy(ratio));
    }
    void OnGrabFinished(Hand hand, ReeTransform const&, ReeTransform const&) override {
        Gizmos::SetSphericalFocus(hand, false);
        Gizmos::Zoom(hand, 1);
    }
};

#pragma endregion

#pragma region PositionAuto

class PositionAutoMode : public ModeManager {
public:
    PositionAutoMode() : ModeManager(AdjustmentMode::PositionAuto, 0, 0) {}

protected:
    Analysis::RotationPointTracker tracker{120};

    void OnGrabStarted(Hand hand, ReeTransform const&, ReeTransform const&) override {
        Gizmos::SetOrthonormalFocus(hand, true);
        tracker.Clear();
        Config::CreateUndoStep(Describe("Auto", hand, "Position"));
    }
    void OnGrabUpdated(Hand hand, ReeTransform const& a, ReeTransform const&) override {
        tracker.Update(a.Position, a.Rotation(), Tracking::DeltaTime());
        auto origin = tracker.GetLocalOrigin();
        if (!origin.IsFinite()) return;
        Config::SetPivotPosition(hand, origin);
    }
    void OnGrabFinished(Hand hand, ReeTransform const&, ReeTransform const&) override { Gizmos::SetOrthonormalFocus(hand, false); }
};

#pragma endregion

#pragma region RotationAuto

class RotationAutoMode : public ModeManager {
public:
    RotationAutoMode() : ModeManager(AdjustmentMode::RotationAuto, 0, 0) {}

protected:
    static constexpr float MinimalAngularVelocity = 45;
    static constexpr float MaximalAngularVelocity = 360;
    Analysis::RelativeRotationTracker rotationTracker;
    Analysis::WeightedList measurements;
    Range velocityRange{MinimalAngularVelocity, MaximalAngularVelocity};
    Quat grabLocalRotation;
    Vec3 grabLocalDirection;

    void OnGrabStarted(Hand hand, ReeTransform const& a, ReeTransform const&) override {
        Gizmos::SetSphericalFocus(hand, true);
        grabLocalRotation = Config::Get(hand).Rotation;
        grabLocalDirection = TransformUtils::DirectionFromRotation(grabLocalRotation);
        measurements.SetCapacity((size_t) std::clamp(Tracking::RefreshRate() * 2.0f, 60.0f, 300.0f));
        measurements.Clear();
        rotationTracker.Initialize(a.Rotation());
        Config::CreateUndoStep(Describe("Auto", hand, "Rotation"));
    }
    void OnGrabUpdated(Hand hand, ReeTransform const& a, ReeTransform const&) override {
        Vec3 positive = Tracking::HeadRotation() * Vec3::Forward();
        float velocity;
        Vec3 axis;
        if (!rotationTracker.Update(a.Rotation(), positive, Tracking::DeltaTime(), velocity, axis)) return;
        if (velocity <= MinimalAngularVelocity) return;
        measurements.Add(a.WorldToLocalDirection(axis), velocityRange.GetRatio(velocity));
        Vec3 localSaberDirection = measurements.Average().Normalized();
        if (localSaberDirection.SqrMagnitude() < 0.5f) return;
        Config::SetRotation(hand, FromToRotation(grabLocalDirection, localSaberDirection) * grabLocalRotation);
    }
    void OnGrabFinished(Hand hand, ReeTransform const&, ReeTransform const&) override { Gizmos::SetSphericalFocus(hand, false); }
};

#pragma endregion

#pragma region SwingBenchmark

static BenchmarkState benchmark;
Event<> BenchmarkUpdatedEvent;
static Quat referenceRotations[2] = {Quat::Identity(), Quat::Identity()};
static float swingCurveAngleDeg = 0;

BenchmarkState const& GetBenchmark() { return benchmark; }

class SwingBenchmarkMode : public ModeManager {
public:
    SwingBenchmarkMode() : ModeManager(AdjustmentMode::SwingBenchmark, 0, 0) {}

protected:
    Analysis::SwingAnalyzer analyzer{120};

    void OnGrabStarted(Hand hand, ReeTransform const&, ReeTransform const&) override {
        analyzer.SetCapacity((size_t) std::clamp(Tracking::RefreshRate() * 2.0f, 60.0f, 300.0f));
        analyzer.Reset();
        benchmark.SelectedHand = hand;
        benchmark.HasResults = true;
        benchmark.Running = true;
        BenchmarkUpdatedEvent.Invoke();
    }

    void OnGrabUpdated(Hand hand, ReeTransform const& a, ReeTransform const&) override {
        auto const& h = Config::Get(hand);
        Vec3 pivot = a.LocalToWorldPosition(h.PivotPosition);
        Quat saber = a.LocalToWorldRotation(h.Rotation);
        Vec3 tip = pivot + saber * Vec3::Forward();
        Vec3 initialNormal = Tracking::HeadRotation() * Vec3::Left();

        auto r = analyzer.Update(a, tip, pivot, initialNormal, Tracking::DeltaTime());
        float curve = std::asin(std::clamp(r.PivotHeight, -1.0f, 1.0f));
        float fullSwing = r.MaximalSwingAngle - r.MinimalSwingAngle;

        benchmark.IsSwingGood = fullSwing > FullSwingAngleRequirementDeg * Deg2Rad;
        swingCurveAngleDeg = curve * Rad2Deg;
        benchmark.SwingCurveDeg = (hand == Hand::Left ? -curve : curve) * Rad2Deg;
        benchmark.TipDeviation = r.TipDeviation;
        benchmark.PivotDeviation = r.PivotDeviation;
        benchmark.MinimalSwingAngle = r.MinimalSwingAngle * Rad2Deg;
        benchmark.MaximalSwingAngle = r.MaximalSwingAngle * Rad2Deg;
        benchmark.PlanePosition = r.PlanePosition;
        benchmark.PlaneRotation = r.PlaneRotation;
        BenchmarkUpdatedEvent.Invoke();
    }

    void OnGrabFinished(Hand hand, ReeTransform const&, ReeTransform const&) override {
        Vec3 averageNormal = analyzer.GetAverageLocalPlaneNormal();
        Vec3 currentDirection = TransformUtils::DirectionFromRotation(Config::Get(hand).Rotation);
        Quat reference = LookRotation(currentDirection, averageNormal);
        reference = reference * Euler(-swingCurveAngleDeg, 0, 0);
        reference = reference * Euler(0, 0, 90);
        referenceRotations[hand == Hand::Left ? 0 : 1] = reference;
        benchmark.Running = false;
        BenchmarkUpdatedEvent.Invoke();
    }
};

void BenchmarkAutoFix() {
    if (!benchmark.HasResults || !benchmark.IsSwingGood) return;
    Hand hand = benchmark.SelectedHand;
    Config::CreateUndoStep(std::string(HandName(hand)) + " Auto-fix");
    Config::SetRotation(hand, TransformUtils::AlignForwardVectors(Config::Get(hand).Rotation, referenceRotations[hand == Hand::Left ? 0 : 1]));
    Config::Save();
}

void BenchmarkSetAsReference() {
    if (!benchmark.HasResults || !benchmark.IsSwingGood) return;
    Hand hand = benchmark.SelectedHand;
    Config::CreateUndoStep("Set " + std::string(HandName(hand)) + " Reference");
    Config::SetReference(hand, true, referenceRotations[hand == Hand::Left ? 0 : 1]);
    Config::Save();
}

void BenchmarkReset() {
    benchmark = BenchmarkState();
    BenchmarkUpdatedEvent.Invoke();
}

#pragma endregion

#pragma region RoomOffset

class RoomOffsetMode : public ModeManager {
public:
    RoomOffsetMode() : ModeManager(AdjustmentMode::RoomOffset, 6, 6) {}

protected:
    Vec3 grabPosition, grabRoomCenter;

    void OnGrabStarted(Hand, ReeTransform const& a, ReeTransform const&) override {
        grabPosition = a.Position;
        grabRoomCenter = Config::GetRoomCenter();
    }
    void OnGrabUpdated(Hand, ReeTransform const& a, ReeTransform const&) override {
        if (!Config::HasGameSettings()) return;
        // tracking space movement -> room space (room is rotated around Y)
        Vec3 change = Euler(0, Config::GetRoomRotation(), 0) * (a.Position - grabPosition);
        Vec3 result = grabRoomCenter - change;
        Config::SetRoomCenter({
            Config::GetAllowRoomChange(0) ? result.x : grabRoomCenter.x,
            Config::GetAllowRoomChange(1) ? result.y : grabRoomCenter.y,
            Config::GetAllowRoomChange(2) ? result.z : grabRoomCenter.z,
        });
    }
    void OnGrabFinished(Hand, ReeTransform const&, ReeTransform const&) override {
        Vec3 before = grabRoomCenter, after = Config::GetRoomCenter();
        Config::CreateUndoStep("Room Offset", [before]() { Config::SetRoomCenter(before); }, [after]() { Config::SetRoomCenter(after); });
    }
};

#pragma endregion

static std::vector<std::unique_ptr<ModeManager>> managers;

void Initialize() {
    if (!managers.empty()) return;
    managers.push_back(std::make_unique<BasicMode>());
    managers.push_back(std::make_unique<PositionMode>());
    managers.push_back(std::make_unique<RotationMode>());
    managers.push_back(std::make_unique<PositionAutoMode>());
    managers.push_back(std::make_unique<RotationAutoMode>());
    managers.push_back(std::make_unique<SwingBenchmarkMode>());
    managers.push_back(std::make_unique<RoomOffsetMode>());
    for (auto& m : managers) m->Subscribe();
}
}
