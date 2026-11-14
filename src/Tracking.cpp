#include "Tracking.hpp"

#include "Config.hpp"
#include "main.hpp"

#include "GlobalNamespace/IVRPlatformHelper.hpp"
#include "GlobalNamespace/OVRInput.hpp"
#include "UnityEngine/Quaternion.hpp"
#include "UnityEngine/Time.hpp"
#include "UnityEngine/Vector3.hpp"
#include "UnityEngine/XR/XRNode.hpp"

using UnityEngine::XR::XRNode;

namespace EasyOffset::Tracking {

static ReeTransform leftTransform;
static ReeTransform rightTransform;
static Vec3 headPosition;
static Quat headRotation;
static GlobalNamespace::IVRPlatformHelper* platformHelper = nullptr;
static float deltaTime = 1.0f / 72.0f;
static float averageDeltaTime = 1.0f / 72.0f;

Event<> TransformsUpdatedEvent;
Event<float> TickEvent;
Event<Hand, ControllerButton> ButtonPressedEvent;
Event<Hand, ControllerButton> ButtonReleasedEvent;
Event<Hand> AssignedButtonPressedEvent;
Event<Hand> AssignedButtonReleasedEvent;
bool ForceInput = false;

ReeTransform& Get(Hand hand) { return hand == Hand::Left ? leftTransform : rightTransform; }
Vec3 HeadPosition() { return headPosition; }
Quat HeadRotation() { return headRotation; }
float DeltaTime() { return deltaTime; }
float RefreshRate() { return std::clamp(1.0f / averageDeltaTime, 60.0f, 144.0f); }

// Meta Touch grip pose relative to the native OVR controller pose (Unity coordinates).
// With it PC presets mean the same thing on Quest. From great-hb/EasyOffsetQuest (MIT).
static constexpr Vec3 LinkBasisPosition{0.0f, -0.03f, -0.04f};
static constexpr Quat LinkBasisRotation{-0.5f, 0.0f, 0.0f, 0.8660254f};

void ApplyTrackingSpace(Vec3& position, Quat& rotation) {
    if (Config::GetTrackingSpace() != TrackingSpace::PcLink) return;
    position = position + rotation * LinkBasisPosition;
    rotation = rotation * LinkBasisRotation;
}

void SetPlatformHelper(GlobalNamespace::IVRPlatformHelper* helper) { platformHelper = helper; }
GlobalNamespace::IVRPlatformHelper* GetPlatformHelper() { return platformHelper; }

static bool GetNodePose(XRNode node, Vec3& position, Quat& rotation) {
    if (!platformHelper) return false;
    UnityEngine::Vector3 pos;
    UnityEngine::Quaternion rot;
    if (!platformHelper->GetNodePose(node, 0, byref(pos), byref(rot))) return false;
    position = pos;
    rotation = rot;
    return position.IsFinite() && rotation.IsFinite();
}

bool GetRawPose(Hand hand, Vec3& position, Quat& rotation) {
    if (!GetNodePose(hand == Hand::Left ? XRNode::LeftHand : XRNode::RightHand, position, rotation)) return false;
    ApplyTrackingSpace(position, rotation);
    return true;
}

static void UpdateTransforms(Vec3 lp, Quat lr, Vec3 rp, Quat rr) {
    if (Config::PositionalSmoothing > 0) {
        float t = deltaTime * Config::PositionalSmoothing;
        leftTransform.Position = Lerp(leftTransform.Position, lp, t);
        rightTransform.Position = Lerp(rightTransform.Position, rp, t);
    } else {
        leftTransform.Position = lp;
        rightTransform.Position = rp;
    }
    if (Config::RotationalSmoothing > 0) {
        float t = deltaTime * Config::RotationalSmoothing;
        leftTransform.SetRotation(Lerp(leftTransform.Rotation(), lr, t));
        rightTransform.SetRotation(Lerp(rightTransform.Rotation(), rr, t));
    } else {
        leftTransform.SetRotation(lr);
        rightTransform.SetRotation(rr);
    }
    TransformsUpdatedEvent.Invoke();
}

#pragma region Input

using GlobalNamespace::OVRInput;
using RawButton = GlobalNamespace::OVRInput_RawButton;
using Controller = GlobalNamespace::OVRInput_Controller;

struct ButtonTracker {
    Hand hand;
    ControllerButton button;
    RawButton raw;
    bool pressed = false;
};

static ButtonTracker trackers[] = {
    {Hand::Left, ControllerButton::PrimaryButton, RawButton::X},
    {Hand::Left, ControllerButton::SecondaryButton, RawButton::Y},
    {Hand::Left, ControllerButton::GripButton, RawButton::LHandTrigger},
    {Hand::Left, ControllerButton::Primary2DAxisClick, RawButton::LThumbstick},
    {Hand::Right, ControllerButton::PrimaryButton, RawButton::A},
    {Hand::Right, ControllerButton::SecondaryButton, RawButton::B},
    {Hand::Right, ControllerButton::GripButton, RawButton::RHandTrigger},
    {Hand::Right, ControllerButton::Primary2DAxisClick, RawButton::RThumbstick},
};

static void Press(Hand hand, ControllerButton button) {
    ButtonPressedEvent.Invoke(hand, button);
    if (button == Config::GetAssignedButton()) AssignedButtonPressedEvent.Invoke(hand);
}

static void Release(Hand hand, ControllerButton button) {
    ButtonReleasedEvent.Invoke(hand, button);
    if (button == Config::GetAssignedButton()) AssignedButtonReleasedEvent.Invoke(hand);
}

static void UpdateInput() {
    bool active = Config::IsPanelVisible || ForceInput;
    for (auto& t : trackers) {
        bool value = active && OVRInput::Get(t.raw, Controller::Touch);
        if (value == t.pressed) continue;
        t.pressed = value;
        if (value) {
            Press(t.hand, t.button);
        } else {
            Release(t.hand, t.button);
        }
    }
}

#pragma endregion

static float saveTimer = 0;

void Tick() {
    deltaTime = std::max(UnityEngine::Time::get_deltaTime(), 1e-4f);
    averageDeltaTime = averageDeltaTime * 0.95f + std::min(deltaTime, 0.05f) * 0.05f;

    if (platformHelper) {
        Vec3 lp = leftTransform.Position, rp = rightTransform.Position;
        Quat lr = leftTransform.Rotation(), rr = rightTransform.Rotation();
        // keep the previous pose when tracking is lost, as PC does
        GetRawPose(Hand::Left, lp, lr);
        GetRawPose(Hand::Right, rp, rr);
        GetNodePose(XRNode::Head, headPosition, headRotation);
        UpdateTransforms(lp, lr, rp, rr);
    }

    UpdateInput();
    TickEvent.Invoke(deltaTime);

    saveTimer += deltaTime;
    if (saveTimer > 2.0f) {
        saveTimer = 0;
        Config::SaveIfDirty();
    }
}
}
