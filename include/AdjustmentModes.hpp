#pragma once

#include "Enums.hpp"
#include "Math.hpp"

namespace EasyOffset::AdjustmentModes {

void Initialize();

// Swing benchmark state, shown by the UI
struct BenchmarkState {
    bool HasResults = false;
    bool Running = false;
    bool IsSwingGood = false;
    Hand SelectedHand = Hand::Left;
    float SwingCurveDeg = 0;   // signed like PC (left hand inverted)
    float TipDeviation = 0;    // meters
    float PivotDeviation = 0;  // meters
    float MinimalSwingAngle = 0;
    float MaximalSwingAngle = 0;
    // last tracked saber, tracking space (for gizmos)
    Vec3 PlanePosition;
    Quat PlaneRotation;
};

BenchmarkState const& GetBenchmark();
extern Event<> BenchmarkUpdatedEvent;
void BenchmarkAutoFix();
void BenchmarkSetAsReference();
void BenchmarkReset();

// Required full swing angle for a valid benchmark
constexpr float FullSwingAngleRequirementDeg = 140.0f;
}
