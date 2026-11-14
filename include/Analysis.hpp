#pragma once

#include <deque>
#include <vector>

#include "Math.hpp"

// Ports of the PC analysis helpers (WeightedList, SwingAnalyzer, RotationPointTracker)
// plus the small linear algebra they need.
namespace EasyOffset::Analysis {

struct Weighted {
    Vec3 Value;
    float Weight;
};

class WeightedList {
public:
    explicit WeightedList(size_t capacity = 300) : capacity(capacity) {}
    void SetCapacity(size_t value) { capacity = value; }
    void Clear() { entries.clear(); }
    void Add(Vec3 value, float weight) {
        entries.push_back({value, weight});
        while (entries.size() > capacity) entries.pop_front();
    }
    size_t Count() const { return entries.size(); }
    std::deque<Weighted> const& Entries() const { return entries; }
    Vec3 Average() const;

private:
    size_t capacity;
    std::deque<Weighted> entries;
};

float DeviationFromPoint(WeightedList const& list, Vec3 point);
float DeviationFromPlane(WeightedList const& list, Plane const& plane);
// Least squares plane through the points; normal sign follows initialNormal
Plane CalculatePlane(WeightedList const& list, Vec3 initialNormal);
void GetSwingAngles(WeightedList const& list, Vec3 origin, Quat rotation, float& minAngle, float& maxAngle);

// Symmetric 3x3 eigen decomposition (Jacobi). Eigenvalues sorted descending.
void SymmetricEigen(float const m[3][3], float values[3], Vec3 vectors[3]);
// Principal axis of a set of (sign-free) directions: eigenvector of sum(w * d d^T)
Vec3 PrincipalAxis(std::vector<Weighted> const& directions, float* strength = nullptr);
bool Solve3x3(float m[3][3], float b[3], Vec3& result);

// PC: RotationPointTracker. Finds the controller-local point the controller rotates around.
class RotationPointTracker {
public:
    explicit RotationPointTracker(size_t capacity) : capacity(capacity) {}
    void Clear();
    void Update(Vec3 position, Quat rotation, float deltaTime);
    // Feeds two poses directly (offline analysis)
    void AddPair(Vec3 positionA, Quat rotationA, Vec3 positionB, Quat rotationB, float deltaTime);
    Vec3 GetLocalOrigin() const;
    // Same, with a weak pull toward `prior` so directions the data cannot see keep the prior
    Vec3 GetLocalOrigin(Vec3 prior, float priorWeight) const;
    size_t Count() const { return points.size(); }

private:
    size_t capacity;
    std::vector<Vec3> points, directions;
    std::vector<float> weights;
    Vec3 previousPosition;
    Quat previousRotation;
    bool hasPrevious = false;
};

// PC: RelativeRotationTracker
class RelativeRotationTracker {
public:
    void Initialize(Quat rotation) {
        previous = rotation;
        initialized = true;
    }
    bool Update(Quat rotation, Vec3 positiveDirection, float deltaTime, float& angularVelocity, Vec3& axis);

private:
    Quat previous;
    bool initialized = false;
};

// PC: SwingAnalyzer, working in tracking space
class SwingAnalyzer {
public:
    explicit SwingAnalyzer(size_t capacity) : capacity(capacity), tips(capacity), pivots(capacity), localNormals(capacity) {}
    void SetCapacity(size_t value);
    void Reset();

    struct Result {
        Vec3 PlaneNormal;
        Vec3 PlanePosition;
        Quat PlaneRotation;
        float TipDeviation = 0;
        float PivotDeviation = 0;
        float PivotHeight = 0;
        float MinimalSwingAngle = 0;
        float MaximalSwingAngle = 0;
    };

    Result Update(ReeTransform const& controller, Vec3 tipPosition, Vec3 pivotPosition, Vec3 initialNormal, float deltaTime);
    Vec3 GetAverageLocalPlaneNormal() const;

private:
    size_t capacity;
    WeightedList tips, pivots, localNormals;
    Vec3 previousTip;
    bool hasPreviousTip = false;
};
}
