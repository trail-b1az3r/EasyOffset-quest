#pragma once

#include "Enums.hpp"

namespace UnityEngine {
class Transform;
}

// Lightweight in-world visualisation of the offsets (pivot, saber direction, benchmark plane).
// PC uses an asset bundle; on Quest the shapes are built from Unity primitives.
namespace EasyOffset::Gizmos {
void Initialize();
void SetOrthonormalFocus(Hand hand, bool focus);
void SetSphericalFocus(Hand hand, bool focus);
void Zoom(Hand hand, float zoom);
// Parent of the VRControllers (tracking space -> world)
void SetOrigin(UnityEngine::Transform* transform);
}
