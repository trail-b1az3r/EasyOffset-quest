#pragma once

#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace EasyOffset {

enum class Hand { Left, Right };
inline Hand OtherHand(Hand h) { return h == Hand::Left ? Hand::Right : Hand::Left; }
inline std::string_view HandName(Hand hand) { return hand == Hand::Left ? "Left" : "Right"; }

enum class AdjustmentMode { None, Basic, Position, Rotation, SwingBenchmark, Direct, PositionAuto, RotationAuto, RoomOffset };

inline constexpr std::array AllAdjustmentModes{
    AdjustmentMode::None, AdjustmentMode::Basic, AdjustmentMode::Position, AdjustmentMode::Rotation, AdjustmentMode::SwingBenchmark,
    AdjustmentMode::Direct, AdjustmentMode::PositionAuto, AdjustmentMode::RotationAuto, AdjustmentMode::RoomOffset,
};

inline std::string_view AdjustmentModeName(AdjustmentMode mode) {
    switch (mode) {
        case AdjustmentMode::None: return "None";
        case AdjustmentMode::Basic: return "Basic";
        case AdjustmentMode::Position: return "Position";
        case AdjustmentMode::Rotation: return "Rotation";
        case AdjustmentMode::SwingBenchmark: return "Swing Benchmark";
        case AdjustmentMode::Direct: return "Direct";
        case AdjustmentMode::PositionAuto: return "Position Auto";
        case AdjustmentMode::RotationAuto: return "Rotation Auto";
        case AdjustmentMode::RoomOffset: return "Room Offset";
    }
    return "None";
}

inline std::string_view AdjustmentModeHint(AdjustmentMode mode) {
    switch (mode) {
        case AdjustmentMode::None: return "Pick an adjustment mode, or use Auto Configure";
        case AdjustmentMode::Basic: return "Drag and drop: hold the button and move your hand";
        case AdjustmentMode::Position: return "Pivot position only: hold the button and move your hand";
        case AdjustmentMode::Rotation: return "Rotation only: hold the button and move your hand.\nRaise your free hand for finer control";
        case AdjustmentMode::SwingBenchmark: return "Hold the button and swing like you do in game,\nthen use Auto Fix / Set Reference";
        case AdjustmentMode::Direct: return "<color=#FF6666>For dialing in exact values only</color>";
        case AdjustmentMode::PositionAuto: return "Hold the button and rotate your wrist randomly";
        case AdjustmentMode::RotationAuto: return "Hold the button and twist your wrist\nwhile pointing in one direction";
        case AdjustmentMode::RoomOffset: return "World pulling: hold the button and move your hand";
    }
    return "";
}

// Quest controllers have the same button layout (A/B/X/Y, grip, trigger, stick)
enum class ControllerButton { TriggerButton, PrimaryButton, SecondaryButton, GripButton, Primary2DAxisClick };

inline constexpr std::array AvailableButtons{
    ControllerButton::GripButton, ControllerButton::PrimaryButton, ControllerButton::SecondaryButton, ControllerButton::Primary2DAxisClick,
};

// Same names as the PC config file
inline std::string_view ControllerButtonName(ControllerButton button) {
    switch (button) {
        case ControllerButton::TriggerButton: return "TriggerButton";
        case ControllerButton::PrimaryButton: return "PrimaryButton";
        case ControllerButton::SecondaryButton: return "SecondaryButton";
        case ControllerButton::GripButton: return "GripButton";
        case ControllerButton::Primary2DAxisClick: return "Primary2DAxisClick";
    }
    return "SecondaryButton";
}

inline std::string_view ControllerButtonLabel(ControllerButton button) {
    switch (button) {
        case ControllerButton::TriggerButton: return "Trigger";
        case ControllerButton::PrimaryButton: return "A / X";
        case ControllerButton::SecondaryButton: return "B / Y";
        case ControllerButton::GripButton: return "Grip";
        case ControllerButton::Primary2DAxisClick: return "Stick click";
    }
    return "";
}

inline ControllerButton ControllerButtonFromName(std::string_view name) {
    for (auto button : AvailableButtons)
        if (ControllerButtonName(button) == name || ControllerButtonLabel(button) == name) return button;
    return ControllerButton::SecondaryButton;
}

// Quest controller families. Names of the first entries match PC EasyOffset presets.
enum class ControllerType { None, Quest2, Quest3, QuestPro, Quest1 };

inline constexpr std::array AllControllerTypes{ControllerType::Quest2, ControllerType::Quest3, ControllerType::QuestPro, ControllerType::Quest1};

inline std::string_view ControllerTypeName(ControllerType type) {
    switch (type) {
        case ControllerType::None: return "None";
        case ControllerType::Quest2: return "Quest 2";
        case ControllerType::Quest3: return "Quest 3/3S";
        case ControllerType::QuestPro: return "Quest Pro";
        case ControllerType::Quest1: return "RiftS/Quest";
    }
    return "None";
}

inline ControllerType ControllerTypeFromName(std::string_view name) {
    for (auto t : AllControllerTypes)
        if (ControllerTypeName(t) == name) return t;
    if (name == "Quest 3" || name == "Quest 3S") return ControllerType::Quest3;
    return ControllerType::None;
}

enum class TrackingSpace { PcLink, QuestNative };

inline std::string_view TrackingSpaceName(TrackingSpace s) { return s == TrackingSpace::PcLink ? "PC (Meta Link)" : "Quest native"; }

// Minimal multicast event (C# `event Action<...>`)
template <typename... Args>
class Event {
public:
    using Callback = std::function<void(Args...)>;

    int Subscribe(Callback callback) {
        int id = nextId++;
        callbacks.emplace_back(id, std::move(callback));
        return id;
    }
    void Unsubscribe(int id) {
        std::erase_if(callbacks, [id](auto const& e) { return e.first == id; });
    }
    void Invoke(Args... args) {
        auto copy = callbacks;
        for (auto& [id, cb] : copy) cb(args...);
    }

private:
    int nextId = 1;
    std::vector<std::pair<int, Callback>> callbacks;
};
}
