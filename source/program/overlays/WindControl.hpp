#pragma once

#include <cstdint>

struct WindSpectrum {
    float cos;
    float sin;
};

// Layout matching game engine Wind structure (FUN_00523140 / 0x00523140)
struct Wind {
    float scale;               // 0x00
    float cycle;               // 0x04
    float yrot;                // 0x08 (Yaw / heading in degrees)
    float zrot;                // 0x0C (Pitch / elevation in degrees)
    float bias;                // 0x10
    uint32_t pad14;            // 0x14
    uint64_t count;            // 0x18
    WindSpectrum spectrum[16]; // 0x20 .. 0x9F
    float val[3];              // 0xA0 (X, Y, Z vector output)
    float frame;               // 0xAC
    float strength;            // 0xB0
};

namespace WindControl {
    extern bool g_showWindow;
    extern bool g_overrideWind;

    void Init();
    void DrawWindow();
}