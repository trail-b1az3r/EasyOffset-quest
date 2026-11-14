#include "Calibration.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "Assets.hpp"
#include "CalibrationMaps.hpp"
#include "CalibrationSolver.hpp"
#include "Config.hpp"
#include "Tracking.hpp"
#include "TransformUtils.hpp"
#include "main.hpp"

#include "bsml/shared/BSML/MainThreadScheduler.hpp"
#include "custom-types/shared/delegate.hpp"
#include "songcore/shared/SongCore.hpp"

#include "GlobalNamespace/AudioTimeSyncController.hpp"
#include "GlobalNamespace/BeatmapCharacteristicSO.hpp"
#include "GlobalNamespace/BeatmapDifficulty.hpp"
#include "GlobalNamespace/BeatmapKey.hpp"
#include "GlobalNamespace/BeatmapLevel.hpp"
#include "GlobalNamespace/ColorScheme.hpp"
#include "GlobalNamespace/ColorSchemesSettings.hpp"
#include "GlobalNamespace/EnvironmentsListModel.hpp"
#include "GlobalNamespace/GameplayModifiers.hpp"
#include "GlobalNamespace/LevelCompletionResults.hpp"
#include "GlobalNamespace/MenuTransitionsHelper.hpp"
#include "GlobalNamespace/OverrideEnvironmentSettings.hpp"
#include "GlobalNamespace/PlayerData.hpp"
#include "GlobalNamespace/PlayerDataFileModel.hpp"
#include "GlobalNamespace/PlayerDataModel.hpp"
#include "GlobalNamespace/PlayerSpecificSettings.hpp"
#include "GlobalNamespace/RecordingToolManager.hpp"
#include "GlobalNamespace/StandardLevelScenesTransitionSetupDataSO.hpp"
#include "System/Action.hpp"
#include "System/Action_1.hpp"
#include "System/Action_2.hpp"
#include "System/Nullable_1.hpp"
#include "UnityEngine/Resources.hpp"
#include "Zenject/DiContainer.hpp"

namespace fs = std::filesystem;
using namespace GlobalNamespace;

namespace EasyOffset::Calibration {

static State state = State::Idle;
static int step = 0;
static float countdown = 0;
static std::string message;
static ControllerType selectedType = ControllerType::None;
static Config::HandOffsets before[2];
static std::array<CalibrationSolver::Recording, 3> recordings;
static UnityW<AudioTimeSyncController> audioTimeSync;
static int searchCooldown = 0;
static float fallbackTime = 0;

Event<> StateChangedEvent;

static constexpr float CountdownSeconds = 8.0f;

State GetState() { return state; }
int GetStep() { return step; }
float GetCountdown() { return countdown; }
std::string GetMessage() { return message; }

std::string GetInstructions() {
    if (step < 0 || step >= CalibrationMaps::StepCount) return "";
    return CalibrationMaps::Maps()[step].Instructions;
}

static void SetState(State value, std::string text = "") {
    state = value;
    message = std::move(text);
    StateChangedEvent.Invoke();
}

static void Fail(std::string const& reason) {
    Logger.error("Auto Configure failed: {}", reason);
    Config::SetHand(Hand::Left, before[0]);
    Config::SetHand(Hand::Right, before[1]);
    Config::Save();
    Tracking::ForceInput = false;
    SetState(State::Failed, reason + "\nYour previous offsets were restored.");
}

#pragma region Map files

static fs::path LevelFolder(int index) { return SongCore::API::Loading::GetPreferredCustomLevelPath() / CalibrationMaps::Maps()[index].FolderName; }

static bool WriteIfDifferent(fs::path const& path, std::string_view content) {
    {
        std::ifstream in(path, std::ios::binary);
        if (in) {
            std::stringstream ss;
            ss << in.rdbuf();
            if (ss.str() == content) return false;
        }
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(content.data(), (std::streamsize) content.size());
    return true;
}

// Returns true when a file changed (SongCore needs a refresh)
static bool WriteMaps() {
    bool changed = false;
    for (int i = 0; i < CalibrationMaps::StepCount; i++) {
        auto const& map = CalibrationMaps::Maps()[i];
        auto folder = LevelFolder(i);
        std::error_code ec;
        fs::create_directories(folder, ec);
        auto song = Assets::CalibrationSong(i);
        auto cover = Assets::Cover();
        changed |= WriteIfDifferent(folder / "Info.dat", CalibrationMaps::InfoDat(map));
        changed |= WriteIfDifferent(folder / "Easy.dat", CalibrationMaps::DifficultyDat(map));
        changed |= WriteIfDifferent(folder / "song.ogg", std::string_view(reinterpret_cast<char const*>(song.data()), song.size()));
        changed |= WriteIfDifferent(folder / "cover.png", std::string_view(reinterpret_cast<char const*>(cover.data()), cover.size()));
    }
    return changed;
}

static BeatmapLevel* FindLevel(int index) {
    if (auto level = SongCore::API::Loading::GetLevelByPath(LevelFolder(index))) return level;
    // fallback: SongCore may store the path in another form
    std::string name = CalibrationMaps::Maps()[index].SongName;
    for (auto level : SongCore::API::Loading::GetAllLevels()) {
        if (level && level->songName && std::string(level->songName) == name) return level;
    }
    return nullptr;
}

static bool AllLevelsLoaded() {
    for (int i = 0; i < CalibrationMaps::StepCount; i++)
        if (!FindLevel(i)) return false;
    return true;
}

#pragma endregion

#pragma region Level launching

static void OnLevelFinished(UnityW<StandardLevelScenesTransitionSetupDataSO>, LevelCompletionResults* results);
static void OnLevelRestarted(UnityW<StandardLevelScenesTransitionSetupDataSO>, LevelCompletionResults*);

void StartCurrentStep() {
    if (state != State::WaitingToStart) return;

    auto level = FindLevel(step);
    auto helpers = UnityEngine::Resources::FindObjectsOfTypeAll<MenuTransitionsHelper*>();
    auto dataModels = UnityEngine::Resources::FindObjectsOfTypeAll<PlayerDataModel*>();
    auto characteristic = SongCore::API::Characteristics::GetCharacteristicBySerializedName("Standard");
    if (!level || helpers.size() == 0 || dataModels.size() == 0 || !characteristic) {
        Fail("Could not start the calibration map (level, menu or player data missing).");
        return;
    }
    auto helper = helpers[0];
    auto playerData = dataModels[0]->get_playerData();
    auto environments = dataModels[0]->get_playerDataFileModel()->_environmentsListModel;

    // No Fail and no score-changing modifiers: this is a measurement, not a score
    using System::Nullable_1;
    auto modifiers = playerData->get_gameplayModifiers()->CopyWith(
        Nullable_1<GameplayModifiers_EnergyType>(true, GameplayModifiers_EnergyType::Bar), Nullable_1<bool>(true, true), Nullable_1<bool>(true, false),
        Nullable_1<bool>(true, false), Nullable_1<GameplayModifiers_EnabledObstacleType>(true, GameplayModifiers_EnabledObstacleType::All),
        Nullable_1<bool>(true, false), Nullable_1<bool>(true, false), Nullable_1<bool>(true, false), Nullable_1<bool>(true, false),
        Nullable_1<GameplayModifiers_SongSpeed>(true, GameplayModifiers_SongSpeed::Normal), Nullable_1<bool>(true, false), Nullable_1<bool>(true, false),
        Nullable_1<bool>(true, false), Nullable_1<bool>(true, false), Nullable_1<bool>(true, false));

    BeatmapKey key(characteristic, BeatmapDifficulty::Easy, level->levelID);

    recordings[step].clear();
    audioTimeSync = nullptr;
    searchCooldown = 0;
    fallbackTime = 0;
    SetState(State::Playing);

    auto afterSwitch = custom_types::MakeDelegate<System::Action_1<Zenject::DiContainer*>*>(std::function<void(Zenject::DiContainer*)>([](Zenject::DiContainer* container) {
        if (container) audioTimeSync = container->TryResolve<AudioTimeSyncController*>();
    }));
    auto finished = custom_types::MakeDelegate<System::Action_2<UnityW<StandardLevelScenesTransitionSetupDataSO>, LevelCompletionResults*>*>(
        std::function<void(UnityW<StandardLevelScenesTransitionSetupDataSO>, LevelCompletionResults*)>(OnLevelFinished));
    auto restarted = custom_types::MakeDelegate<System::Action_2<UnityW<StandardLevelScenesTransitionSetupDataSO>, LevelCompletionResults*>*>(
        std::function<void(UnityW<StandardLevelScenesTransitionSetupDataSO>, LevelCompletionResults*)>(OnLevelRestarted));

    Logger.info("Auto Configure: starting map {}", step + 1);
    auto colorSchemes = playerData->get_colorSchemesSettings();
    helper->StartStandardLevel("Solo", byref(key), level, playerData->get_overrideEnvironmentSettings(), colorSchemes ? colorSchemes->GetOverrideColorScheme() : nullptr,
                               false, nullptr, modifiers, playerData->get_playerSpecificSettings(), nullptr, environments, "Menu", false, false, nullptr, afterSwitch,
                               finished, restarted, System::Nullable_1<RecordingToolManager_SetupData>());
}

static std::string DescribeResult(Hand hand, CalibrationSolver::HandResult const& r) {
    char buffer[512];
    std::string rotation = r.RotationMeasured ? (r.UsedTwoPlanes ? "measured" : "measured (one plane)") : "kept";
    std::snprintf(buffer, sizeof buffer, "<b>%s</b>: rotation %s (%.1f deg change), pivot %s (%.1f cm change)", std::string(HandName(hand)).c_str(), rotation.c_str(),
                  r.RotationChangeDeg, r.PivotMeasured ? "measured" : "default", r.PivotChangeCm);
    std::string text = buffer;
    if (!r.Notes.empty()) text += "\n<size=80%><color=#FFCC66>" + r.Notes + "</color></size>";
    return text;
}

static void Solve() {
    std::string text = "<color=#66FF66>Auto Configure finished!</color>\n";
    CalibrationSolver::HandResult results[2];
    for (int h = 0; h < 2; h++) {
        Hand hand = h == 0 ? Hand::Left : Hand::Right;
        auto prior = Config::DefaultOffsets(selectedType, hand);
        results[h] = CalibrationSolver::SolveHand(hand, recordings, prior, before[h]);
        Logger.info("Auto Configure {}: rot={} twoPlanes={} planeAngle={:.1f} qV={:.2f} qD={:.2f} pivot={} samples={} notes={}", HandName(hand), results[h].RotationMeasured,
                    results[h].UsedTwoPlanes, results[h].PlaneAngleDeg, results[h].VerticalQuality, results[h].DiagonalQuality, results[h].PivotMeasured,
                    results[h].SwingSamples, results[h].Notes);
    }

    bool anything = results[0].RotationMeasured || results[1].RotationMeasured;
    if (!anything) {
        Fail("No clean swings were detected in the calibration maps.");
        return;
    }

    for (int h = 0; h < 2; h++) {
        Hand hand = h == 0 ? Hand::Left : Hand::Right;
        auto offsets = results[h].Offsets;
        offsets.ZOffset = before[h].ZOffset;  // saber length preference is kept
        Config::SetHand(hand, offsets);
        text += DescribeResult(hand, results[h]) + "\n";
    }
    Config::Save();
    Tracking::ForceInput = false;
    text += "Use Undo to go back. Swing Benchmark / Direct mode can fine-tune.";
    SetState(State::Done, text);
}

static void OnLevelFinished(UnityW<StandardLevelScenesTransitionSetupDataSO>, LevelCompletionResults* results) {
    audioTimeSync = nullptr;
    if (state != State::Playing) return;
    auto action = results ? results->levelEndAction : LevelCompletionResults_LevelEndAction::None;
    if (action == LevelCompletionResults_LevelEndAction::Restart) {
        recordings[step].clear();
        return;
    }
    if (action == LevelCompletionResults_LevelEndAction::Quit) {
        Fail("Calibration map was quit.");
        return;
    }
    Logger.info("Auto Configure: map {} finished with {} samples", step + 1, recordings[step].size());
    if (recordings[step].size() < 60) {
        Fail("Not enough tracking data was recorded.");
        return;
    }

    step++;
    if (step >= CalibrationMaps::StepCount) {
        Solve();
        return;
    }
    countdown = CountdownSeconds;
    SetState(State::WaitingToStart);
}

static void OnLevelRestarted(UnityW<StandardLevelScenesTransitionSetupDataSO>, LevelCompletionResults*) {
    recordings[step].clear();
    audioTimeSync = nullptr;
    fallbackTime = 0;
}

#pragma endregion

#pragma region Recording

static void Record(float deltaTime) {
    if (!audioTimeSync && --searchCooldown <= 0) {
        searchCooldown = 30;
        auto found = UnityEngine::Resources::FindObjectsOfTypeAll<AudioTimeSyncController*>();
        if (found.size() > 0) audioTimeSync = found[0];
    }
    if (!audioTimeSync) return;  // still loading / in the menu

    fallbackTime += deltaTime;
    CalibrationSolver::Sample s;
    s.Time = audioTimeSync->get_songTime();
    s.DeltaTime = deltaTime;
    s.Head = Tracking::HeadRotation();
    for (int h = 0; h < 2; h++) {
        Hand hand = h == 0 ? Hand::Left : Hand::Right;
        s.Valid[h] = Tracking::GetRawPose(hand, s.Position[h], s.Rotation[h]);
    }
    auto& rec = recordings[step];
    if (!rec.empty() && s.Time < rec.back().Time - 0.5f) rec.clear();  // song restarted
    if (!rec.empty() && s.Time <= rec.back().Time) return;            // paused or duplicate frame
    if (!rec.empty()) s.DeltaTime = s.Time - rec.back().Time;
    rec.push_back(s);
}

static void OnTick(float deltaTime) {
    switch (state) {
        case State::Playing:
            Record(deltaTime);
            break;
        case State::WaitingToStart:
            countdown -= deltaTime;
            if (countdown <= 0) StartCurrentStep();
            break;
        default:
            break;
    }
}

#pragma endregion

void Initialize() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;
    Tracking::TickEvent.Subscribe(OnTick);
}

void Begin(ControllerType type) {
    if (state == State::Preparing || state == State::Playing || state == State::WaitingToStart) return;

    selectedType = type;
    Config::SetControllerType(type);
    before[0] = Config::Get(Hand::Left);
    before[1] = Config::Get(Hand::Right);
    Config::CreateUndoStep("Auto Configure");

    // play the maps with the controller defaults so the sabers feel reasonable
    for (Hand hand : {Hand::Left, Hand::Right}) {
        auto defaults = Config::DefaultOffsets(type, hand);
        defaults.ZOffset = before[hand == Hand::Left ? 0 : 1].ZOffset;
        Config::SetHand(hand, defaults);
    }

    step = 0;
    for (auto& r : recordings) r.clear();
    SetState(State::Preparing, "Preparing calibration maps...");

    bool changed = false;
    try {
        changed = WriteMaps();
    } catch (std::exception const& e) {
        Fail(std::string("Could not write the calibration maps: ") + e.what());
        return;
    }

    auto ready = []() {
        if (state != State::Preparing) return;
        if (!AllLevelsLoaded()) {
            Fail("SongCore did not load the calibration maps.");
            return;
        }
        countdown = CountdownSeconds;
        SetState(State::WaitingToStart);
    };

    if (!changed && AllLevelsLoaded()) {
        ready();
        return;
    }
    SongCore::API::Loading::RefreshSongs(false);
    BSML::MainThreadScheduler::ScheduleUntil([]() { return !SongCore::API::Loading::AreSongsRefreshing() && SongCore::API::Loading::AreSongsLoaded(); }, ready);
}

void Cancel() {
    if (state == State::Idle || state == State::Done || state == State::Failed) return;
    Config::SetHand(Hand::Left, before[0]);
    Config::SetHand(Hand::Right, before[1]);
    Config::Save();
    Tracking::ForceInput = false;
    SetState(State::Idle, "Auto Configure cancelled, previous offsets restored.");
}

void Acknowledge() {
    if (state == State::Done || state == State::Failed) SetState(State::Idle);
}
}
