#include "Config.hpp"
#include "Gizmos.hpp"
#include "Tracking.hpp"
#include "main.hpp"

#include "GlobalNamespace/IVRPlatformHelper.hpp"
#include "GlobalNamespace/MainSettingsMenuViewControllersInstaller.hpp"
#include "GlobalNamespace/SettingsApplicatorSO.hpp"
#include "GlobalNamespace/SettingsManager.hpp"
#include "GlobalNamespace/VRController.hpp"
#include "GlobalNamespace/VRControllerTransformOffset.hpp"
#include "UnityEngine/Pose.hpp"
#include "UnityEngine/Resources.hpp"
#include "UnityEngine/Time.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/XR/XRNode.hpp"
#include "Zenject/DiContainer.hpp"

using namespace GlobalNamespace;
using UnityEngine::XR::XRNode;

namespace EasyOffset {

static constexpr Vec3 DefaultLeftPosition{-0.2f, 0.05f, 0.0f};
static constexpr Vec3 DefaultRightPosition{0.2f, 0.05f, 0.0f};

// PC: VRControllerGetControllerOffsetPatch. EasyOffset replaces the base game offset.
MAKE_HOOK_MATCH(VRController_TryGetControllerOffset,
                static_cast<bool (*)(IVRPlatformHelper*, VRControllerTransformOffset*, ByRef<XRNode>, ByRef<UnityEngine::Pose>)>(&VRController::TryGetControllerOffset),
                bool, IVRPlatformHelper* vrPlatformHelper, VRControllerTransformOffset* transformOffset, ByRef<XRNode> node, ByRef<UnityEngine::Pose> poseOffset) {
    EnsureInitialized();
    if (!Config::GetEnabled()) return VRController_TryGetControllerOffset(vrPlatformHelper, transformOffset, node, poseOffset);
    *poseOffset = UnityEngine::Pose(Vec3::Zero(), Quat::Identity());
    return false;
}

// PC: VRControllerUpdatePatch. The original still runs for its bookkeeping
// (pose validity, frame counter, anchor events); then the pose is replaced.
MAKE_HOOK_MATCH(VRController_Update, &VRController::Update, void, VRController* self) {
    VRController_Update(self);
    EnsureInitialized();

    auto* helper = self->_vrPlatformHelper;
    if (helper) {
        Tracking::SetPlatformHelper(helper);
        static int lastTickFrame = -1;
        int frame = UnityEngine::Time::get_frameCount();
        if (frame != lastTickFrame) {
            lastTickFrame = frame;
            Tracking::Tick();
        }
    }

    if (!Config::GetEnabled() || self->_mouseMode || !helper) return;

    Hand hand;
    if (self->_node == XRNode::LeftHand) {
        hand = Hand::Left;
    } else if (self->_node == XRNode::RightHand) {
        hand = Hand::Right;
    } else {
        return;
    }

    Vec3 position;
    Quat rotation;
    if (Config::SmoothingEnabled) {
        auto const& smoothed = Tracking::Get(hand);
        position = smoothed.Position;
        rotation = smoothed.Rotation();
    } else {
        UnityEngine::Vector3 pos;
        UnityEngine::Quaternion rot;
        if (helper->GetNodePose(self->_node, self->_nodeIdx, byref(pos), byref(rot))) {
            position = pos;
            rotation = rot;
        } else {
            Vec3 lastPosition = self->_lastTrackedPosition;
            Quat lastRotation = self->_lastTrackedRotation;
            position = !ApproxEqual(lastPosition, Vec3::Zero()) ? lastPosition : (hand == Hand::Left ? DefaultLeftPosition : DefaultRightPosition);
            rotation = !ApproxEqual(lastRotation, Quat::Identity()) ? lastRotation : Quat::Identity();
        }
        Tracking::ApplyTrackingSpace(position, rotation);
    }

    // transform.Translate(translation); transform.rotation *= saberRotation
    position = position + rotation * Config::GetSaberTranslation(hand);
    rotation = rotation * Config::GetSaberRotation(hand);

    auto transform = self->get_transform();
    Gizmos::SetOrigin(transform->get_parent());
    transform->SetLocalPositionAndRotation(position, rotation);

    // 1.40 moves the base offset onto a separate view anchor; keep it neutral
    auto anchor = self->_viewAnchorTransform;
    if (anchor && anchor.ptr() != transform.ptr()) anchor->SetLocalPositionAndRotation(Vec3::Zero(), Quat::Identity());
}

// PC: MenuInstallerPermanentPatch. Grabs the game settings for Room Offset.
MAKE_HOOK_MATCH(MainSettingsMenuViewControllersInstaller_InstallBindings, &MainSettingsMenuViewControllersInstaller::InstallBindings, void,
                MainSettingsMenuViewControllersInstaller* self) {
    MainSettingsMenuViewControllersInstaller_InstallBindings(self);
    auto container = self->get_Container();
    if (!container) return;
    Config::SetGameSettings(container->TryResolve<SettingsManager*>(), container->TryResolve<SettingsApplicatorSO*>());
}

static void OnEnabledChanged(bool enabled) {
    if (enabled) return;
    // restore the base game offsets
    auto controllers = UnityEngine::Resources::FindObjectsOfTypeAll<VRController*>();
    for (auto controller : controllers)
        if (controller) controller->UpdateAnchorOffsetPose();
}

void InstallHooks() {
    INSTALL_HOOK(Logger, VRController_TryGetControllerOffset);
    INSTALL_HOOK(Logger, VRController_Update);
    INSTALL_HOOK(Logger, MainSettingsMenuViewControllersInstaller_InstallBindings);
    Config::EnabledChangedEvent.Subscribe(OnEnabledChanged);
}
}
