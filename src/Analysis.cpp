#include "Analysis.hpp"

#include <algorithm>
#include <cmath>

namespace EasyOffset::Analysis {

#pragma region WeightedList

Vec3 WeightedList::Average() const {
    Vec3 result;
    float total = 0;
    for (auto const& e : entries) {
        total += e.Weight;
        result += e.Value * e.Weight;
    }
    return total > 0 ? result / total : Vec3::Zero();
}

float DeviationFromPoint(WeightedList const& list, Vec3 point) {
    if (list.Count() == 0) return 0;
    double sum = 0;
    for (auto const& e : list.Entries()) sum += (e.Value - point).SqrMagnitude();
    return std::sqrt(sum / list.Count());
}

float DeviationFromPlane(WeightedList const& list, Plane const& plane) {
    if (list.Count() == 0) return 0;
    double sum = 0;
    for (auto const& e : list.Entries()) {
        float d = plane.GetDistanceToPoint(e.Value);
        sum += d * d;
    }
    return std::sqrt(sum / list.Count());
}

#pragma endregion

#pragma region Linear algebra

void SymmetricEigen(float const input[3][3], float values[3], Vec3 vectors[3]) {
    double a[3][3], v[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) a[i][j] = input[i][j];

    for (int sweep = 0; sweep < 50; sweep++) {
        double off = std::fabs(a[0][1]) + std::fabs(a[0][2]) + std::fabs(a[1][2]);
        if (off < 1e-12) break;
        for (int p = 0; p < 2; p++) {
            for (int q = p + 1; q < 3; q++) {
                if (std::fabs(a[p][q]) < 1e-15) continue;
                double theta = (a[q][q] - a[p][p]) / (2 * a[p][q]);
                double t = (theta >= 0 ? 1.0 : -1.0) / (std::fabs(theta) + std::sqrt(theta * theta + 1));
                double c = 1 / std::sqrt(t * t + 1), s = t * c;
                for (int k = 0; k < 3; k++) {
                    double akp = a[k][p], akq = a[k][q];
                    a[k][p] = c * akp - s * akq;
                    a[k][q] = s * akp + c * akq;
                }
                for (int k = 0; k < 3; k++) {
                    double apk = a[p][k], aqk = a[q][k];
                    a[p][k] = c * apk - s * aqk;
                    a[q][k] = s * apk + c * aqk;
                }
                for (int k = 0; k < 3; k++) {
                    double vkp = v[k][p], vkq = v[k][q];
                    v[k][p] = c * vkp - s * vkq;
                    v[k][q] = s * vkp + c * vkq;
                }
            }
        }
    }

    int order[3] = {0, 1, 2};
    std::sort(order, order + 3, [&](int x, int y) { return a[x][x] > a[y][y]; });
    for (int i = 0; i < 3; i++) {
        int k = order[i];
        values[i] = (float) a[k][k];
        vectors[i] = Vec3((float) v[0][k], (float) v[1][k], (float) v[2][k]).Normalized();
    }
}

Vec3 PrincipalAxis(std::vector<Weighted> const& directions, float* strength) {
    float m[3][3] = {};
    float total = 0;
    for (auto const& d : directions) {
        Vec3 n = d.Value.Normalized();
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) m[i][j] += d.Weight * n[i] * n[j];
        total += d.Weight;
    }
    if (total <= 0) {
        if (strength) *strength = 0;
        return Vec3::Zero();
    }
    float values[3];
    Vec3 vectors[3];
    SymmetricEigen(m, values, vectors);
    // 1 = all directions agree, 1/3 = isotropic
    if (strength) *strength = values[0] / total;
    return vectors[0];
}

bool Solve3x3(float m[3][3], float b[3], Vec3& result) {
    constexpr int n = 3;
    for (int dia = 0; dia < n; dia++) {
        int maxRow = dia;
        float max = std::fabs(m[dia][dia]);
        for (int row = dia + 1; row < n; row++) {
            if (std::fabs(m[row][dia]) > max) {
                max = std::fabs(m[row][dia]);
                maxRow = row;
            }
        }
        if (max < 1e-9f) return false;
        if (maxRow != dia) {
            for (int i = 0; i < n; i++) std::swap(m[dia][i], m[maxRow][i]);
            std::swap(b[dia], b[maxRow]);
        }
        for (int row = dia + 1; row < n; row++) {
            float t = m[row][dia] / m[dia][dia];
            for (int col = dia + 1; col < n; col++) m[row][col] -= t * m[dia][col];
            m[row][dia] = 0;
            b[row] -= t * b[dia];
        }
    }
    for (int row = n - 1; row >= 0; row--) {
        float t = b[row];
        for (int j = n - 1; j > row; j--) t -= result[j] * m[row][j];
        result[row] = t / m[row][row];
    }
    return result.IsFinite();
}

Plane CalculatePlane(WeightedList const& list, Vec3 initialNormal) {
    Vec3 average = list.Average();
    float m[3][3] = {};
    for (auto const& e : list.Entries()) {
        Vec3 r = e.Value - average;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) m[i][j] += r[i] * r[j];
    }
    float values[3];
    Vec3 vectors[3];
    SymmetricEigen(m, values, vectors);
    Vec3 normal = vectors[2];  // smallest spread
    if (normal.SqrMagnitude() < 0.5f) normal = initialNormal.Normalized();
    if (Dot(normal, initialNormal) < 0) normal = -normal;
    return Plane(normal, average);
}

void GetSwingAngles(WeightedList const& list, Vec3 origin, Quat rotation, float& minAngle, float& maxAngle) {
    minAngle = maxAngle = 0;
    if (list.Count() == 0) return;
    Quat inverse = Inverse(rotation);

    std::vector<float> minimums, maximums;
    float absMin = 0, absMax = 0, previous = 0;
    bool previousPositive = false, hasPreviousDirection = false;
    size_t count = 0;
    float last = 0;

    for (auto const& e : list.Entries()) {
        Vec3 local = inverse * (e.Value - origin);
        float angle = std::atan2(local.y, local.x);
        last = angle;
        if (count++ == 0) {
            absMin = absMax = previous = angle;
            minimums.push_back(angle);
            maximums.push_back(angle);
            continue;
        }
        absMax = std::max(absMax, angle);
        absMin = std::min(absMin, angle);
        bool positive = angle - previous >= 0;
        if (hasPreviousDirection && positive != previousPositive) {
            (previousPositive ? maximums : minimums).push_back(previous);
        }
        hasPreviousDirection = true;
        previous = angle;
        previousPositive = positive;
    }

    if (maximums.size() > 1) maximums.erase(maximums.begin());
    if (minimums.size() > 1) minimums.erase(minimums.begin());
    if (last >= absMax) {
        absMax = last;
        maximums.push_back(last);
    }
    if (last <= absMin) {
        absMin = last;
        minimums.push_back(last);
    }

    float margin = std::fabs(absMax - absMin) / 6;
    auto average = [margin](std::vector<float> const& angles, float anchor) {
        float sum = 0;
        int n = 0;
        for (float a : angles) {
            if (std::fabs(a - anchor) > margin) continue;
            sum += a;
            n++;
        }
        return n > 0 ? sum / n : 0.0f;
    };
    minAngle = average(minimums, absMin);
    maxAngle = average(maximums, absMax);
}

#pragma endregion

#pragma region RotationPointTracker

void RotationPointTracker::Clear() {
    points.clear();
    directions.clear();
    weights.clear();
    hasPrevious = false;
}

static void CalculateOriginLine(Vec3 positionA, Quat rotationA, Vec3 positionB, Quat rotationB, Vec3& worldPoint, Vec3& worldDirection) {
    float angle;
    ToAngleAxis(rotationB * Inverse(rotationA), angle, worldDirection);
    Vec3 diff = positionB - positionA;
    float halfAngle = angle * Deg2Rad * 0.5f;
    Vec3 rayOrigin = (positionA + positionB) * 0.5f;
    Vec3 rayDirection = (AngleAxis(90.0f, worldDirection) * diff).Normalized();
    float rayLength = std::cos(halfAngle) * diff.Magnitude() / (std::sin(halfAngle) * 2);
    worldPoint = rayOrigin + rayDirection * rayLength;
}

void RotationPointTracker::AddPair(Vec3 positionA, Quat rotationA, Vec3 positionB, Quat rotationB, float deltaTime) {
    if (deltaTime <= 0 || ApproxEqual(positionA, positionB) || ApproxEqual(rotationA, rotationB)) return;
    Vec3 worldPoint, worldDirection;
    CalculateOriginLine(positionA, rotationA, positionB, rotationB, worldPoint, worldDirection);
    if (!worldPoint.IsFinite() || !worldDirection.IsFinite()) return;
    float angularVelocity = Angle(rotationA, rotationB) / deltaTime;

    Quat inverse = Inverse(rotationB);
    Vec3 localPoint = inverse * (worldPoint - positionB);
    Vec3 localDirection = inverse * worldDirection;

    if (points.size() < capacity) {
        points.push_back(localPoint);
        directions.push_back(localDirection);
        weights.push_back(angularVelocity);
        return;
    }
    // replace the slowest sample
    size_t worst = 0;
    for (size_t i = 1; i < weights.size(); i++)
        if (weights[i] < weights[worst]) worst = i;
    if (angularVelocity < weights[worst]) return;
    points[worst] = localPoint;
    directions[worst] = localDirection;
    weights[worst] = angularVelocity;
}

void RotationPointTracker::Update(Vec3 position, Quat rotation, float deltaTime) {
    if (hasPrevious) AddPair(previousPosition, previousRotation, position, rotation, deltaTime);
    previousPosition = position;
    previousRotation = rotation;
    hasPrevious = true;
}

// Least squares point closest to all rotation axis lines
Vec3 RotationPointTracker::GetLocalOrigin() const { return GetLocalOrigin(Vec3::Zero(), 0.0f); }

Vec3 RotationPointTracker::GetLocalOrigin(Vec3 prior, float priorWeight) const {
    // Minimise sum |(I - d d^T)(p - a)|^2. The normal matrix N = sum (I - d d^T) is
    // rank deficient along an axis the data never rotates around (e.g. pure vertical
    // swings cannot locate the wrist along the swing axis). There p keeps the prior.
    float n[3][3] = {};
    float b[3] = {};
    for (size_t i = 0; i < points.size(); i++) {
        Vec3 a = points[i], d = directions[i].Normalized();
        float da = Dot(d, a);
        for (int j = 0; j < 3; j++) {
            for (int k = 0; k < 3; k++) n[j][k] -= d[j] * d[k];
            n[j][j] += 1;
            b[j] += a[j] - d[j] * da;  // (I - d d^T) a
        }
    }
    float count = (float) points.size();
    if (count < 1) return Vec3(NAN, NAN, NAN);

    float values[3];
    Vec3 vectors[3];
    SymmetricEigen(n, values, vectors);

    // p = prior + sum over observable eigen-directions of (v . (b - N prior)) / lambda * v
    Vec3 nPrior;
    for (int j = 0; j < 3; j++) nPrior[j] = n[j][0] * prior.x + n[j][1] * prior.y + n[j][2] * prior.z;
    Vec3 residual = Vec3(b[0], b[1], b[2]) - nPrior;
    Vec3 result = prior;
    float threshold = std::max(priorWeight, 1e-4f) * count;
    for (int i = 0; i < 3; i++) {
        if (values[i] < threshold) continue;
        result += vectors[i] * (Dot(vectors[i], residual) / values[i]);
    }
    return result;
}

#pragma endregion

#pragma region RelativeRotationTracker

bool RelativeRotationTracker::Update(Quat rotation, Vec3 positiveDirection, float deltaTime, float& angularVelocity, Vec3& axis) {
    if (!initialized || ApproxEqual(rotation, previous) || deltaTime <= 0) {
        angularVelocity = 0;
        axis = Vec3::Zero();
        return false;
    }
    angularVelocity = Angle(previous, rotation) / deltaTime;
    float angle;
    ToAngleAxis(rotation * Inverse(previous), angle, axis);
    previous = rotation;
    if (Dot(positiveDirection, axis) < 0) axis = -axis;
    return true;
}

#pragma endregion

#pragma region SwingAnalyzer

void SwingAnalyzer::SetCapacity(size_t value) {
    capacity = value;
    tips.SetCapacity(value);
    pivots.SetCapacity(value);
    localNormals.SetCapacity(value);
}

void SwingAnalyzer::Reset() {
    tips.Clear();
    pivots.Clear();
    localNormals.Clear();
    hasPreviousTip = false;
}

SwingAnalyzer::Result SwingAnalyzer::Update(ReeTransform const& controller, Vec3 tipPosition, Vec3 pivotPosition, Vec3 initialNormal, float deltaTime) {
    tips.Add(tipPosition, 1);
    pivots.Add(pivotPosition, 1);

    Result r;
    Vec3 averagePivot = pivots.Average();
    r.PivotDeviation = DeviationFromPoint(pivots, averagePivot);
    Plane plane = CalculatePlane(tips, initialNormal);
    r.TipDeviation = DeviationFromPlane(tips, plane);
    r.PivotHeight = plane.GetDistanceToPoint(averagePivot);
    r.PlaneNormal = plane.normal;
    r.PlanePosition = plane.ClosestPointOnPlane(averagePivot);
    r.PlaneRotation = LookRotation(plane.normal, Vec3::Up());
    GetSwingAngles(tips, r.PlanePosition, r.PlaneRotation, r.MinimalSwingAngle, r.MaximalSwingAngle);

    if (hasPreviousTip && deltaTime > 0) {
        Vec3 localNormal = controller.WorldToLocalDirection(plane.normal);
        float tipVelocity = (tipPosition - previousTip).Magnitude() / deltaTime;
        float ratio = (float) localNormals.Count() / (float) capacity;
        localNormals.Add(localNormal, std::sqrt(ratio) * tipVelocity * tipVelocity);
    }
    previousTip = tipPosition;
    hasPreviousTip = true;
    return r;
}

Vec3 SwingAnalyzer::GetAverageLocalPlaneNormal() const { return hasPreviousTip ? localNormals.Average() : Vec3::Forward(); }

#pragma endregion
}
