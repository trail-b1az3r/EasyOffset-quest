#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Enums.hpp"
#include "Math.hpp"

namespace GlobalNamespace {
class SettingsManager;
class SettingsApplicatorSO;
class IVRPlatformHelper;
}

// PC: PluginConfig + ConfigFileData + ConfigDefaults
namespace EasyOffset::Config {

constexpr float MaximalPositionOffset = 0.4f;
constexpr float DefaultZOffset = 0.155f;

struct HandOffsets {
    Vec3 PivotPosition{0, 0, -DefaultZOffset};
    Quat Rotation = Quat::Identity();
    float ZOffset = DefaultZOffset;
    bool HasReference = false;
    Quat ReferenceRotation = Quat::Identity();
};

struct Preset {
    std::string Version = "2.0";
    long long UnixTimestamp = 0;
    ControllerType Type = ControllerType::None;
    HandOffsets Left;
    HandOffsets Right;
};

// ---- Load / save ----
void Load();
void MarkDirty();
void SaveIfDirty();
void Save();

// ---- General settings ----
bool GetEnabled();
void SetEnabled(bool value);
extern Event<bool> EnabledChangedEvent;

bool GetUseFreeHand();
void SetUseFreeHand(bool value);

ControllerButton GetAssignedButton();
void SetAssignedButton(ControllerButton value);
extern Event<ControllerButton> AssignedButtonChangedEvent;

ControllerType GetControllerType();
void SetControllerType(ControllerType value);
extern Event<ControllerType> ControllerTypeChangedEvent;

TrackingSpace GetTrackingSpace();
void SetTrackingSpace(TrackingSpace value);

bool GetShowGizmos();
void SetShowGizmos(bool value);

bool GetAllowRoomChange(int axis);
void SetAllowRoomChange(int axis, bool value);

// ---- Saber offsets ----
HandOffsets const& Get(Hand hand);
Vec3 GetSaberTranslation(Hand hand);
Quat GetSaberRotation(Hand hand);
Vec3 GetRotationEuler(Hand hand);

void SetPivotPosition(Hand hand, Vec3 value);
void SetRotation(Hand hand, Quat value);
void SetZOffset(Hand hand, float value);
void SetReference(Hand hand, bool hasReference, Quat reference);
void SetHand(Hand hand, HandOffsets const& value);
void AlignReferenceToCurrent(Hand hand);

extern Event<> ConfigWasChangedEvent;

void Mirror(Hand source, bool position, bool rotation, bool reference);
void ResetOffsets(Hand hand, bool position, bool rotation, bool reference);

// Starting offsets for a controller family (used by Reset and as the auto-configure prior)
HandOffsets DefaultOffsets(ControllerType type, Hand hand);

// ---- Undo ----
void CreateUndoStep(std::string description, std::function<void()> onUndo = nullptr, std::function<void()> onRedo = nullptr);
bool UndoAvailable();
bool RedoAvailable();
std::string UndoDescription();
std::string RedoDescription();
void Undo();
void Redo();
extern Event<> UndoListChangedEvent;

// ---- Presets (PC compatible V1 / V2 json) ----
Preset GeneratePreset();
void ApplyPreset(Preset const& preset);
std::string SerializePreset(Preset const& preset);
std::optional<Preset> ParsePreset(std::string const& json);
std::string PresetsDirectory();
std::vector<std::string> ListPresets();
bool SavePresetFile(std::string const& name);
bool LoadPresetFile(std::string const& name);
bool DeletePresetFile(std::string const& name);

// ---- Runtime state (not saved) ----
extern bool SmoothingEnabled;
extern float PositionalSmoothing;
extern float RotationalSmoothing;
extern bool IsPanelVisible;

AdjustmentMode GetAdjustmentMode();
void SetAdjustmentMode(AdjustmentMode mode);
extern Event<AdjustmentMode> AdjustmentModeChangedEvent;

// ---- Base game settings ----
void SetGameSettings(GlobalNamespace::SettingsManager* manager, GlobalNamespace::SettingsApplicatorSO* applicator);
bool HasGameSettings();
Vec3 GetRoomCenter();
void SetRoomCenter(Vec3 value);
float GetRoomRotation();
}
