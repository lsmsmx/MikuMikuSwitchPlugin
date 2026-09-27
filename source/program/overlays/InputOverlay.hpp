#pragma once
#include <cstdint>

namespace InputOverlay {
    enum Mode {
        Mode_Disabled = 0,
        Mode_Gamepad  = 1,
        Mode_Keyboard = 2
    };

    int GetMode();
    void SetMode(int mode);
    bool IsVisible();
    void SetVisible(bool state); // Backward compatibility

    void Draw();
    void DrawGamepad();
    void DrawKeyboard();
}
