#include <stdint.h>
#include <array>
#include "lib.hpp"
#include "keyboard_sliders.hpp"
#include "hid.hpp"
#include "Config.hpp"
#include "TouchSlider.hpp"
#include "DebugMode.hpp"
#include "ImGui.hpp"

namespace keyboard_sliders {

    static std::array<bool, 256> s_keys = {};

    void Poll() {
        if (!Config::enableKeyboard || !nn::hid::GetKeyboardState) return;

        nn::hid::KeyboardState ks = {};
        nn::hid::GetKeyboardState(&ks);

        for (int i = 0; i < 256; i++) {
            s_keys[i] = (ks.keys[i / 64] & (1ULL << (i % 64))) != 0;
        }
    }

    bool IsDown(int32_t key)   { return s_keys[key]; }
    bool IsTapped(int32_t key) { return s_keys[key]; }
    bool IsRepeat(int32_t key) { return s_keys[key]; }

    float GetLeftStickX() {
        if (IsDown(20)) return -1.0f; // Q
        if (IsDown(8))  return  1.0f; // E
        return 0.0f;
    }

    float GetRightStickX() {
        if (IsDown(24)) return -1.0f; // U
        if (IsDown(18)) return  1.0f; // O
        return 0.0f;
    }

    bool IsRawKeyDown(int32_t button) { return false; }
    int32_t GetGameButtonsMask(int32_t key_index) { return 0; }
    int32_t GetGameButtonsTappedMask(int32_t key_index) { return 0; }

    void Inject(nn::hid::NpadHandheldState* state) {
        if (!Config::enableKeyboard || !state) return;

        Poll();

        uint64_t buttons = 0;

        // Notes
        if (IsDown(12)) buttons |= nn::hid::Button::X;
        if (IsDown(13)) buttons |= nn::hid::Button::Y;
        if (IsDown(14)) buttons |= nn::hid::Button::B;
        if (IsDown(15)) buttons |= nn::hid::Button::A;

        // D-Pad
        if (IsDown(26) || IsDown(82)) buttons |= nn::hid::Button::Up;
        if (IsDown(4)  || IsDown(80)) buttons |= nn::hid::Button::Left;
        if (IsDown(22) || IsDown(81)) buttons |= nn::hid::Button::Down;
        if (IsDown(7)  || IsDown(79)) buttons |= nn::hid::Button::Right;

        // Triggers
        if (IsDown(225) || IsDown(27)) buttons |= nn::hid::Button::L;
        if (IsDown(229) || IsDown(16)) buttons |= nn::hid::Button::R;
        if (IsDown(224) || IsDown(29) || IsDown(57)) buttons |= nn::hid::Button::ZL;
        if (IsDown(228) || IsDown(54) || IsDown(44) || IsDown(51)) buttons |= nn::hid::Button::ZR;

        // Sticks
        if (IsDown(9))  buttons |= nn::hid::Button::LStick;
        if (IsDown(11)) buttons |= nn::hid::Button::RStick;

        // System
        if (IsDown(40) || IsDown(19)) buttons |= nn::hid::Button::Plus;
        if (IsDown(41))               buttons |= nn::hid::Button::B;
        if (IsDown(43))               buttons |= nn::hid::Button::Minus;

        state->buttons |= buttons;

        // Analog sliders
        float lx = GetLeftStickX();
        if (lx != 0.0f) state->analogStickL[0] = static_cast<int32_t>(lx * 32767.0f);

        float rx = GetRightStickX();
        if (rx != 0.0f) state->analogStickR[0] = static_cast<int32_t>(rx * 32767.0f);
    }

    static inline void ApplyInjections(nn::hid::NpadHandheldState* state) {
        if (!state) return;

        Inject(state);

        if (Config::enableTouch || DebugMode::g_DebugModeEnabled || ImGui::g_isMenuOpen) {
            TouchSlider::Update(state);
        }
    }

    HOOK_DEFINE_TRAMPOLINE(GetNpadStateHandheldHook) {
        static void Callback(nn::hid::NpadHandheldState* state, const uint32_t& id) {
            Orig(state, id);
            ApplyInjections(state);
        }
    };

    HOOK_DEFINE_TRAMPOLINE(GetNpadStateFullKeyHook) {
        static void Callback(nn::hid::NpadHandheldState* state, const uint32_t& id) {
            Orig(state, id);
            ApplyInjections(state);
        }
    };

    HOOK_DEFINE_TRAMPOLINE(GetNpadStateJoyDualHook) {
        static void Callback(nn::hid::NpadHandheldState* state, const uint32_t& id) {
            Orig(state, id);
            ApplyInjections(state);
        }
    };

    void init() {
        if (nn::hid::GetNpadStateHandheld) {
            GetNpadStateHandheldHook::InstallAtPtr((uintptr_t)nn::hid::GetNpadStateHandheld);
        }
        if (nn::hid::GetNpadStateFullKey) {
            GetNpadStateFullKeyHook::InstallAtPtr((uintptr_t)nn::hid::GetNpadStateFullKey);
        }
        if (nn::hid::GetNpadStateJoyDual) {
            GetNpadStateJoyDualHook::InstallAtPtr((uintptr_t)nn::hid::GetNpadStateJoyDual);
        }

        if (!Config::enableKeyboard) {
            nn::hid::g_keyboardModeEnabled = false;
            return;
        }

        nn::hid::g_keyboardModeEnabled = true;
    }

} // namespace keyboard_flicks
