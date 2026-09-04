#include <stdint.h>
#include <array>
#include <cmath>
#include <algorithm>
#include "lib.hpp"
#include "keyboard_sliders.hpp"
#include "hid.hpp"
#include "Config.hpp"
#include "DebugMode.hpp"
#include "ImGui.hpp"

namespace keyboard_sliders {

    // =========================================================
    // KEYBOARD IMPLEMENTATION
    // =========================================================
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

    void InjectKeyboard(nn::hid::NpadHandheldState* state) {
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

        // Sliders to Analog Sticks
        float lx = GetLeftStickX();
        if (lx != 0.0f) state->analogStickL[0] = static_cast<int32_t>(lx * 32767.0f);

        float rx = GetRightStickX();
        if (rx != 0.0f) state->analogStickR[0] = static_cast<int32_t>(rx * 32767.0f);
    }

    // =========================================================
    // TOUCH SLIDERS (X & Y FULL 2D TRACKING)
    // =========================================================
    struct TouchTracker {
        bool active = false;
        uint32_t fingerId = 0;
        int32_t startX = 0, prevX = 0;
        int32_t startY = 0, prevY = 0;
        int holdFramesX = 0;
        int holdFramesY = 0;
        int32_t stickValueX = 0;
        int32_t stickValueY = 0;
    };

    static TouchTracker s_leftTouch;
    static TouchTracker s_rightTouch;

    static void UpdateTouchSliders(nn::hid::NpadHandheldState* state) {
        if (!nn::hid::GetTouchScreenState) return;

        nn::hid::TouchScreenState ts = {};
        nn::hid::GetTouchScreenState(&ts);

        // Thresholds in pixels
        const int32_t THRESHOLD_X = 14;
        const int32_t THRESHOLD_Y = 12; // Slightly more sensitive for vertical thumb movement
        const int HOLD_FRAMES     = 2;  // Quick return to center for rapid star streams

        bool leftFound = false;
        bool rightFound = false;

        for (int i = 0; i < ts.count; i++) {
            const auto& t = ts.touches[i];
            bool isLeftZone = (t.x < 640);

            if (isLeftZone && leftFound) continue;
            if (!isLeftZone && rightFound) continue;

            TouchTracker& tracker = isLeftZone ? s_leftTouch : s_rightTouch;
            (isLeftZone ? leftFound : rightFound) = true;

            if (!tracker.active || tracker.fingerId != t.fingerId) {
                tracker.active = true;
                tracker.fingerId = t.fingerId;
                tracker.startX = (int32_t)t.x;
                tracker.prevX  = (int32_t)t.x;
                tracker.startY = (int32_t)t.y;
                tracker.prevY  = (int32_t)t.y;
            } else {
                int32_t dx = (int32_t)t.x - tracker.prevX;
                int32_t totalDx = (int32_t)t.x - tracker.startX;
                int32_t dy = (int32_t)t.y - tracker.prevY;
                int32_t totalDy = (int32_t)t.y - tracker.startY;

                // 1. Horizontal flick detection (X-axis)
                if (std::abs(dx) >= THRESHOLD_X || std::abs(totalDx) >= THRESHOLD_X) {
                    int32_t dirX = (dx != 0) ? dx : totalDx;
                    tracker.stickValueX = (dirX > 0) ? 32767 : -32767;
                    tracker.holdFramesX = HOLD_FRAMES;
                    tracker.startX = (int32_t)t.x;
                }

                // 2. Vertical flick detection (Y-axis): Up is negative in screen space, but positive in stick space!
                if (std::abs(dy) >= THRESHOLD_Y || std::abs(totalDy) >= THRESHOLD_Y) {
                    int32_t dirY = (dy != 0) ? dy : totalDy;
                    tracker.stickValueY = (dirY < 0) ? 32767 : -32767; // Up = +32767, Down = -32767
                    tracker.holdFramesY = HOLD_FRAMES;
                    tracker.startY = (int32_t)t.y;
                }

                tracker.prevX = (int32_t)t.x;
                tracker.prevY = (int32_t)t.y;
            }
        }

        if (!leftFound)  s_leftTouch.active = false;
        if (!rightFound) s_rightTouch.active = false;

        // Apply Left Stick (X and Y)
        if (s_leftTouch.holdFramesX > 0) {
            state->analogStickL[0] = s_leftTouch.stickValueX;
            s_leftTouch.holdFramesX--;
        }
        if (s_leftTouch.holdFramesY > 0) {
            state->analogStickL[1] = s_leftTouch.stickValueY;
            s_leftTouch.holdFramesY--;
        }

        // Apply Right Stick (X and Y)
        if (s_rightTouch.holdFramesX > 0) {
            state->analogStickR[0] = s_rightTouch.stickValueX;
            s_rightTouch.holdFramesX--;
        }
        if (s_rightTouch.holdFramesY > 0) {
            state->analogStickR[1] = s_rightTouch.stickValueY;
            s_rightTouch.holdFramesY--;
        }
    }

    // =========================================================
    // UNIFIED INJECTION POINT
    // =========================================================
    static inline void ApplyInjections(nn::hid::NpadHandheldState* state) {
        if (!state) return;

        InjectKeyboard(state);

        // Process touch sliders
        bool canTouch = Config::enableTouch || DebugMode::g_DebugModeEnabled || ImGui::g_isMenuOpen;
        if (canTouch && !(ImGui::g_isMenuOpen && ImGui::g_imguiHasFocus)) {
            UpdateTouchSliders(state);
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

} // namespace keyboard_sliders
