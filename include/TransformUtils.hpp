#pragma once

#include "Math.hpp"

// PC: TransformUtils
namespace EasyOffset::TransformUtils {

inline Vec3 MirrorVector(Vec3 v) { return {-v.x, v.y, v.z}; }

inline Quat MirrorRotation(Quat rotation) {
    float angle;
    Vec3 axis;
    ToAngleAxis(rotation, angle, axis);
    return AngleAxis(-angle, MirrorVector(axis));
}

inline float ClampRotation(float value) {
    if (value > 180) return -360 + value;
    if (value < -180) return 360 - value;
    return value;
}

inline Vec3 ClampEulerAngles(Vec3 e) { return {ClampRotation(e.x), ClampRotation(e.y), ClampRotation(e.z)}; }

inline Vec3 DirectionFromRotation(Quat rotation) { return rotation * Vec3::Forward(); }
inline Quat RotationFromDirection(Vec3 direction) { return LookRotation(direction, Vec3::Up()); }
inline Quat RotationFromEuler(Vec3 euler) { return Euler(euler); }
inline Vec3 EulerFromRotation(Quat rotation) { return ClampEulerAngles(EulerAngles(rotation)); }

inline Vec3 GetSaberTranslation(Vec3 pivotPosition, Quat saberRotation, float zOffset) {
    return pivotPosition + saberRotation * Vec3(0, 0, zOffset);
}

// x = vertical angle, y = horizontal angle (radians)
inline Vec3 SphericalToOrthoDirection(float xRad, float yRad) {
    float xz = std::cos(xRad);
    return {std::sin(yRad) * xz, -std::sin(xRad), std::cos(yRad) * xz};
}

inline void OrthoToSphericalDirection(Vec3 d, float& xRad, float& yRad) {
    yRad = std::atan2(d.x, d.z);
    xRad = -std::atan2(d.y, std::sqrt(d.z * d.z + d.x * d.x));
}

inline Quat AlignForwardVectors(Quat source, Quat target) {
    return FromToRotation(DirectionFromRotation(source), DirectionFromRotation(target)) * source;
}

inline void ToReferenceSpace(Quat original, Quat reference, float& curve, float& balance) {
    auto difference = Inverse(reference) * original;
    float xRad, yRad;
    OrthoToSphericalDirection(DirectionFromRotation(difference), xRad, yRad);
    curve = -yRad * Rad2Deg;
    balance = xRad * Rad2Deg;
}

inline Vec3 FromReferenceSpace(Quat original, Quat reference, float curve, float balance) {
    auto result = reference * Euler(balance, -curve, 0.0f);
    return EulerFromRotation(AlignForwardVectors(original, result));
}
}
