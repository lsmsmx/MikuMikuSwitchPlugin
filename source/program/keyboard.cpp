#include <stdint.h>
#include <array>
#include "lib.hpp"
#include "keyboard.hpp"
#include "hid.hpp"
#include "Config.hpp"
#include "TouchSlider.hpp"
#include "DebugMode.hpp"
#include "ImGui.hpp"
#include "imgui/imgui.h"
#include "overlays/VirtualKeyboard.hpp"

namespace keyboard {

    static std::array<bool, 256> s_keys = {};
    static std::array<bool, 256> s_prev_keys = {};
    static int s_lastImGuiFrame = -1;
    static uint64_t s_lastPhysicalKeyTick = 0;

    // Prevent Plus button trigger when finishing text input
    static bool s_ignoreEnterForPlus = false;
    static bool s_ignoreEscapeForPlus = false;

    void Poll() {
        if (!Config::enableKeyboard || !nn::hid::GetKeyboardState) return;

        nn::hid::KeyboardState ks = {};
        nn::hid::GetKeyboardState(&ks);

        for (int i = 0; i < 256; i++) {
            s_keys[i] = (ks.keys[i / 64] & (1ULL << (i % 64))) != 0;
        }
    }

    bool IsRecentlyUsed() {
        if (!Config::enableKeyboard) return false;
        uint64_t now = nn::os::GetSystemTick().GetInt64Value();
        // Active if key pressed within the last 10 seconds
        return (now - s_lastPhysicalKeyTick) < (19200000ULL * 10);
    }

    bool IsDown(int32_t key)   { return s_keys[key]; }
    bool IsTapped(int32_t key) { return s_keys[key] && !s_prev_keys[key]; }
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

    // =========================================================================
    // FEED KEYBOARD EVENTS TO IMGUI
    // =========================================================================
    static void FeedKeyboardToImGui() {
        if (ImGui::GetCurrentContext() == nullptr) return;

        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantTextInput) {
            s_prev_keys = s_keys;
            return;
        }

        bool shift = IsDown(225) || IsDown(229); // Shift
        bool anyKeyPressed = false;

        // Letters A-Z (USB HID: 4..29)
        for (int k = 4; k <= 29; k++) {
            if (s_keys[k] && !s_prev_keys[k]) {
                char c = (shift ? 'A' : 'a') + (k - 4);
                io.AddInputCharacter(c);
                anyKeyPressed = true;
            }
        }

        // Digits 0-9 (scancodes: 30..39)
        const char numChars[]      = "1234567890";
        const char numShiftChars[] = "!@#$%^&*()";
        for (int k = 30; k <= 39; k++) {
            if (s_keys[k] && !s_prev_keys[k]) {
                char c = shift ? numShiftChars[k - 30] : numChars[k - 30];
                io.AddInputCharacter(c);
                anyKeyPressed = true;
            }
        }

        // Punctuation and symbols
        if (s_keys[44] && !s_prev_keys[44]) { io.AddInputCharacter(' '); anyKeyPressed = true; }
        if (s_keys[45] && !s_prev_keys[45]) { io.AddInputCharacter(shift ? '_' : '-'); anyKeyPressed = true; }
        if (s_keys[55] && !s_prev_keys[55]) { io.AddInputCharacter(shift ? '>' : '.'); anyKeyPressed = true; }
        if (s_keys[54] && !s_prev_keys[54]) { io.AddInputCharacter(shift ? '<' : ','); anyKeyPressed = true; }
        if (s_keys[56] && !s_prev_keys[56]) { io.AddInputCharacter(shift ? '?' : '/'); anyKeyPressed = true; }

        // Escape
        io.AddKeyEvent(ImGuiKey_Escape, s_keys[41]);
        if (s_keys[41]) anyKeyPressed = true;

        // Enter and Numpad Enter
        io.AddKeyEvent(ImGuiKey_Enter, s_keys[40] || s_keys[88]);
        if (s_keys[40] || s_keys[88]) anyKeyPressed = true;

        // Left and Right arrow keys
        io.AddKeyEvent(ImGuiKey_LeftArrow, s_keys[80]);
        io.AddKeyEvent(ImGuiKey_RightArrow, s_keys[79]);

        // Delete and Backspace
        io.AddKeyEvent(ImGuiKey_Delete, s_keys[76]);
        io.AddKeyEvent(ImGuiKey_Backspace, s_keys[42]);
        if (s_keys[42] || s_keys[76]) anyKeyPressed = true;

        if (anyKeyPressed) {
            s_lastPhysicalKeyTick = nn::os::GetSystemTick().GetInt64Value();
            // Close virtual keyboard if physical typing occurs
            if (VirtualKeyboard::IsOpen()) {
                VirtualKeyboard::Close();
            }
        }

        // Update s_prev_keys once per ImGui frame
        s_prev_keys = s_keys;
    }

    void Inject(nn::hid::NpadHandheldState* state) {
        if (!Config::enableKeyboard || !state) return;

        Poll();

        if (ImGui::GetCurrentContext() != nullptr) {
            int currentFrame = ImGui::GetFrameCount();
            if (currentFrame != s_lastImGuiFrame) {
                s_lastImGuiFrame = currentFrame;
                FeedKeyboardToImGui();
            }

            // Block gamepad and register Enter/Escape while text input is active
            if (ImGui::GetIO().WantTextInput) {
                if (IsDown(40)) s_ignoreEnterForPlus = true;
                if (IsDown(41)) s_ignoreEscapeForPlus = true;
                return;
            }
        }

        // Reset Plus block when physical keys are released
        if (!IsDown(40)) s_ignoreEnterForPlus = false;
        if (!IsDown(41)) s_ignoreEscapeForPlus = false;

        uint64_t buttons = 0;

        // Notes
        if (IsDown(12)) buttons |= nn::hid::Button::X;
        if (IsDown(13)) buttons |= nn::hid::Button::Y;
        if (IsDown(14) || IsDown(42)) buttons |= nn::hid::Button::B;
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

        // System: Enter and Escape do not trigger Plus if search was just performed
        bool plus = IsDown(19); // P key always triggers Plus
        if (IsDown(40) && !s_ignoreEnterForPlus) plus = true;
        if (IsDown(41) && !s_ignoreEscapeForPlus) plus = true;

        if (plus) buttons |= nn::hid::Button::Plus;
        if (IsDown(43)) buttons |= nn::hid::Button::Minus;

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

} // namespace keyboard
