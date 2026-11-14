// Host test: simulates the three calibration maps with known offsets and checks
// that the solver recovers them. Build: see test/run.sh
#include <cstdio>
#include <random>

#include "CalibrationMaps.hpp"
#include "CalibrationSolver.hpp"
#include "TransformUtils.hpp"

using namespace EasyOffset;
using namespace EasyOffset::CalibrationSolver;

static std::mt19937 rng(1234);
static float Noise(float s) { return std::normal_distribution<float>(0, s)(rng); }

struct Truth {
    Vec3 pivot;      // controller-local wrist position
    Quat rotation;   // controller-local saber rotation
};

// Controller pose for a saber rotated by angle around worldAxis, pivoting around the wrist
static void Pose(Truth const& t, Vec3 wrist, Quat saberBase, Vec3 worldAxis, float angleDeg, float jitter, Vec3& pos, Quat& rot) {
    Quat saber = AngleAxis(angleDeg, worldAxis) * saberBase;
    // small roll around the saber axis (forearm pronation) + noise
    saber = saber * Euler(Noise(jitter), Noise(jitter), Noise(jitter));
    rot = saber * Inverse(t.rotation);
    pos = wrist - rot * t.pivot + Vec3(Noise(0.001f), Noise(0.001f), Noise(0.001f));
}

static void Simulate(int step, Truth const truth[2], Recording& rec, float jitter) {
    float dt = 1.0f / 72.0f;
    auto const& map = CalibrationMaps::Maps()[step];
    for (float time = 0; time < map.Length; time += dt) {
        Sample s;
        s.Time = time;
        s.DeltaTime = dt;
        s.Head = Euler(0, Noise(3), 0);
        for (int h = 0; h < 2; h++) {
            Hand hand = h == 0 ? Hand::Left : Hand::Right;
            Vec3 wrist(h == 0 ? -0.25f : 0.25f, 1.1f, 0.35f);
            // saber points forward and up a bit, lying in the swing plane
            Quat base = Euler(-10, h == 0 ? -8.0f : 8.0f, 0);
            Vec3 axis = base * Vec3::Right();
            float angle = 0;
            for (auto const& n : map.Notes) {
                if (n.NoteHand != hand) continue;
                float dtn = time - n.Time;
                float span = step == 1 ? 0.16f : 0.35f;
                float amplitude = step == 1 ? 50.0f : 120.0f;
                if (n.Direction == CalibrationMaps::Cut::DownLeft || n.Direction == CalibrationMaps::Cut::DownRight) {
                    float tilt = n.Direction == CalibrationMaps::Cut::DownLeft ? 45.0f : -45.0f;
                    axis = AngleAxis(tilt, base * Vec3::Forward()) * (base * Vec3::Right());
                }
                if (std::fabs(dtn) < span) {
                    float phase = dtn / span;  // -1..1
                    float sign = (n.Direction == CalibrationMaps::Cut::Up) ? -1.0f : 1.0f;
                    angle = sign * amplitude * 0.5f * std::sin(phase * 1.5707963f);
                }
            }
            Vec3 pos;
            Quat rot;
            Pose(truth[h], wrist, base, axis, angle, jitter, pos, rot);
            s.Position[h] = pos;
            s.Rotation[h] = rot;
            s.Valid[h] = true;
        }
        rec.push_back(s);
    }
}

int main() {
    Truth truth[2];
    truth[0] = {Vec3(-0.012f, 0.015f, -0.052f), Euler(33, 7, 2)};
    truth[1] = {TransformUtils::MirrorVector(truth[0].pivot), TransformUtils::MirrorRotation(truth[0].rotation)};

    int failures = 0;
    struct Case { char const* name; float jitter; float maxDir; float maxPivot; };
    for (auto const& c : {Case{"realistic noise (0.15 deg)", 0.15f, 1.0f, 0.5f}, Case{"heavy noise (1 deg/frame)", 1.0f, 3.0f, 1.5f}}) {
    std::printf("== %s\n", c.name);
    std::array<Recording, 3> recs;
    for (int i = 0; i < 3; i++) Simulate(i, truth, recs[i], c.jitter);
    for (int h = 0; h < 2; h++) {
        Hand hand = h == 0 ? Hand::Left : Hand::Right;
        Config::HandOffsets prior;
        prior.PivotPosition = {h == 0 ? -0.015f : 0.015f, 0.011f, -0.044f};
        prior.Rotation = h == 0 ? Euler(27, 5, 3) : TransformUtils::MirrorRotation(Euler(27, 5, 3));
        auto r = SolveHand(hand, recs, prior, prior);

        float dirError = AngleBetween(TransformUtils::DirectionFromRotation(r.Offsets.Rotation), TransformUtils::DirectionFromRotation(truth[h].rotation));
        float priorError = AngleBetween(TransformUtils::DirectionFromRotation(prior.Rotation), TransformUtils::DirectionFromRotation(truth[h].rotation));
        float pivotError = Distance(r.Offsets.PivotPosition, truth[h].pivot) * 100;
        std::printf("%s: rot measured=%d twoPlanes=%d planeAngle=%.1f qV=%.2f qD=%.2f samples=%d\n", h == 0 ? "Left" : "Right", r.RotationMeasured, r.UsedTwoPlanes,
                    r.PlaneAngleDeg, r.VerticalQuality, r.DiagonalQuality, r.SwingSamples);
        std::printf("   direction error %.2f deg (prior was %.2f deg off), pivot measured=%d error %.2f cm, ref=%d\n", dirError, priorError, r.PivotMeasured, pivotError,
                    r.Offsets.HasReference);
        { Vec3 e = r.Offsets.PivotPosition - truth[h].pivot; std::printf("   pivot error xyz cm (%.2f %.2f %.2f)\n", e.x * 100, e.y * 100, e.z * 100); }
        if (!r.Notes.empty()) std::printf("   notes: %s\n", r.Notes.c_str());
        if (!r.RotationMeasured || dirError > c.maxDir || !r.PivotMeasured || pivotError > c.maxPivot || !r.UsedTwoPlanes) failures++;

        // curve relative to the stored reference should be ~0
        float curve, balance;
        TransformUtils::ToReferenceSpace(r.Offsets.Rotation, r.Offsets.ReferenceRotation, curve, balance);
        std::printf("   reference curve %.2f balance %.2f\n", curve, balance);
    }
    }

    {
        // player stands still: nothing may change
        std::array<Recording, 3> idle;
        Truth still[2] = {truth[0], truth[1]};
        for (int i = 0; i < 3; i++) {
            for (float t = 0; t < 8; t += 1 / 72.0f) {
                Sample s;
                s.Time = t;
                s.DeltaTime = 1 / 72.0f;
                for (int h = 0; h < 2; h++) {
                    s.Rotation[h] = Euler(Noise(0.1f), Noise(0.1f), Noise(0.1f)) * Inverse(still[h].rotation);
                    s.Position[h] = Vec3(0, 1, 0);
                    s.Valid[h] = true;
                }
                idle[i].push_back(s);
            }
        }
        Config::HandOffsets current;
        current.Rotation = Euler(10, 20, 30);
        current.PivotPosition = {0.01f, 0.02f, -0.03f};
        Config::HandOffsets prior;
        prior.Rotation = Euler(27, 5, 3);
        auto r = SolveHand(Hand::Left, idle, prior, current);
        bool ok = !r.RotationMeasured && !r.PivotMeasured && r.Offsets.Rotation.ExactlyEquals(current.Rotation);
        std::printf("== idle player: rotation kept=%d (notes: %s)\n", ok, r.Notes.c_str());
        if (!ok) failures++;
    }

    // math sanity: Euler <-> EulerAngles round trip (Unity ZXY)
    Vec3 e(20.5f, 4.1f, 2.8f);
    Vec3 back = TransformUtils::EulerFromRotation(Euler(e));
    std::printf("euler round trip: (%.3f %.3f %.3f)\n", back.x, back.y, back.z);
    if (Distance(e, back) > 0.01f) failures++;
    // Unity: Quaternion.Euler(90,0,0) * forward = (0,-1,0)
    Vec3 f = Euler(90, 0, 0) * Vec3::Forward();
    std::printf("Euler(90,0,0)*fwd = (%.3f %.3f %.3f)\n", f.x, f.y, f.z);
    if (Distance(f, Vec3(0, -1, 0)) > 1e-4f) failures++;
    // LookRotation(forward) maps forward correctly
    Vec3 dir = Vec3(0.3f, -0.2f, 0.9f).Normalized();
    if (Distance(LookRotation(dir) * Vec3::Forward(), dir) > 1e-4f) failures++;

    std::printf(failures == 0 ? "ALL PASSED\n" : "FAILURES: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
