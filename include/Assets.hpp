#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

// Binary files from /assets embedded into the .so (see src/Assets.cpp)
namespace EasyOffset::Assets {
std::span<uint8_t const> CalibrationSong(int step);
std::span<uint8_t const> Cover();
}
