#pragma once

#include <string>

#include "Enums.hpp"

// Auto Configure: plays the three generated calibration maps back to back,
// records the raw controller motion and solves the offsets (see CalibrationSolver).
namespace EasyOffset::Calibration {

enum class State {
    Idle,
    Preparing,        // writing maps / waiting for SongCore
    WaitingToStart,   // instructions shown, countdown running
    Playing,          // inside a calibration map
    Done,             // results applied
    Failed,
};

State GetState();
int GetStep();  // 0..2
float GetCountdown();
std::string GetInstructions();  // for the current step
std::string GetMessage();       // status / results / error

extern Event<> StateChangedEvent;

void Initialize();
void Begin(ControllerType type);
void StartCurrentStep();
void Cancel();       // restores the offsets from before Auto Configure
void Acknowledge();  // closes the Done / Failed message
}
