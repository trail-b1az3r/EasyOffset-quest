#include "Config.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "TransformUtils.hpp"
#include "beatsaber-hook/shared/config/rapidjson-utils.hpp"
#include "main.hpp"

#include "BeatSaber/Settings/Settings.hpp"
#include "BeatSaber/Settings/RoomSettings.hpp"
#include "Unity/Mathematics/float3.hpp"
#include "GlobalNamespace/IVRPlatformHelper.hpp"
#include "GlobalNamespace/SettingsApplicatorSO.hpp"
#include "GlobalNamespace/SettingsManager.hpp"

namespace fs = std::filesystem;

namespace EasyOffset::Config {

static constexpr float PositionUnitScale = 0.01f;  // file stores centimeters, like PC
static constexpr char const* ConfigPath = "/sdcard/ModData/com.beatgames.beatsaber/Configs/EasyOffset.json";
static constexpr char const* PresetsPath = "/sdcard/ModData/com.beatgames.beatsaber/Mods/EasyOffset/Presets";

#pragma region State

static bool enabled = true;
static bool useFreeHand = true;
static bool showGizmos = true;
static bool allowRoom[3] = {true, true, true};
static ControllerButton assignedButton = ControllerButton::SecondaryButton;
static ControllerType controllerType = ControllerType::None;
static TrackingSpace trackingSpace = TrackingSpace::PcLink;
static HandOffsets hands[2];
static Vec3 cachedTranslation[2];
static bool dirty = false;
static bool disableChangeEvent = false;

Event<bool> EnabledChangedEvent;
Event<ControllerButton> AssignedButtonChangedEvent;
Event<ControllerType> ControllerTypeChangedEvent;
Event<> ConfigWasChangedEvent;
Event<> UndoListChangedEvent;
Event<AdjustmentMode> AdjustmentModeChangedEvent;

bool SmoothingEnabled = false;
float PositionalSmoothing = 0;
float RotationalSmoothing = 0;
bool IsPanelVisible = false;
static AdjustmentMode adjustmentMode = AdjustmentMode::None;

static HandOffsets& H(Hand hand) { return hands[hand == Hand::Left ? 0 : 1]; }

static void UpdateTranslation(Hand hand) {
    auto& h = H(hand);
    cachedTranslation[hand == Hand::Left ? 0 : 1] = TransformUtils::GetSaberTranslation(h.PivotPosition, h.Rotation, h.ZOffset);
}

static void NotifyChanged() {
    MarkDirty();
    if (disableChangeEvent) return;
    ConfigWasChangedEvent.Invoke();
}

struct ScopedSilence {
    ScopedSilence() { disableChangeEvent = true; }
    ~ScopedSilence() {
        disableChangeEvent = false;
        NotifyChanged();
    }
};

#pragma endregion

#pragma region Json helpers

using rapidjson::Document;
using rapidjson::Value;

static Value const* FindMember(Value const& obj, std::string_view key) {
    if (!obj.IsObject()) return nullptr;
    for (auto it = obj.MemberBegin(); it != obj.MemberEnd(); ++it) {
        std::string_view name(it->name.GetString(), it->name.GetStringLength());
        if (name.size() == key.size() && std::equal(name.begin(), name.end(), key.begin(), [](char a, char b) { return std::tolower(a) == std::tolower(b); })) {
            return &it->value;
        }
    }
    return nullptr;
}

static float GetFloat(Value const& obj, std::string_view key, float fallback) {
    auto v = FindMember(obj, key);
    return v && v->IsNumber() ? v->GetFloat() : fallback;
}
static bool GetBool(Value const& obj, std::string_view key, bool fallback) {
    auto v = FindMember(obj, key);
    return v && v->IsBool() ? v->GetBool() : fallback;
}
static std::string GetString(Value const& obj, std::string_view key, std::string fallback) {
    auto v = FindMember(obj, key);
    return v && v->IsString() ? std::string(v->GetString()) : fallback;
}
static Vec3 GetVector(Value const& obj, std::string_view key, Vec3 fallback) {
    auto v = FindMember(obj, key);
    if (!v || !v->IsObject()) return fallback;
    return {GetFloat(*v, "x", fallback.x), GetFloat(*v, "y", fallback.y), GetFloat(*v, "z", fallback.z)};
}
static Quat GetQuaternion(Value const& obj, std::string_view key, Quat fallback) {
    auto v = FindMember(obj, key);
    if (!v || !v->IsObject()) return fallback;
    Quat q{GetFloat(*v, "x", fallback.x), GetFloat(*v, "y", fallback.y), GetFloat(*v, "z", fallback.z), GetFloat(*v, "w", fallback.w)};
    return q.IsFinite() ? Normalize(q) : fallback;
}

template <typename W>
static void WriteVector(W& w, char const* key, Vec3 v) {
    w.Key(key);
    w.StartObject();
    w.Key("x"); w.Double(v.x);
    w.Key("y"); w.Double(v.y);
    w.Key("z"); w.Double(v.z);
    w.EndObject();
}
template <typename W>
static void WriteQuaternion(W& w, char const* key, Quat q) {
    w.Key(key);
    w.StartObject();
    w.Key("x"); w.Double(q.x);
    w.Key("y"); w.Double(q.y);
    w.Key("z"); w.Double(q.z);
    w.Key("w"); w.Double(q.w);
    w.EndObject();
}

template <typename W>
static void WriteHand(W& w, char const* prefix, HandOffsets const& h) {
    std::string p(prefix);
    WriteVector(w, (p + "SaberPivotPosition").c_str(), h.PivotPosition / PositionUnitScale);
    WriteVector(w, (p + "SaberRotationEuler").c_str(), TransformUtils::EulerFromRotation(h.Rotation));
    w.Key((p + "SaberZOffset").c_str());
    w.Double(h.ZOffset / PositionUnitScale);
    w.Key((p + "SaberHasReference").c_str());
    w.Bool(h.HasReference);
    WriteQuaternion(w, (p + "SaberReference").c_str(), h.ReferenceRotation);
}

static HandOffsets ReadHand(Value const& obj, std::string const& p, HandOffsets fallback) {
    HandOffsets h;
    h.PivotPosition = ClampMagnitude(GetVector(obj, p + "SaberPivotPosition", fallback.PivotPosition / PositionUnitScale) * PositionUnitScale, MaximalPositionOffset);
    h.Rotation = TransformUtils::RotationFromEuler(GetVector(obj, p + "SaberRotationEuler", TransformUtils::EulerFromRotation(fallback.Rotation)));
    h.ZOffset = GetFloat(obj, p + "SaberZOffset", fallback.ZOffset / PositionUnitScale) * PositionUnitScale;
    h.HasReference = GetBool(obj, p + "SaberHasReference", fallback.HasReference);
    h.ReferenceRotation = GetQuaternion(obj, p + "SaberReference", fallback.ReferenceRotation);
    if (!h.PivotPosition.IsFinite()) h.PivotPosition = fallback.PivotPosition;
    if (!h.Rotation.IsFinite()) h.Rotation = fallback.Rotation;
    if (!std::isfinite(h.ZOffset)) h.ZOffset = fallback.ZOffset;
    return h;
}

static bool WriteText(std::string const& path, std::string const& text) {
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::ofstream file(path, std::ios::trunc);
    if (!file) return false;
    file << text;
    return file.good();
}

static std::optional<std::string> ReadText(std::string const& path) {
    std::ifstream file(path);
    if (!file) return std::nullopt;
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

#pragma endregion

#pragma region Load / Save

void Load() {
    hands[0] = DefaultOffsets(ControllerType::None, Hand::Left);
    hands[1] = DefaultOffsets(ControllerType::None, Hand::Right);

    auto text = ReadText(ConfigPath);
    if (text) {
        Document doc;
        doc.Parse(text->c_str());
        if (!doc.HasParseError() && doc.IsObject()) {
            enabled = GetBool(doc, "Enabled", true);
            useFreeHand = GetBool(doc, "UseFreeHand", true);
            showGizmos = GetBool(doc, "ShowGizmos", true);
            allowRoom[0] = GetBool(doc, "AllowRoomXChange", true);
            allowRoom[1] = GetBool(doc, "AllowRoomYChange", true);
            allowRoom[2] = GetBool(doc, "AllowRoomZChange", true);
            assignedButton = ControllerButtonFromName(GetString(doc, "AssignedButton", "SecondaryButton"));
            controllerType = ControllerTypeFromName(GetString(doc, "ControllerType", "None"));
            trackingSpace = GetString(doc, "TrackingSpace", "PcLink") == "QuestNative" ? TrackingSpace::QuestNative : TrackingSpace::PcLink;
            hands[0] = ReadHand(doc, "Left", hands[0]);
            hands[1] = ReadHand(doc, "Right", hands[1]);
            Logger.info("Config loaded from {}", ConfigPath);
        } else {
            Logger.warn("Config file is not valid json, using defaults");
        }
    } else {
        Logger.info("No config file yet, using defaults");
        dirty = true;
    }

    UpdateTranslation(Hand::Left);
    UpdateTranslation(Hand::Right);
}

void MarkDirty() { dirty = true; }

void SaveIfDirty() {
    if (dirty) Save();
}

void Save() {
    dirty = false;
    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> w(buffer);
    w.StartObject();
    w.Key("ConfigVersion"); w.String("2.0");
    w.Key("Enabled"); w.Bool(enabled);
    w.Key("UseFreeHand"); w.Bool(useFreeHand);
    w.Key("ShowGizmos"); w.Bool(showGizmos);
    w.Key("AllowRoomXChange"); w.Bool(allowRoom[0]);
    w.Key("AllowRoomYChange"); w.Bool(allowRoom[1]);
    w.Key("AllowRoomZChange"); w.Bool(allowRoom[2]);
    w.Key("AssignedButton"); w.String(std::string(ControllerButtonName(assignedButton)).c_str());
    w.Key("ControllerType"); w.String(std::string(ControllerTypeName(controllerType)).c_str());
    w.Key("TrackingSpace"); w.String(trackingSpace == TrackingSpace::PcLink ? "PcLink" : "QuestNative");
    WriteHand(w, "Left", hands[0]);
    WriteHand(w, "Right", hands[1]);
    w.EndObject();
    if (!WriteText(ConfigPath, buffer.GetString())) Logger.error("Failed to write {}", ConfigPath);
}

#pragma endregion

#pragma region Settings

bool GetEnabled() { return enabled; }
void SetEnabled(bool value) {
    if (enabled == value) return;
    enabled = value;
    MarkDirty();
    EnabledChangedEvent.Invoke(value);
}

bool GetUseFreeHand() { return useFreeHand; }
void SetUseFreeHand(bool value) {
    useFreeHand = value;
    MarkDirty();
}

ControllerButton GetAssignedButton() { return assignedButton; }
void SetAssignedButton(ControllerButton value) {
    if (assignedButton == value) return;
    assignedButton = value;
    MarkDirty();
    AssignedButtonChangedEvent.Invoke(value);
}

ControllerType GetControllerType() { return controllerType; }
void SetControllerType(ControllerType value) {
    if (controllerType == value) return;
    controllerType = value;
    MarkDirty();
    ControllerTypeChangedEvent.Invoke(value);
}

TrackingSpace GetTrackingSpace() { return trackingSpace; }
void SetTrackingSpace(TrackingSpace value) {
    trackingSpace = value;
    MarkDirty();
}

bool GetShowGizmos() { return showGizmos; }
void SetShowGizmos(bool value) {
    showGizmos = value;
    MarkDirty();
}

bool GetAllowRoomChange(int axis) { return allowRoom[std::clamp(axis, 0, 2)]; }
void SetAllowRoomChange(int axis, bool value) {
    allowRoom[std::clamp(axis, 0, 2)] = value;
    MarkDirty();
}

AdjustmentMode GetAdjustmentMode() { return adjustmentMode; }
void SetAdjustmentMode(AdjustmentMode mode) {
    adjustmentMode = mode;
    AdjustmentModeChangedEvent.Invoke(mode);
}

#pragma endregion

#pragma region Offsets

HandOffsets const& Get(Hand hand) { return H(hand); }
Vec3 GetSaberTranslation(Hand hand) { return cachedTranslation[hand == Hand::Left ? 0 : 1]; }
Quat GetSaberRotation(Hand hand) { return H(hand).Rotation; }
Vec3 GetRotationEuler(Hand hand) { return TransformUtils::EulerFromRotation(H(hand).Rotation); }

void SetPivotPosition(Hand hand, Vec3 value) {
    if (!value.IsFinite()) return;
    auto clamped = ClampMagnitude(value, MaximalPositionOffset);
    auto& h = H(hand);
    if (h.PivotPosition.x == clamped.x && h.PivotPosition.y == clamped.y && h.PivotPosition.z == clamped.z) return;
    h.PivotPosition = clamped;
    UpdateTranslation(hand);
    NotifyChanged();
}

void SetRotation(Hand hand, Quat value) {
    if (!value.IsFinite()) return;
    value = Normalize(value);
    auto& h = H(hand);
    if (h.Rotation.ExactlyEquals(value)) return;
    h.Rotation = value;
    UpdateTranslation(hand);
    NotifyChanged();
}

void SetZOffset(Hand hand, float value) {
    if (!std::isfinite(value)) return;
    auto& h = H(hand);
    if (h.ZOffset == value) return;
    h.ZOffset = value;
    UpdateTranslation(hand);
    NotifyChanged();
}

void SetReference(Hand hand, bool hasReference, Quat reference) {
    auto& h = H(hand);
    h.HasReference = hasReference;
    h.ReferenceRotation = Normalize(reference);
    NotifyChanged();
}

void SetHand(Hand hand, HandOffsets const& value) {
    {
        ScopedSilence silence;
        H(hand) = value;
        H(hand).PivotPosition = ClampMagnitude(value.PivotPosition, MaximalPositionOffset);
        UpdateTranslation(hand);
    }
}

void AlignReferenceToCurrent(Hand hand) {
    auto& h = H(hand);
    if (!h.HasReference) return;
    h.ReferenceRotation = TransformUtils::AlignForwardVectors(h.ReferenceRotation, h.Rotation);
    NotifyChanged();
}

void Mirror(Hand source, bool position, bool rotation, bool reference) {
    ScopedSilence silence;
    Hand target = OtherHand(source);
    auto const& s = H(source);
    auto& t = H(target);
    if (position) {
        t.PivotPosition = TransformUtils::MirrorVector(s.PivotPosition);
        t.ZOffset = s.ZOffset;
    }
    if (rotation) t.Rotation = TransformUtils::MirrorRotation(s.Rotation);
    if (reference) {
        t.HasReference = s.HasReference;
        t.ReferenceRotation = TransformUtils::MirrorRotation(s.ReferenceRotation);
    }
    UpdateTranslation(target);
}

void ResetOffsets(Hand hand, bool position, bool rotation, bool reference) {
    ScopedSilence silence;
    auto defaults = DefaultOffsets(controllerType, hand);
    auto& h = H(hand);
    if (position) {
        h.PivotPosition = defaults.PivotPosition;
        h.ZOffset = defaults.ZOffset;
    }
    if (rotation) h.Rotation = defaults.Rotation;
    if (reference) {
        h.HasReference = false;
        h.ReferenceRotation = Quat::Identity();
    }
    UpdateTranslation(hand);
}

// Starting points per controller family, in the PC (Meta Link) tracking space, left hand.
// Quest 2 values come from a typical PC EasyOffset Quest 2 preset; the others are
// adjusted for the different controller shapes. They are only starting points:
// Auto Configure measures the real values.
HandOffsets DefaultOffsets(ControllerType type, Hand hand) {
    HandOffsets h;
    switch (type) {
        case ControllerType::None:
            return h;  // PC defaults: saber at the controller origin
        case ControllerType::Quest2:
            h.PivotPosition = {-0.015f, 0.011f, -0.044f};
            h.Rotation = Euler(27.0f, 5.0f, 3.0f);
            break;
        case ControllerType::Quest1:
            h.PivotPosition = {-0.015f, 0.010f, -0.040f};
            h.Rotation = Euler(24.0f, 5.0f, 3.0f);
            break;
        case ControllerType::Quest3:
            h.PivotPosition = {-0.010f, 0.006f, -0.040f};
            h.Rotation = Euler(31.0f, 4.0f, 2.0f);
            break;
        case ControllerType::QuestPro:
            h.PivotPosition = {-0.012f, 0.004f, -0.050f};
            h.Rotation = Euler(29.0f, 4.0f, 2.0f);
            break;
    }
    h.ZOffset = DefaultZOffset;

    if (trackingSpace == TrackingSpace::QuestNative) {
        // express the Link-space offsets relative to the native controller pose
        constexpr Vec3 linkPos{0.0f, -0.03f, -0.04f};
        constexpr Quat linkRot{-0.5f, 0.0f, 0.0f, 0.8660254f};
        h.PivotPosition = linkPos + linkRot * h.PivotPosition;
        h.Rotation = linkRot * h.Rotation;
    }

    if (hand == Hand::Right) {
        h.PivotPosition = TransformUtils::MirrorVector(h.PivotPosition);
        h.Rotation = TransformUtils::MirrorRotation(h.Rotation);
    }
    return h;
}

#pragma endregion

#pragma region Undo

struct Snapshot {
    std::string Description;
    std::function<void()> OnUndo;
    std::function<void()> OnRedo;
    HandOffsets Left, Right;
};

static constexpr size_t MaxUndoSteps = 30;
static std::vector<Snapshot> undoSteps;
static std::vector<Snapshot> redoSteps;

static Snapshot MakeSnapshot(std::string description, std::function<void()> onUndo, std::function<void()> onRedo) {
    return {std::move(description), std::move(onUndo), std::move(onRedo), hands[0], hands[1]};
}

static void ApplySnapshot(Snapshot const& s) {
    ScopedSilence silence;
    hands[0] = s.Left;
    hands[1] = s.Right;
    UpdateTranslation(Hand::Left);
    UpdateTranslation(Hand::Right);
}

void CreateUndoStep(std::string description, std::function<void()> onUndo, std::function<void()> onRedo) {
    undoSteps.push_back(MakeSnapshot(std::move(description), std::move(onUndo), std::move(onRedo)));
    redoSteps.clear();
    if (undoSteps.size() > MaxUndoSteps) undoSteps.erase(undoSteps.begin());
    UndoListChangedEvent.Invoke();
}

bool UndoAvailable() { return !undoSteps.empty(); }
bool RedoAvailable() { return !redoSteps.empty(); }
std::string UndoDescription() { return undoSteps.empty() ? "" : undoSteps.back().Description; }
std::string RedoDescription() { return redoSteps.empty() ? "" : redoSteps.back().Description; }

void Undo() {
    if (undoSteps.empty()) return;
    auto snapshot = std::move(undoSteps.back());
    undoSteps.pop_back();
    redoSteps.push_back(MakeSnapshot(snapshot.Description, snapshot.OnUndo, snapshot.OnRedo));
    ApplySnapshot(snapshot);
    if (snapshot.OnUndo) snapshot.OnUndo();
    UndoListChangedEvent.Invoke();
}

void Redo() {
    if (redoSteps.empty()) return;
    auto snapshot = std::move(redoSteps.back());
    redoSteps.pop_back();
    undoSteps.push_back(MakeSnapshot(snapshot.Description, snapshot.OnUndo, snapshot.OnRedo));
    ApplySnapshot(snapshot);
    if (snapshot.OnRedo) snapshot.OnRedo();
    UndoListChangedEvent.Invoke();
}

#pragma endregion

#pragma region Presets

Preset GeneratePreset() {
    Preset p;
    p.UnixTimestamp = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    p.Type = controllerType;
    p.Left = hands[0];
    p.Right = hands[1];
    return p;
}

void ApplyPreset(Preset const& preset) {
    ScopedSilence silence;
    hands[0] = preset.Left;
    hands[1] = preset.Right;
    hands[0].PivotPosition = ClampMagnitude(hands[0].PivotPosition, MaximalPositionOffset);
    hands[1].PivotPosition = ClampMagnitude(hands[1].PivotPosition, MaximalPositionOffset);
    UpdateTranslation(Hand::Left);
    UpdateTranslation(Hand::Right);
}

std::string SerializePreset(Preset const& p) {
    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> w(buffer);
    w.StartObject();
    w.Key("Version"); w.String("2.0");
    w.Key("UnixTimestamp"); w.Int64(p.UnixTimestamp);
    w.Key("ControllerType"); w.String(std::string(ControllerTypeName(p.Type)).c_str());
    WriteHand(w, "Left", p.Left);
    WriteHand(w, "Right", p.Right);
    w.EndObject();
    return buffer.GetString();
}

static HandOffsets ReadV1Hand(Value const& obj, std::string const& p) {
    HandOffsets h;
    h.PivotPosition = {GetFloat(obj, p + "HandPivotPositionX", 0), GetFloat(obj, p + "HandPivotPositionY", 0), GetFloat(obj, p + "HandPivotPositionZ", -DefaultZOffset)};
    Vec3 dir{GetFloat(obj, p + "HandSaberDirectionX", 0), GetFloat(obj, p + "HandSaberDirectionY", 0), GetFloat(obj, p + "HandSaberDirectionZ", 1)};
    h.Rotation = TransformUtils::RotationFromDirection(dir);
    h.ZOffset = GetFloat(obj, p + "HandZOffset", DefaultZOffset);
    return h;
}

std::optional<Preset> ParsePreset(std::string const& json) {
    Document doc;
    doc.Parse(json.c_str());
    if (doc.HasParseError() || !doc.IsObject()) return std::nullopt;

    Preset p;
    p.Version = GetString(doc, "Version", "1.0");
    p.UnixTimestamp = 0;
    if (auto ts = FindMember(doc, "UnixTimestamp"); ts && ts->IsNumber()) p.UnixTimestamp = ts->GetInt64();
    p.Type = ControllerTypeFromName(GetString(doc, "ControllerType", "None"));

    if (p.Version == "2.0") {
        HandOffsets fallback;
        p.Left = ReadHand(doc, "Left", fallback);
        p.Right = ReadHand(doc, "Right", fallback);
        if (!FindMember(doc, "LeftSaberPivotPosition") || !FindMember(doc, "RightSaberPivotPosition")) return std::nullopt;
    } else if (p.Version == "1.0") {
        p.Left = ReadV1Hand(doc, "left");
        p.Right = ReadV1Hand(doc, "right");
    } else {
        return std::nullopt;
    }
    return p;
}

std::string PresetsDirectory() { return PresetsPath; }

static std::string PresetPath(std::string const& name) { return std::string(PresetsPath) + "/" + name + ".json"; }

std::vector<std::string> ListPresets() {
    std::vector<std::string> result;
    std::error_code ec;
    fs::create_directories(PresetsPath, ec);
    for (auto const& entry : fs::directory_iterator(PresetsPath, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        result.push_back(entry.path().stem().string());
    }
    std::sort(result.begin(), result.end());
    return result;
}

bool SavePresetFile(std::string const& name) {
    if (name.empty()) return false;
    return WriteText(PresetPath(name), SerializePreset(GeneratePreset()));
}

bool LoadPresetFile(std::string const& name) {
    auto text = ReadText(PresetPath(name));
    if (!text) return false;
    auto preset = ParsePreset(*text);
    if (!preset) return false;
    CreateUndoStep("Load preset " + name);
    ApplyPreset(*preset);
    return true;
}

bool DeletePresetFile(std::string const& name) {
    std::error_code ec;
    return fs::remove(PresetPath(name), ec);
}

#pragma endregion

#pragma region Game settings

static GlobalNamespace::SettingsManager* settingsManager = nullptr;
static GlobalNamespace::SettingsApplicatorSO* settingsApplicator = nullptr;

void SetGameSettings(GlobalNamespace::SettingsManager* manager, GlobalNamespace::SettingsApplicatorSO* applicator) {
    settingsManager = manager;
    settingsApplicator = applicator;
}

bool HasGameSettings() { return settingsManager != nullptr; }

Vec3 GetRoomCenter() {
    if (!settingsManager) return Vec3::Zero();
    auto c = settingsManager->settings.room.center;
    return Vec3(c.x, c.y, c.z);
}

void SetRoomCenter(Vec3 value) {
    if (!settingsManager) return;
    settingsManager->settings.room.center = Unity::Mathematics::float3(value.x, value.y, value.z);
    if (settingsApplicator) settingsApplicator->NotifyRoomTransformOffsetWasUpdated();
}

float GetRoomRotation() {
    if (!settingsManager) return 0;
    return settingsManager->settings.room.rotation;
}

#pragma endregion
}
