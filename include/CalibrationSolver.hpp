#pragma once

#include <array>
#include <string>
#include <vector>

#include "Config.hpp"
#include "Enums.hpp"
#include "Math.hpp"

// Turns the controller motion recorded in the three calibration maps into saber offsets.
//
//  * Every fast frame gives the controller-local axis the controller rotates around.
//    In a clean swing that axis is the swing plane normal, and the saber lies in the plane
//    (zero "curve", the same goal as the PC Swing Benchmark auto-fix).
//  * Map 1 (vertical up/down) + map 2 (inlines) give the vertical plane normal Nv,
//    map 3 (diagonal) gives a second plane normal Nd. The saber must lie in both planes,
//    so its direction is Nv x Nd: both pitch and yaw are measured.
//  * Map 2 (fast wrist-driven inlines) gives the wrist pivot via the PC Position Auto solver.
//  * The vertical plane is stored as the saber reference (Curve/Balance sliders).
namespace EasyOffset::CalibrationSolver {

struct Sample {
    float Time = 0;  // song time
    float DeltaTime = 0;
    std::array<Vec3, 2> Position;
    std::array<Quat, 2> Rotation;
    std::array<bool, 2> Valid{false, false};
    Quat Head = Quat::Identity();
};

using Recording = std::vector<Sample>;

struct HandResult {
    bool RotationMeasured = false;
    bool PivotMeasured = false;
    bool UsedTwoPlanes = false;
    Config::HandOffsets Offsets;
    float PlaneAngleDeg = 0;       // angle between vertical and diagonal planes
    float VerticalQuality = 0;     // 0..1
    float DiagonalQuality = 0;     // 0..1
    float RotationChangeDeg = 0;   // vs. offsets before calibration
    float PivotChangeCm = 0;
    int SwingSamples = 0;
    std::string Notes;
};

// prior: controller-type default, current: offsets before calibration
HandResult SolveHand(Hand hand, std::array<Recording, 3> const& recordings, Config::HandOffsets const& prior, Config::HandOffsets const& current);
}
