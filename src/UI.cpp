#include "UI.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <vector>

#include "AdjustmentModes.hpp"
#include "Calibration.hpp"
#include "Config.hpp"
#include "Tracking.hpp"
#include "TransformUtils.hpp"
#include "main.hpp"

#include "bsml/shared/BSML-Lite.hpp"
#include "bsml/shared/BSML.hpp"
#include "bsml/shared/BSML/Components/ModalView.hpp"
#include "bsml/shared/BSML/Components/Settings/IncrementSetting.hpp"
#include "bsml/shared/BSML/Components/Settings/SliderSetting.hpp"
#include "bsml/shared/BSML/Components/Settings/ToggleSetting.hpp"

#include "HMUI/CurvedTextMeshPro.hpp"
#include "HMUI/ViewController.hpp"
#include "TMPro/FontStyles.hpp"
#include "TMPro/TextAlignmentOptions.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/RectOffset.hpp"
#include "UnityEngine/Transform.hpp"
#include "UnityEngine/UI/Button.hpp"
#include "UnityEngine/UI/ContentSizeFitter.hpp"
#include "UnityEngine/UI/HorizontalLayoutGroup.hpp"
#include "UnityEngine/UI/LayoutElement.hpp"
#include "UnityEngine/UI/VerticalLayoutGroup.hpp"

using namespace BSML::Lite;
using UnityEngine::Vector2;

namespace EasyOffset::UI {

#pragma region State

static UnityW<HMUI::ViewController> viewController;
static bool refreshing = false;
static bool needsRefresh = false;
static float refreshTimer = 0;

static UnityW<BSML::ToggleSetting> enabledToggle, freeHandToggle, gizmosToggle;
static UnityW<BSML::IncrementSetting> modeSetting, buttonSetting, trackingSetting, presetSetting;
static UnityW<HMUI::CurvedTextMeshPro> hintText, controllerText, benchmarkText, presetStatusText, calibrationStatusText, directHeader[2];
static UnityW<UnityEngine::UI::Button> undoButton, redoButton;

enum Slider { PosX, PosY, PosZ, RotX, RotY, RotZ, ZOff, Curve, Balance, SliderCount };
static UnityW<BSML::SliderSetting> sliders[2][SliderCount];

static UnityW<BSML::ModalView> pickerModal, calibrationModal;
static UnityW<HMUI::CurvedTextMeshPro> calibrationTitle, calibrationBody, calibrationCountdown;
static UnityW<UnityEngine::UI::Button> calibrationStart, calibrationCancel, calibrationOk;

static std::vector<std::string> presetNames;

static std::vector<AdjustmentMode> const& Modes() {
    static std::vector<AdjustmentMode> modes(AllAdjustmentModes.begin(), AllAdjustmentModes.end());
    return modes;
}

static int ModeIndex(AdjustmentMode mode) {
    for (size_t i = 0; i < Modes().size(); i++)
        if (Modes()[i] == mode) return (int) i;
    return 0;
}

static int ButtonIndex(ControllerButton button) {
    for (size_t i = 0; i < AvailableButtons.size(); i++)
        if (AvailableButtons[i] == button) return (int) i;
    return 0;
}

static std::string Format(char const* fmt, float value) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, fmt, value);
    return buffer;
}

#pragma endregion

#pragma region Layout helpers

static UnityEngine::UI::HorizontalLayoutGroup* Row(UnityEngine::Transform* parent) {
    auto row = CreateHorizontalLayoutGroup(parent);
    row->set_spacing(2);
    row->set_childForceExpandWidth(true);
    row->set_childControlWidth(true);
    auto fitter = row->GetComponent<UnityEngine::UI::ContentSizeFitter*>();
    if (fitter) fitter->set_verticalFit(UnityEngine::UI::ContentSizeFitter::FitMode::PreferredSize);
    return row;
}

static HMUI::CurvedTextMeshPro* Header(UnityEngine::Transform* parent, std::string_view text) {
    auto t = CreateText(parent, std::string(text), TMPro::FontStyles::Bold, 4.5f);
    t->set_alignment(TMPro::TextAlignmentOptions::Center);
    t->set_color({0.6f, 0.85f, 1.0f, 1.0f});
    return t;
}

static HMUI::CurvedTextMeshPro* Label(UnityEngine::Transform* parent, std::string_view text, float size = 3.2f) {
    auto t = CreateText(parent, std::string(text), TMPro::FontStyles::Normal, size);
    t->set_alignment(TMPro::TextAlignmentOptions::Center);
    t->set_enableWordWrapping(true);
    return t;
}

static UnityEngine::UI::Button* Button(UnityEngine::Transform* parent, std::string_view text, std::function<void()> onClick) {
    auto b = CreateUIButton(parent, std::string(text), onClick);
    SetButtonTextSize(b, 3.2f);
    return b;
}

#pragma endregion

#pragma region Refresh

static void SetSlider(Hand hand, Slider s, float value) {
    auto& slider = sliders[hand == Hand::Left ? 0 : 1][s];
    if (slider) slider->set_Value(value);
}

static void RefreshUndo() {
    if (undoButton) {
        SetButtonText(undoButton.ptr(), Config::UndoAvailable() ? "Undo: " + Config::UndoDescription() : "Undo");
        undoButton->set_interactable(Config::UndoAvailable());
    }
    if (redoButton) {
        SetButtonText(redoButton.ptr(), Config::RedoAvailable() ? "Redo: " + Config::RedoDescription() : "Redo");
        redoButton->set_interactable(Config::RedoAvailable());
    }
}

static void RefreshBenchmark() {
    if (!benchmarkText) return;
    auto const& b = AdjustmentModes::GetBenchmark();
    if (!b.HasResults) {
        benchmarkText->set_text("Swing Benchmark: select the mode, hold the button and swing like in game.");
        return;
    }
    float swing = b.MaximalSwingAngle - b.MinimalSwingAngle;
    char buffer[400];
    std::snprintf(buffer, sizeof buffer,
                  "<b>%s hand</b>%s\nCurve: <b>%.1f°</b>   Tip wobble: %.1f cm   Pivot wobble: %.1f cm\nSwing angle: %s%.0f°</color> / %.0f°",
                  std::string(HandName(b.SelectedHand)).c_str(), b.Running ? " (recording)" : "", b.SwingCurveDeg, b.TipDeviation * 100, b.PivotDeviation * 100,
                  b.IsSwingGood ? "<color=#66FF66>" : "<color=#FF8844>", swing, AdjustmentModes::FullSwingAngleRequirementDeg);
    benchmarkText->set_text(buffer);
}

static void RefreshPresets() {
    presetNames = Config::ListPresets();
    if (presetSetting) {
        presetSetting->maxValue = std::max(0, (int) presetNames.size() - 1);
        if (presetSetting->get_Value() > presetSetting->maxValue) presetSetting->set_Value(presetSetting->maxValue);
        presetSetting->set_Value(presetSetting->get_Value());
    }
}

static void RefreshAll() {
    refreshing = true;
    if (enabledToggle) enabledToggle->set_Value(Config::GetEnabled());
    if (freeHandToggle) freeHandToggle->set_Value(Config::GetUseFreeHand());
    if (gizmosToggle) gizmosToggle->set_Value(Config::GetShowGizmos());
    if (modeSetting) modeSetting->set_Value(ModeIndex(Config::GetAdjustmentMode()));
    if (buttonSetting) buttonSetting->set_Value(ButtonIndex(Config::GetAssignedButton()));
    if (trackingSetting) trackingSetting->set_Value(Config::GetTrackingSpace() == TrackingSpace::PcLink ? 0 : 1);
    if (hintText) hintText->set_text(std::string(AdjustmentModeHint(Config::GetAdjustmentMode())));
    if (controllerText) controllerText->set_text("Controller: " + std::string(ControllerTypeName(Config::GetControllerType())));

    for (Hand hand : {Hand::Left, Hand::Right}) {
        auto const& h = Config::Get(hand);
        Vec3 euler = Config::GetRotationEuler(hand);
        SetSlider(hand, PosX, h.PivotPosition.x * 100);
        SetSlider(hand, PosY, h.PivotPosition.y * 100);
        SetSlider(hand, PosZ, h.PivotPosition.z * 100);
        SetSlider(hand, RotX, euler.x);
        SetSlider(hand, RotY, euler.y);
        SetSlider(hand, RotZ, euler.z);
        SetSlider(hand, ZOff, h.ZOffset * 100);
        float curve = 0, balance = 0;
        if (h.HasReference) TransformUtils::ToReferenceSpace(h.Rotation, h.ReferenceRotation, curve, balance);
        SetSlider(hand, Curve, curve);
        SetSlider(hand, Balance, balance);
        auto& header = directHeader[hand == Hand::Left ? 0 : 1];
        if (header) {
            header->set_text(std::string(HandName(hand)) + " hand" + (h.HasReference ? "" : "  <size=75%><color=#AAAAAA>(Curve/Balance need a reference: Swing Benchmark or Auto Configure)</color></size>"));
        }
    }
    RefreshUndo();
    RefreshBenchmark();
    refreshing = false;
    needsRefresh = false;
}

#pragma endregion

#pragma region Direct mode

static std::chrono::steady_clock::time_point lastDirectEdit;

static void BeginDirectEdit(Hand hand, std::string_view what) {
    auto now = std::chrono::steady_clock::now();
    if (now - lastDirectEdit > std::chrono::milliseconds(1200)) Config::CreateUndoStep("Change " + std::string(HandName(hand)) + " " + std::string(what));
    lastDirectEdit = now;
}

static void OnSliderChanged(Hand hand, Slider s, float value) {
    if (refreshing) return;
    auto h = Config::Get(hand);
    Vec3 euler = Config::GetRotationEuler(hand);
    switch (s) {
        case PosX:
        case PosY:
        case PosZ: {
            BeginDirectEdit(hand, "Position");
            Vec3 p = h.PivotPosition;
            p[s - PosX] = value / 100.0f;
            Config::SetPivotPosition(hand, p);
            break;
        }
        case RotX:
        case RotY:
        case RotZ:
            BeginDirectEdit(hand, "Rotation");
            euler[s - RotX] = value;
            Config::SetRotation(hand, TransformUtils::RotationFromEuler(euler));
            break;
        case ZOff:
            BeginDirectEdit(hand, "Z Offset");
            Config::SetZOffset(hand, value / 100.0f);
            break;
        case Curve:
        case Balance: {
            if (!h.HasReference) break;
            BeginDirectEdit(hand, s == Curve ? "Curve" : "Balance");
            float curve, balance;
            TransformUtils::ToReferenceSpace(h.Rotation, h.ReferenceRotation, curve, balance);
            if (s == Curve) curve = value;
            else balance = value;
            Config::SetRotation(hand, TransformUtils::RotationFromEuler(TransformUtils::FromReferenceSpace(h.Rotation, h.ReferenceRotation, curve, balance)));
            break;
        }
        default:
            break;
    }
    Config::MarkDirty();
}

static void CreateDirectSliders(UnityEngine::Transform* parent, Hand hand) {
    int i = hand == Hand::Left ? 0 : 1;
    directHeader[i] = Label(parent, std::string(HandName(hand)) + " hand", 3.6f);
    struct Def {
        Slider s;
        char const* label;
        float min, max, step;
        char const* fmt;
    };
    Def defs[] = {
        {PosX, "Pivot X", -20, 20, 0.1f, "%.1f cm"}, {PosY, "Pivot Y", -20, 20, 0.1f, "%.1f cm"},  {PosZ, "Pivot Z", -25, 20, 0.1f, "%.1f cm"},
        {ZOff, "Z Offset", -20, 40, 0.1f, "%.1f cm"}, {RotX, "Rotation X", -180, 180, 0.1f, "%.1f°"}, {RotY, "Rotation Y", -180, 180, 0.1f, "%.1f°"},
        {RotZ, "Rotation Z", -180, 180, 0.1f, "%.1f°"}, {Curve, "Curve", -30, 30, 0.1f, "%.1f°"},     {Balance, "Balance", -30, 30, 0.1f, "%.1f°"},
    };
    for (auto const& d : defs) {
        Slider s = d.s;
        auto slider = CreateSliderSetting(parent, d.label, d.step, 0, d.min, d.max, 0.05f, [hand, s](float v) { OnSliderChanged(hand, s, v); });
        std::string fmt = d.fmt;
        slider->formatter = [fmt](float v) -> StringW { return Format(fmt.c_str(), v); };
        sliders[i][s] = slider;
    }
}

#pragma endregion

#pragma region Calibration modal

static void ShowModal(UnityW<BSML::ModalView> modal) {
    if (modal && viewController && viewController->get_isActivated()) modal->Show();
}

static void HideModal(UnityW<BSML::ModalView> modal) {
    if (modal) modal->Hide();
}

static void SetActive(UnityW<UnityEngine::UI::Button> b, bool active) {
    if (b) b->get_gameObject()->SetActive(active);
}

static void SyncCalibrationModal() {
    if (!calibrationModal) return;
    auto state = Calibration::GetState();
    if (calibrationStatusText) calibrationStatusText->set_text(state == Calibration::State::Idle ? Calibration::GetMessage() : "");

    switch (state) {
        case Calibration::State::Idle:
        case Calibration::State::Playing:
            HideModal(calibrationModal);
            return;
        case Calibration::State::Preparing:
            calibrationTitle->set_text("Auto Configure");
            calibrationBody->set_text(Calibration::GetMessage());
            calibrationCountdown->set_text("");
            SetActive(calibrationStart, false);
            SetActive(calibrationCancel, true);
            SetActive(calibrationOk, false);
            break;
        case Calibration::State::WaitingToStart:
            calibrationTitle->set_text("Map " + std::to_string(Calibration::GetStep() + 1) + " of 3");
            calibrationBody->set_text(Calibration::GetInstructions());
            SetActive(calibrationStart, true);
            SetActive(calibrationCancel, true);
            SetActive(calibrationOk, false);
            break;
        case Calibration::State::Done:
        case Calibration::State::Failed:
            calibrationTitle->set_text(state == Calibration::State::Done ? "Auto Configure: results" : "Auto Configure failed");
            calibrationBody->set_text(Calibration::GetMessage());
            calibrationCountdown->set_text("");
            SetActive(calibrationStart, false);
            SetActive(calibrationCancel, false);
            SetActive(calibrationOk, true);
            break;
    }
    ShowModal(calibrationModal);
}

static BSML::ModalView* MakeModal(UnityEngine::Transform* parent, Vector2 size) {
    auto modal = CreateModal(parent, Vector2(0, 0), size, nullptr, false);
    return modal;
}

static UnityEngine::UI::VerticalLayoutGroup* ModalLayout(BSML::ModalView* modal) {
    auto layout = CreateVerticalLayoutGroup(modal->get_transform());
    layout->set_padding(UnityEngine::RectOffset::New_ctor(3, 3, 3, 3));
    layout->set_spacing(1.5f);
    layout->set_childForceExpandHeight(false);
    layout->set_childControlHeight(true);
    auto fitter = layout->GetComponent<UnityEngine::UI::ContentSizeFitter*>();
    if (fitter) fitter->set_verticalFit(UnityEngine::UI::ContentSizeFitter::FitMode::Unconstrained);
    auto rect = layout->get_rectTransform();
    rect->set_anchorMin(Vector2(0, 0));
    rect->set_anchorMax(Vector2(1, 1));
    rect->set_sizeDelta(Vector2(0, 0));
    return layout;
}

static void CreateModals(UnityEngine::Transform* parent) {
    // controller picker
    pickerModal = MakeModal(parent, Vector2(80, 62));
    {
        auto layout = ModalLayout(pickerModal.ptr())->get_transform();
        Header(layout, "Auto Configure");
        Label(layout, "Which controllers are you using?\nYou will then play 3 short maps:\n1) up + down swings, right then left hand\n2) 2 seconds of inlines\n3) one diagonal slash", 3.0f);
        auto row = Row(layout)->get_transform();
        Button(row, "Quest 2", []() {
            HideModal(pickerModal);
            Calibration::Begin(ControllerType::Quest2);
        });
        Button(row, "Quest 3 / 3S", []() {
            HideModal(pickerModal);
            Calibration::Begin(ControllerType::Quest3);
        });
        Button(row, "Quest Pro", []() {
            HideModal(pickerModal);
            Calibration::Begin(ControllerType::QuestPro);
        });
        Button(layout, "Cancel", []() { HideModal(pickerModal); });
    }

    // step instructions / results
    calibrationModal = MakeModal(parent, Vector2(100, 66));
    {
        auto layout = ModalLayout(calibrationModal.ptr())->get_transform();
        calibrationTitle = Header(layout, "Auto Configure");
        calibrationBody = Label(layout, "", 3.2f);
        calibrationCountdown = Label(layout, "", 3.0f);
        auto row = Row(layout)->get_transform();
        calibrationStart = Button(row, "Start now", []() { Calibration::StartCurrentStep(); });
        calibrationCancel = Button(row, "Cancel", []() { Calibration::Cancel(); });
        calibrationOk = Button(row, "OK", []() { Calibration::Acknowledge(); });
    }
}

#pragma endregion

#pragma region Build

static void BuildUI(HMUI::ViewController* vc) {
    auto container = CreateScrollableSettingsContainer(vc->get_transform());
    auto root = container->get_transform();

    // ---- general ----
    Header(root, "EasyOffset");
    enabledToggle = CreateToggle(root, "Enabled", Config::GetEnabled(), [](bool v) {
        if (!refreshing) Config::SetEnabled(v);
    });

    // ---- auto configure ----
    Header(root, "Auto Configure");
    Label(root, "Plays 3 short calibration maps and measures your saber offsets automatically.", 3.0f);
    controllerText = Label(root, "", 3.0f);
    Button(root, "Auto Configure", []() {
        if (Calibration::GetState() == Calibration::State::Idle) ShowModal(pickerModal);
        else SyncCalibrationModal();
    });
    calibrationStatusText = Label(root, "", 3.0f);

    // ---- adjustment ----
    Header(root, "Adjustment");
    modeSetting = CreateIncrementSetting(root, "Mode", 0, 1, ModeIndex(Config::GetAdjustmentMode()), 0, (float) Modes().size() - 1, [](float v) {
        if (refreshing) return;
        Config::SetAdjustmentMode(Modes()[std::clamp((int) v, 0, (int) Modes().size() - 1)]);
        needsRefresh = true;
    });
    modeSetting->formatter = [](float v) -> StringW { return std::string(AdjustmentModeName(Modes()[std::clamp((int) v, 0, (int) Modes().size() - 1)])); };
    hintText = Label(root, "", 3.0f);

    buttonSetting = CreateIncrementSetting(root, "Button", 0, 1, ButtonIndex(Config::GetAssignedButton()), 0, (float) AvailableButtons.size() - 1, [](float v) {
        if (!refreshing) Config::SetAssignedButton(AvailableButtons[std::clamp((int) v, 0, (int) AvailableButtons.size() - 1)]);
    });
    buttonSetting->formatter = [](float v) -> StringW { return std::string(ControllerButtonLabel(AvailableButtons[std::clamp((int) v, 0, (int) AvailableButtons.size() - 1)])); };

    freeHandToggle = CreateToggle(root, "Use free hand (button adjusts the other hand)", Config::GetUseFreeHand(), [](bool v) {
        if (!refreshing) Config::SetUseFreeHand(v);
    });
    gizmosToggle = CreateToggle(root, "Show pivot / saber gizmos", Config::GetShowGizmos(), [](bool v) {
        if (!refreshing) Config::SetShowGizmos(v);
    });

    {
        auto row = Row(root)->get_transform();
        undoButton = Button(row, "Undo", []() {
            Config::Undo();
            Config::Save();
            needsRefresh = true;
        });
        redoButton = Button(row, "Redo", []() {
            Config::Redo();
            Config::Save();
            needsRefresh = true;
        });
    }

    // ---- swing benchmark ----
    Header(root, "Swing Benchmark");
    benchmarkText = Label(root, "", 3.0f);
    {
        auto row = Row(root)->get_transform();
        Button(row, "Auto Fix", []() {
            AdjustmentModes::BenchmarkAutoFix();
            needsRefresh = true;
        });
        Button(row, "Set Reference", []() {
            AdjustmentModes::BenchmarkSetAsReference();
            needsRefresh = true;
        });
        Button(row, "Clear", []() { AdjustmentModes::BenchmarkReset(); });
    }

    // ---- mirror / reset ----
    Header(root, "Mirror / Reset");
    {
        auto row = Row(root)->get_transform();
        Button(row, "Left → Right", []() {
            Config::CreateUndoStep("Mirror Left to Right");
            Config::Mirror(Hand::Left, true, true, true);
            Config::Save();
        });
        Button(row, "Right → Left", []() {
            Config::CreateUndoStep("Mirror Right to Left");
            Config::Mirror(Hand::Right, true, true, true);
            Config::Save();
        });
    }
    {
        auto row = Row(root)->get_transform();
        Button(row, "Reset Left", []() {
            Config::CreateUndoStep("Reset Left");
            Config::ResetOffsets(Hand::Left, true, true, true);
            Config::Save();
        });
        Button(row, "Reset Right", []() {
            Config::CreateUndoStep("Reset Right");
            Config::ResetOffsets(Hand::Right, true, true, true);
            Config::Save();
        });
    }

    // ---- direct ----
    Header(root, "Direct values");
    Label(root, "<color=#FF8888>For dialing in exact values. Use the modes above to find them.</color>", 3.0f);
    CreateDirectSliders(root, Hand::Left);
    CreateDirectSliders(root, Hand::Right);

    // ---- presets ----
    Header(root, "Presets");
    Label(root, "PC EasyOffset presets work too: copy them to\n" + Config::PresetsDirectory(), 2.8f);
    presetSetting = CreateIncrementSetting(root, "Preset", 0, 1, 0, 0, 0, nullptr);
    presetSetting->formatter = [](float v) -> StringW {
        int i = (int) v;
        if (presetNames.empty()) return "(none)";
        return presetNames[std::clamp(i, 0, (int) presetNames.size() - 1)];
    };
    presetStatusText = Label(root, "", 3.0f);
    {
        auto row = Row(root)->get_transform();
        auto selected = []() -> std::string {
            if (presetNames.empty() || !presetSetting) return "";
            return presetNames[std::clamp((int) presetSetting->get_Value(), 0, (int) presetNames.size() - 1)];
        };
        Button(row, "Load", [selected]() {
            auto name = selected();
            bool ok = !name.empty() && Config::LoadPresetFile(name);
            if (ok) Config::Save();
            presetStatusText->set_text(ok ? "Loaded " + name : "Could not load the preset");
            needsRefresh = true;
        });
        Button(row, "Save new", []() {
            std::time_t now = std::time(nullptr);
            char name[64];
            std::strftime(name, sizeof name, "%Y-%m-%d %H-%M-%S", std::localtime(&now));
            bool ok = Config::SavePresetFile(name);
            presetStatusText->set_text(ok ? std::string("Saved ") + name : "Could not save the preset");
            RefreshPresets();
        });
        Button(row, "Delete", [selected]() {
            auto name = selected();
            bool ok = !name.empty() && Config::DeletePresetFile(name);
            presetStatusText->set_text(ok ? "Deleted " + name : "Nothing deleted");
            RefreshPresets();
        });
        Button(row, "Refresh", []() { RefreshPresets(); });
    }

    // ---- settings ----
    Header(root, "Settings");
    trackingSetting = CreateIncrementSetting(root, "Tracking space", 0, 1, Config::GetTrackingSpace() == TrackingSpace::PcLink ? 0 : 1, 0, 1, [](float v) {
        if (refreshing) return;
        Config::SetTrackingSpace(v < 0.5f ? TrackingSpace::PcLink : TrackingSpace::QuestNative);
    });
    trackingSetting->formatter = [](float v) -> StringW { return std::string(TrackingSpaceName(v < 0.5f ? TrackingSpace::PcLink : TrackingSpace::QuestNative)); };
    Label(root, "PC (Meta Link) makes PC presets line up on Quest.", 2.8f);
    char const* axisNames[] = {"Room offset: allow X", "Room offset: allow Y", "Room offset: allow Z"};
    for (int axis = 0; axis < 3; axis++) {
        CreateToggle(root, axisNames[axis], Config::GetAllowRoomChange(axis), [axis](bool v) { Config::SetAllowRoomChange(axis, v); });
    }

    CreateModals(vc->get_transform());
}

#pragma endregion

#pragma region Lifecycle

static void DidActivate(HMUI::ViewController* vc, bool firstActivation, bool, bool) {
    EnsureInitialized();
    viewController = vc;
    if (firstActivation) BuildUI(vc);
    Config::IsPanelVisible = true;
    RefreshPresets();
    RefreshAll();
    SyncCalibrationModal();
}

static void OnTick(float deltaTime) {
    bool visible = viewController && viewController->get_isActivated();
    if (Config::IsPanelVisible != visible) {
        Config::IsPanelVisible = visible;
        if (!visible) Config::Save();
    }
    if (!visible) return;

    refreshTimer += deltaTime;
    if (needsRefresh && refreshTimer > 0.1f) {
        refreshTimer = 0;
        RefreshAll();
    }

    if (Calibration::GetState() == Calibration::State::WaitingToStart && calibrationCountdown) {
        calibrationCountdown->set_text(Format("<color=#AAAAAA>Starting in %.0f s</color>", std::max(0.0f, Calibration::GetCountdown())));
    }
}

void Register() {
    BSML::Register::RegisterMainMenu("EasyOffset", "EasyOffset", "Controller offset tool + Auto Configure", DidActivate);

    Config::ConfigWasChangedEvent.Subscribe([]() { needsRefresh = true; });
    Config::UndoListChangedEvent.Subscribe([]() { needsRefresh = true; });
    Config::AdjustmentModeChangedEvent.Subscribe([](AdjustmentMode) { needsRefresh = true; });
    Config::ControllerTypeChangedEvent.Subscribe([](ControllerType) { needsRefresh = true; });
    AdjustmentModes::BenchmarkUpdatedEvent.Subscribe([]() { needsRefresh = true; });
    Calibration::StateChangedEvent.Subscribe([]() {
        SyncCalibrationModal();
        needsRefresh = true;
    });
    Tracking::TickEvent.Subscribe(OnTick);
}

#pragma endregion
}
