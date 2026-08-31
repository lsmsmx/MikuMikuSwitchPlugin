#include <stdint.h>
#include <array>
#include "lib.hpp"
#include "keyboard.hpp"
#include "hid.hpp"
#include "Config.hpp"

static std::array<bool, 256> s_keys = {};

void keyboard::Poll() {
    if (!Config::enableKeyboard || !nn::hid::GetKeyboardState) return;

    nn::hid::KeyboardState ks = {};
    nn::hid::GetKeyboardState(&ks);

    for (int i = 0; i < 256; i++) {
        s_keys[i] = (ks.keys[i / 64] & (1ULL << (i % 64))) != 0;
    }
}

bool keyboard::IsDown(int32_t key) { return s_keys[key]; }
bool keyboard::IsTapped(int32_t key) { return s_keys[key]; }
bool keyboard::IsRepeat(int32_t key) { return s_keys[key]; }

float keyboard::GetLeftStickX() {
    if (IsDown(20)) return -1.0f; // Q
    if (IsDown(8))  return  1.0f; // E
    return 0.0f;
}

float keyboard::GetRightStickX() {
    if (IsDown(24)) return -1.0f; // U
    if (IsDown(18)) return  1.0f; // O
    return 0.0f;
}

bool keyboard::IsRawKeyDown(int32_t button) { return false; }
int32_t keyboard::GetGameButtonsMask(int32_t key_index) { return 0; }
int32_t keyboard::GetGameButtonsTappedMask(int32_t key_index) { return 0; }

// Unified keyboard injection point
void keyboard::Inject(nn::hid::NpadHandheldState* state) {
    if (!Config::enableKeyboard || !state) return;

    keyboard::Poll();

    uint64_t buttons = 0;

    // 1. Arcade Notes (I, J, K, L)
    if (keyboard::IsDown(12)) buttons |= nn::hid::Button::X; // I -> Triangle
    if (keyboard::IsDown(13)) buttons |= nn::hid::Button::Y; // J -> Square
    if (keyboard::IsDown(14)) buttons |= nn::hid::Button::B; // K -> Cross
    if (keyboard::IsDown(15)) buttons |= nn::hid::Button::A; // L -> Circle

    // 2. D-Pad / Arrows (WASD + Arrow Keys)
    if (keyboard::IsDown(26) || keyboard::IsDown(82)) buttons |= nn::hid::Button::Up;
    if (keyboard::IsDown(4)  || keyboard::IsDown(80)) buttons |= nn::hid::Button::Left;
    if (keyboard::IsDown(22) || keyboard::IsDown(81)) buttons |= nn::hid::Button::Down;
    if (keyboard::IsDown(7)  || keyboard::IsDown(79)) buttons |= nn::hid::Button::Right;

    // 3. Triggers L / R (Shifts + X + M)
    if (keyboard::IsDown(225) || keyboard::IsDown(27)) buttons |= nn::hid::Button::L;
    if (keyboard::IsDown(229) || keyboard::IsDown(16)) buttons |= nn::hid::Button::R;

    // 4. Triggers ZL (LeftCtrl / Z / CapsLock)
    if (keyboard::IsDown(224) || keyboard::IsDown(29) || keyboard::IsDown(57)) {
        buttons |= nn::hid::Button::ZL;
    }

    // 5. Triggers ZR (RightCtrl / Comma / Space / Semicolon)
    if (keyboard::IsDown(228) || keyboard::IsDown(54) || keyboard::IsDown(44) || keyboard::IsDown(51)) {
        buttons |= nn::hid::Button::ZR;
    }

    // 6. Stick Clicks (L3 / R3: F / H)
    if (keyboard::IsDown(9))  buttons |= nn::hid::Button::LStick;
    if (keyboard::IsDown(11)) buttons |= nn::hid::Button::RStick;

    // 7. Navigation and Menu
    if (keyboard::IsDown(40) || keyboard::IsDown(19)) buttons |= nn::hid::Button::Plus;
    if (keyboard::IsDown(41))                         buttons |= nn::hid::Button::B;
    if (keyboard::IsDown(43))                         buttons |= nn::hid::Button::Minus;

    state->buttons |= buttons;

    // Sliders to Analog Sticks (Q/E and U/O)
    float lx = keyboard::GetLeftStickX();
    if (lx != 0.0f) state->analogStickL[0] = (int32_t)(lx * 32767.0f);

    float rx = keyboard::GetRightStickX();
    if (rx != 0.0f) state->analogStickR[0] = (int32_t)(rx * 32767.0f);
}

// Hook Trampolines
HOOK_DEFINE_TRAMPOLINE(GetNpadStateHandheldHook) {
    static void Callback(nn::hid::NpadHandheldState* state, const uint32_t& id) {
        Orig(state, id);
        keyboard::Inject(state);
    }
};

HOOK_DEFINE_TRAMPOLINE(GetNpadStateFullKeyHook) {
    static void Callback(nn::hid::NpadHandheldState* state, const uint32_t& id) {
        Orig(state, id);
        keyboard::Inject(state);
    }
};

HOOK_DEFINE_TRAMPOLINE(GetNpadStateJoyDualHook) {
    static void Callback(nn::hid::NpadHandheldState* state, const uint32_t& id) {
        Orig(state, id);
        keyboard::Inject(state);
    }
};

void keyboard::init()
{
    if (!Config::enableKeyboard) {
        nn::hid::g_keyboardModeEnabled = false;
        return;
    }

    nn::hid::g_keyboardModeEnabled = true;

    if (nn::hid::GetNpadStateHandheld) {
        GetNpadStateHandheldHook::InstallAtPtr((uintptr_t)nn::hid::GetNpadStateHandheld);
    }
    if (nn::hid::GetNpadStateFullKey) {
        GetNpadStateFullKeyHook::InstallAtPtr((uintptr_t)nn::hid::GetNpadStateFullKey);
    }
    if (nn::hid::GetNpadStateJoyDual) {
        GetNpadStateJoyDualHook::InstallAtPtr((uintptr_t)nn::hid::GetNpadStateJoyDual);
    }
}
