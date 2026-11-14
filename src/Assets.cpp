#include "Assets.hpp"

#ifndef ASSETS_DIR
#error "ASSETS_DIR must be defined by CMake"
#endif

#define EO_INCBIN(name, file)                                \
    asm(".section .rodata\n"                                 \
        ".balign 16\n"                                       \
        ".hidden " #name "_start\n"                          \
        ".global " #name "_start\n" #name "_start:\n"        \
        ".incbin \"" ASSETS_DIR "/" file "\"\n"              \
        ".hidden " #name "_end\n"                            \
        ".global " #name "_end\n" #name "_end:\n"            \
        ".byte 0\n"                                          \
        ".previous\n");                                      \
    extern "C" uint8_t const name##_start[];                 \
    extern "C" uint8_t const name##_end[];

EO_INCBIN(eo_song1, "calibration1.ogg")
EO_INCBIN(eo_song2, "calibration2.ogg")
EO_INCBIN(eo_song3, "calibration3.ogg")
EO_INCBIN(eo_cover, "../cover.png")

namespace EasyOffset::Assets {
static std::span<uint8_t const> Span(uint8_t const* start, uint8_t const* end) { return {start, static_cast<size_t>(end - start)}; }

std::span<uint8_t const> CalibrationSong(int step) {
    switch (step) {
        case 0: return Span(eo_song1_start, eo_song1_end);
        case 1: return Span(eo_song2_start, eo_song2_end);
        default: return Span(eo_song3_start, eo_song3_end);
    }
}

std::span<uint8_t const> Cover() { return Span(eo_cover_start, eo_cover_end); }
}
