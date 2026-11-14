#include "CalibrationMaps.hpp"

#include <cstdio>

namespace EasyOffset::CalibrationMaps {

static std::string Escape(std::string const& s) {
    std::string r;
    for (char c : s) {
        if (c == '"' || c == '\\') r += '\\';
        if (c == '\n') {
            r += "\\n";
            continue;
        }
        r += c;
    }
    return r;
}

std::string InfoDat(MapDefinition const& map) {
    char buffer[2048];
    std::snprintf(buffer, sizeof buffer,
                  R"({
  "_version": "2.1.0",
  "_songName": "%s",
  "_songSubName": "%s",
  "_songAuthorName": "EasyOffset",
  "_levelAuthorName": "EasyOffset Auto Configure",
  "_beatsPerMinute": %.1f,
  "_shuffle": 0,
  "_shufflePeriod": 0.5,
  "_previewStartTime": 0,
  "_previewDuration": 3,
  "_songFilename": "song.ogg",
  "_coverImageFilename": "cover.png",
  "_environmentName": "DefaultEnvironment",
  "_allDirectionsEnvironmentName": "GlassDesertEnvironment",
  "_songTimeOffset": 0,
  "_customData": { "_contributors": [] },
  "_difficultyBeatmapSets": [
    {
      "_beatmapCharacteristicName": "Standard",
      "_difficultyBeatmaps": [
        {
          "_difficulty": "Easy",
          "_difficultyRank": 1,
          "_beatmapFilename": "Easy.dat",
          "_noteJumpMovementSpeed": 10,
          "_noteJumpStartBeatOffset": 0.5
        }
      ]
    }
  ]
}
)",
                  Escape(map.SongName).c_str(), Escape(map.SubName).c_str(), Bpm);
    return buffer;
}

std::string DifficultyDat(MapDefinition const& map) {
    std::string notes;
    char buffer[256];
    for (size_t i = 0; i < map.Notes.size(); i++) {
        auto const& n = map.Notes[i];
        float beat = n.Time * Bpm / 60.0f;
        std::snprintf(buffer, sizeof buffer, R"(    {"_time": %.4f, "_lineIndex": %d, "_lineLayer": %d, "_type": %d, "_cutDirection": %d}%s)", beat, n.Line, n.Layer,
                      n.NoteHand == Hand::Left ? 0 : 1, static_cast<int>(n.Direction), i + 1 < map.Notes.size() ? ",\n" : "\n");
        notes += buffer;
    }
    return "{\n  \"_version\": \"2.6.0\",\n  \"_notes\": [\n" + notes + "  ],\n  \"_obstacles\": [],\n  \"_events\": [],\n  \"_waypoints\": []\n}\n";
}
}
