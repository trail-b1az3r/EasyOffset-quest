#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "Enums.hpp"

// The three generated calibration levels. BPM is 60, so beats == seconds.
namespace EasyOffset::CalibrationMaps {

constexpr int StepCount = 3;
constexpr float Bpm = 60.0f;

// Beat Saber v2 cut directions
enum class Cut { Up = 0, Down = 1, Left = 2, Right = 3, UpLeft = 4, UpRight = 5, DownLeft = 6, DownRight = 7, Any = 8 };

struct Note {
    float Time;  // seconds
    Hand NoteHand;
    int Line;   // 0..3
    int Layer;  // 0..2
    Cut Direction;
};

struct MapDefinition {
    std::string FolderName;
    std::string SongName;
    std::string SubName;
    std::string Instructions;
    float Length;  // seconds (length of the audio track)
    std::vector<Note> Notes;
};

inline std::vector<MapDefinition> const& Maps() {
    static std::vector<MapDefinition> const maps = [] {
        std::vector<MapDefinition> result;

        // 1: one up then down swing with the right hand, then the same with the left hand
        result.push_back({
            "EasyOffset Calibration 1",
            "EasyOffset Calibration 1/3",
            "Up + down, right then left",
            "RIGHT hand: swing UP, then DOWN.\nThen LEFT hand: swing UP, then DOWN.\nUse full, natural swings.",
            11.0f,
            {
                {3.0f, Hand::Right, 2, 1, Cut::Up},
                {4.5f, Hand::Right, 2, 1, Cut::Down},
                {6.5f, Hand::Left, 1, 1, Cut::Up},
                {8.0f, Hand::Left, 1, 1, Cut::Down},
            },
        });

        // 2: two seconds of inlines (both hands, same spot, alternating down/up)
        {
            MapDefinition map{
                "EasyOffset Calibration 2",
                "EasyOffset Calibration 2/3",
                "2 seconds of inlines",
                "BOTH hands: fast inline down/up swings\nfor two seconds. Keep the notes in line\nwith your saber and swing from the wrist.",
                8.0f,
                {},
            };
            float const start = 3.0f, end = 5.0f, step = 1.0f / 3.0f;
            int i = 0;
            for (float t = start; t <= end + 0.001f; t += step, i++) {
                Cut cut = (i % 2 == 0) ? Cut::Down : Cut::Up;
                map.Notes.push_back({t, Hand::Left, 1, 1, cut});
                map.Notes.push_back({t, Hand::Right, 2, 1, cut});
            }
            result.push_back(std::move(map));
        }

        // 3: a single diagonal slash (both hands, outward diagonals)
        result.push_back({
            "EasyOffset Calibration 3",
            "EasyOffset Calibration 3/3",
            "Single diagonal slash",
            "BOTH hands: one big diagonal slash\ndown and outwards.",
            7.0f,
            {
                {3.0f, Hand::Left, 0, 1, Cut::DownLeft},
                {3.0f, Hand::Right, 3, 1, Cut::DownRight},
            },
        });
        return result;
    }();
    return maps;
}

// Song-time window that contains the swings of one hand in one map
inline bool GetWindow(int step, Hand hand, float& from, float& to) {
    auto const& notes = Maps()[step].Notes;
    bool found = false;
    for (auto const& n : notes) {
        if (n.NoteHand != hand) continue;
        if (!found) {
            from = to = n.Time;
            found = true;
        }
        from = std::min(from, n.Time);
        to = std::max(to, n.Time);
    }
    if (!found) return false;
    from -= 0.8f;
    to += 0.5f;
    return true;
}

std::string InfoDat(MapDefinition const& map);
std::string DifficultyDat(MapDefinition const& map);
}
