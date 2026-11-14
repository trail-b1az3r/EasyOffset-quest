#pragma once

#include "Enums.hpp"
#include "Math.hpp"

namespace GlobalNamespace {
class IVRPlatformHelper;
}

// PC: Abomination + AbominationTransformManager + ReeInputManager
namespace EasyOffset::Tracking {

// Smoothed controller poses in tracking space (room offset not applied)
ReeTransform& Get(Hand hand);
Vec3 HeadPosition();
Quat HeadRotation();

extern Event<> TransformsUpdatedEvent;
extern Event<float> TickEvent;  // after poses are updated, once per frame
extern Event<Hand, ControllerButton> ButtonPressedEvent;
extern Event<Hand, ControllerButton> ButtonReleasedEvent;
extern Event<Hand> AssignedButtonPressedEvent;
extern Event<Hand> AssignedButtonReleasedEvent;

// Converts a native Quest controller pose into the configured tracking space
void ApplyTrackingSpace(Vec3& position, Quat& rotation);

void SetPlatformHelper(GlobalNamespace::IVRPlatformHelper* helper);
GlobalNamespace::IVRPlatformHelper* GetPlatformHelper();

// Fresh (unsmoothed) controller pose in the configured tracking space
bool GetRawPose(Hand hand, Vec3& position, Quat& rotation);

// Called once per frame from the VRController hook
void Tick();

// Polls buttons even when the mod panel is closed (used by Auto Configure)
extern bool ForceInput;

float DeltaTime();
float RefreshRate();
}
