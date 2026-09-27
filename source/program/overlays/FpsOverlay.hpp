#pragma once

#include <cstdint>

namespace FpsOverlay {
    extern bool g_showWindow;

    float GetFramerate();
    void DrawWindow();
}
