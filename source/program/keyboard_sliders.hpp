#pragma once
#include <stdint.h>

namespace nn::hid { struct NpadHandheldState; }

namespace keyboard_sliders
{
    void Poll();
    void init();
    void Inject(nn::hid::NpadHandheldState* state);

    bool IsDown(int32_t key);
    bool IsTapped(int32_t key);
    bool IsRepeat(int32_t key);

    float GetLeftStickX();
    float GetRightStickX();

    bool IsRawKeyDown(int32_t button);
    int32_t GetGameButtonsMask(int32_t key_index);
    int32_t GetGameButtonsTappedMask(int32_t key_index);
}
