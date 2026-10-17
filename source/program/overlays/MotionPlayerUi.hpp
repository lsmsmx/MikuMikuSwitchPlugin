#pragma once

namespace MotionPlayerUi {
    extern bool g_showUltimatePlayer;
    extern bool g_showMotionControlWindow;

    void Init();
    void ResetPtrCounter();
    void DrawRecorderHeader();
    void DrawMotionControlWindow();
    void DrawUltimatePlayerWindow();
}
