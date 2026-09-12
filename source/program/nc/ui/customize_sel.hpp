#pragma once

#include <stdint.h>

namespace CustomizeSelUi
{
    enum class AnimState
    {
        Closed,
        Opening,
        Open,
        Closing
    };

    bool IsOpen();
    void Open();
    void Close();
    void ForceClose();
    void Toggle();

    // Installs the game lifecycle hooks (called during mod initialization)
    void Init();

    // Main control loop (called strictly inside CSTopMenuMainCtrl)
    void Update();

    // ImGui drawing routine (called inside nvnImguiCalc)
    void Draw();
}
