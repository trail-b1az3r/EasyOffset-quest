#pragma once

// Self-contained vector/quaternion math that reproduces UnityEngine conventions
// (left-handed, ZXY euler order, Quaternion.* semantics) without il2cpp calls.

#include <algorithm>
#include <cmath>

#ifndef EASYOFFSET_HOST_TEST
#include "UnityEngine/Quaternion.hpp"
#include "UnityEngine/Vector3.hpp"
#define EO_UNITY_CONVERSIONS 1
#endif

namespace EasyOffset {

constexpr float Deg2Rad = 0.01745329251994329577f;
constexpr float Rad2Deg = 57.2957795130823208768f;

struct Vec3 {
    float x = 0, y = 0, z = 0;

    constexpr Vec3() = default;
    constexpr Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
#ifdef EO_UNITY_CONVERSIONS
    Vec3(UnityEngine::Vector3 const& v) : x(v.x), y(v.y), z(v.z) {}
    operator UnityEngine::Vector3() const { return UnityEngine::Vector3(x, y, z); }
#endif

    float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
    float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }

    constexpr Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    Vec3& operator+=(Vec3 o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(Vec3 o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }

    static constexpr Vec3 Zero() { return {0, 0, 0}; }
    static constexpr Vec3 One() { return {1, 1, 1}; }
    static constexpr Vec3 Forward() { return {0, 0, 1}; }
    static constexpr Vec3 Back() { return {0, 0, -1}; }
    static constexpr Vec3 Up() { return {0, 1, 0}; }
    static constexpr Vec3 Down() { return {0, -1, 0}; }
    static constexpr Vec3 Right() { return {1, 0, 0}; }
    static constexpr Vec3 Left() { return {-1, 0, 0}; }

    float SqrMagnitude() const { return x * x + y * y + z * z; }
    float Magnitude() const { return std::sqrt(SqrMagnitude()); }
    Vec3 Normalized() const {
        float m = Magnitude();
        return m > 1e-5f ? *this / m : Zero();
    }
    bool IsFinite() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
};

inline constexpr Vec3 operator*(float s, Vec3 v) { return v * s; }
inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float Distance(Vec3 a, Vec3 b) { return (a - b).Magnitude(); }
// Unity Vector3 == (approximate)
inline bool ApproxEqual(Vec3 a, Vec3 b) { return (a - b).SqrMagnitude() < 1e-10f; }
inline Vec3 Lerp(Vec3 a, Vec3 b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return a + (b - a) * t;
}
inline Vec3 ClampMagnitude(Vec3 v, float maxLength) {
    float sq = v.SqrMagnitude();
    if (sq > maxLength * maxLength) return v.Normalized() * maxLength;
    return v;
}
inline float AngleBetween(Vec3 a, Vec3 b) {
    float denom = std::sqrt(a.SqrMagnitude() * b.SqrMagnitude());
    if (denom < 1e-15f) return 0;
    return std::acos(std::clamp(Dot(a, b) / denom, -1.0f, 1.0f)) * Rad2Deg;
}

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;

    constexpr Quat() = default;
    constexpr Quat(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
#ifdef EO_UNITY_CONVERSIONS
    Quat(UnityEngine::Quaternion const& q) : x(q.x), y(q.y), z(q.z), w(q.w) {}
    operator UnityEngine::Quaternion() const { return UnityEngine::Quaternion(x, y, z, w); }
#endif

    static constexpr Quat Identity() { return {0, 0, 0, 1}; }

    constexpr Quat operator*(Quat r) const {
        return {
            w * r.x + x * r.w + y * r.z - z * r.y,
            w * r.y + y * r.w + z * r.x - x * r.z,
            w * r.z + z * r.w + x * r.y - y * r.x,
            w * r.w - x * r.x - y * r.y - z * r.z,
        };
    }

    Vec3 operator*(Vec3 v) const {
        float x2 = x * 2, y2 = y * 2, z2 = z * 2;
        float xx = x * x2, yy = y * y2, zz = z * z2;
        float xy = x * y2, xz = x * z2, yz = y * z2;
        float wx = w * x2, wy = w * y2, wz = w * z2;
        return {
            (1 - (yy + zz)) * v.x + (xy - wz) * v.y + (xz + wy) * v.z,
            (xy + wz) * v.x + (1 - (xx + zz)) * v.y + (yz - wx) * v.z,
            (xz - wy) * v.x + (yz + wx) * v.y + (1 - (xx + yy)) * v.z,
        };
    }

    bool IsFinite() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::isfinite(w); }
    bool ExactlyEquals(Quat o) const { return x == o.x && y == o.y && z == o.z && w == o.w; }
};

inline float Dot(Quat a, Quat b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
// Unity Quaternion == (approximate)
inline bool ApproxEqual(Quat a, Quat b) { return Dot(a, b) > 0.999999f; }

inline Quat Normalize(Quat q) {
    float m = std::sqrt(Dot(q, q));
    if (m < 1e-6f) return Quat::Identity();
    return {q.x / m, q.y / m, q.z / m, q.w / m};
}

inline Quat Inverse(Quat q) {
    float n = Dot(q, q);
    if (n < 1e-12f) return Quat::Identity();
    return {-q.x / n, -q.y / n, -q.z / n, q.w / n};
}

inline Quat AngleAxis(float angleDeg, Vec3 axis) {
    axis = axis.Normalized();
    if (axis.SqrMagnitude() < 1e-12f) return Quat::Identity();
    float half = angleDeg * Deg2Rad * 0.5f;
    float s = std::sin(half);
    return {axis.x * s, axis.y * s, axis.z * s, std::cos(half)};
}

inline void ToAngleAxis(Quat q, float& angleDeg, Vec3& axis) {
    q = Normalize(q);
    float w = std::clamp(q.w, -1.0f, 1.0f);
    angleDeg = 2.0f * std::acos(w) * Rad2Deg;
    float s = std::sqrt(std::max(0.0f, 1.0f - w * w));
    if (s < 1e-5f) {
        axis = Vec3::Right();
    } else {
        axis = Vec3(q.x / s, q.y / s, q.z / s);
    }
}

inline float Angle(Quat a, Quat b) {
    float d = std::min(std::fabs(Dot(a, b)), 1.0f);
    return d > 0.999999f ? 0.0f : std::acos(d) * 2.0f * Rad2Deg;
}

// Unity ZXY order: q = Y * X * Z
inline Quat Euler(float xDeg, float yDeg, float zDeg) {
    float hx = xDeg * Deg2Rad * 0.5f, hy = yDeg * Deg2Rad * 0.5f, hz = zDeg * Deg2Rad * 0.5f;
    Quat qx{std::sin(hx), 0, 0, std::cos(hx)};
    Quat qy{0, std::sin(hy), 0, std::cos(hy)};
    Quat qz{0, 0, std::sin(hz), std::cos(hz)};
    return qy * qx * qz;
}
inline Quat Euler(Vec3 e) { return Euler(e.x, e.y, e.z); }

inline float Repeat360(float v) {
    v = std::fmod(v, 360.0f);
    if (v < 0) v += 360.0f;
    return v;
}

// Matches Unity's Quaternion.eulerAngles (each component in [0, 360))
inline Vec3 EulerAngles(Quat q) {
    q = Normalize(q);
    float m12 = 2 * (q.y * q.z - q.w * q.x);
    float sx = std::clamp(-m12, -1.0f, 1.0f);
    float x = std::asin(sx);
    float y, z;
    if (std::fabs(sx) < 0.9999f) {
        float m02 = 2 * (q.x * q.z + q.w * q.y);
        float m22 = 1 - 2 * (q.x * q.x + q.y * q.y);
        float m10 = 2 * (q.x * q.y + q.w * q.z);
        float m11 = 1 - 2 * (q.x * q.x + q.z * q.z);
        y = std::atan2(m02, m22);
        z = std::atan2(m10, m11);
    } else {
        float m20 = 2 * (q.x * q.z - q.w * q.y);
        float m00 = 1 - 2 * (q.y * q.y + q.z * q.z);
        y = std::atan2(-m20, m00);
        z = 0;
    }
    return {Repeat360(x * Rad2Deg), Repeat360(y * Rad2Deg), Repeat360(z * Rad2Deg)};
}

inline Quat FromRotationMatrix(Vec3 c0, Vec3 c1, Vec3 c2) {
    // columns are the images of x, y, z axes
    float m00 = c0.x, m10 = c0.y, m20 = c0.z;
    float m01 = c1.x, m11 = c1.y, m21 = c1.z;
    float m02 = c2.x, m12 = c2.y, m22 = c2.z;
    float trace = m00 + m11 + m22;
    Quat q;
    if (trace > 0) {
        float s = std::sqrt(trace + 1.0f) * 2;
        q = {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s};
    } else if (m00 > m11 && m00 > m22) {
        float s = std::sqrt(1.0f + m00 - m11 - m22) * 2;
        q = {0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
    } else if (m11 > m22) {
        float s = std::sqrt(1.0f + m11 - m00 - m22) * 2;
        q = {(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s};
    } else {
        float s = std::sqrt(1.0f + m22 - m00 - m11) * 2;
        q = {(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s};
    }
    return Normalize(q);
}

inline Quat LookRotation(Vec3 forward, Vec3 up = Vec3::Up()) {
    Vec3 f = forward.Normalized();
    if (f.SqrMagnitude() < 1e-10f) return Quat::Identity();
    Vec3 r = Cross(up, f);
    if (r.SqrMagnitude() < 1e-10f) {
        // up parallel to forward: pick any perpendicular
        r = Cross(std::fabs(f.y) < 0.9f ? Vec3::Up() : Vec3::Right(), f);
    }
    r = r.Normalized();
    Vec3 u = Cross(f, r);
    return FromRotationMatrix(r, u, f);
}

inline Quat FromToRotation(Vec3 from, Vec3 to) {
    Vec3 a = from.Normalized(), b = to.Normalized();
    float d = std::clamp(Dot(a, b), -1.0f, 1.0f);
    if (d > 0.999999f) return Quat::Identity();
    if (d < -0.999999f) {
        Vec3 axis = Cross(Vec3::Right(), a);
        if (axis.SqrMagnitude() < 1e-6f) axis = Cross(Vec3::Up(), a);
        return AngleAxis(180.0f, axis);
    }
    return AngleAxis(std::acos(d) * Rad2Deg, Cross(a, b));
}

// Unity Quaternion.Lerp (normalized, shortest path, clamped t)
inline Quat Lerp(Quat a, Quat b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    if (Dot(a, b) < 0) b = {-b.x, -b.y, -b.z, -b.w};
    return Normalize({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t});
}

struct Plane {
    Vec3 normal = Vec3::Up();
    float distance = 0;

    Plane() = default;
    Plane(Vec3 inNormal, Vec3 inPoint) {
        normal = inNormal.Normalized();
        distance = -Dot(normal, inPoint);
    }
    Plane(Vec3 inNormal, float d) : normal(inNormal.Normalized()), distance(d) {}

    float GetDistanceToPoint(Vec3 p) const { return Dot(normal, p) + distance; }
    Vec3 ClosestPointOnPlane(Vec3 p) const { return p - normal * GetDistanceToPoint(p); }
};

// Position + rotation pair with cached inverse (PC: ReeTransform)
class ReeTransform {
public:
    Vec3 Position;

    ReeTransform() : ReeTransform(Vec3::Zero(), Quat::Identity()) {}
    ReeTransform(Vec3 position, Quat rotation) : Position(position) { SetRotation(rotation); }

    Quat Rotation() const { return rotation; }
    void SetRotation(Quat value) {
        rotation = value;
        inverseRotation = Inverse(value);
    }

    Vec3 LocalToWorldPosition(Vec3 local) const { return Position + rotation * local; }
    Vec3 LocalToWorldDirection(Vec3 local) const { return rotation * local; }
    Quat LocalToWorldRotation(Quat local) const { return rotation * local; }
    Vec3 WorldToLocalPosition(Vec3 world) const { return inverseRotation * (world - Position); }
    Vec3 WorldToLocalDirection(Vec3 world) const { return inverseRotation * world; }
    Quat WorldToLocalRotation(Quat world) const { return inverseRotation * world; }

private:
    Quat rotation;
    Quat inverseRotation;
};

// PC: Range
struct Range {
    float Start, End;
    float GetRatio(float value) const { return (value - Start) / (End - Start); }
    float GetRatioClamped(float value) const { return std::clamp(GetRatio(value), 0.0f, 1.0f); }
    float SlideBy(float ratio) const { return Start + (End - Start) * ratio; }
};
}
