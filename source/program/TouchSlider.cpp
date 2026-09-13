#include "TouchSlider.hpp"
#include "ImGui.hpp"
#include "DebugMode.hpp"
#include "hid.hpp"
#include <cmath>
#include <algorithm>

namespace TouchSlider {

    struct FingerTracker {
        bool active = false;
        uint32_t fingerId = 0;
        int32_t startX = 0, startY = 0;
        int32_t prevX = 0, prevY = 0;
        int32_t currentDirX = 0; // -1 = Left, +1 = Right, 0 = Neutral
        int32_t currentDirY = 0; // -1 = Down, +1 = Up, 0 = Neutral
    };

    static FingerTracker s_leftSide;
    static FingerTracker s_rightSide;

    void Update(nn::hid::NpadHandheldState* state) {
        if (ImGui::g_isMenuOpen || DebugMode::g_DebugModeEnabled) return;
        if (!nn::hid::GetTouchScreenState) return;

        nn::hid::TouchScreenState ts = {};
        nn::hid::GetTouchScreenState(&ts);

        // Movement deadzone before engaging the virtual stick
        const int32_t SLIDE_DEADZONE = 12;

        bool leftFound = false;
        bool rightFound = false;

        for (int i = 0; i < ts.count; i++) {
            const auto& t = ts.touches[i];
            bool isLeftZone = (t.x < 640);

            if (isLeftZone && leftFound) continue;
            if (!isLeftZone && rightFound) continue;

            FingerTracker& tracker = isLeftZone ? s_leftSide : s_rightSide;
            (isLeftZone ? leftFound : rightFound) = true;

            if (!tracker.active || tracker.fingerId != t.fingerId) {
                // Finger touched down: initialize baseline coordinates
                tracker.active = true;
                tracker.fingerId = t.fingerId;
                tracker.startX = (int32_t)t.x;
                tracker.startY = (int32_t)t.y;
                tracker.prevX  = (int32_t)t.x;
                tracker.prevY  = (int32_t)t.y;
                tracker.currentDirX = 0;
                tracker.currentDirY = 0;
            } else {
                int32_t totalDx = (int32_t)t.x - tracker.startX;
                int32_t totalDy = (int32_t)t.y - tracker.startY;
                int32_t moveDx  = (int32_t)t.x - tracker.prevX;
                int32_t moveDy  = (int32_t)t.y - tracker.prevY;

                // 1. Horizontal tracking (Slide Chains & Star Flicks)
                if (std::abs(totalDx) >= SLIDE_DEADZONE) {
                    int32_t targetDirX = (totalDx > 0) ? 1 : -1;

                    // Reversal / Zig-zag detection (e.g., swiping rapidly left-to-right)
                    // If moving opposite to current held deflection, reset origin
                    if (tracker.currentDirX != 0 && targetDirX != tracker.currentDirX && std::abs(moveDx) >= 10) {
                        tracker.startX = (int32_t)t.x;
                        tracker.currentDirX = 0; // 1-frame neutral allows input.cpp to register the new flick
                    } else {
                        tracker.currentDirX = targetDirX;
                    }
                }

                // 2. Vertical tracking (Up / Down)
                if (std::abs(totalDy) >= SLIDE_DEADZONE) {
                    int32_t targetDirY = (totalDy < 0) ? 1 : -1; // Screen upwards is positive stick Y

                    if (tracker.currentDirY != 0 && targetDirY != tracker.currentDirY && std::abs(moveDy) >= 10) {
                        tracker.startY = (int32_t)t.y;
                        tracker.currentDirY = 0;
                    } else {
                        tracker.currentDirY = targetDirY;
                    }
                }

                tracker.prevX = (int32_t)t.x;
                tracker.prevY = (int32_t)t.y;
            }
        }

        // Finger lifted off the screen: immediately return stick to neutral
        if (!leftFound) {
            s_leftSide.active = false;
            s_leftSide.currentDirX = 0;
            s_leftSide.currentDirY = 0;
        }
        if (!rightFound) {
            s_rightSide.active = false;
            s_rightSide.currentDirX = 0;
            s_rightSide.currentDirY = 0;
        }

        // Apply stick deflection CONTINUOUSLY while finger is holding/sliding
        // This guarantees slide chains never drop as long as the finger is moving or held
        if (s_leftSide.active && s_leftSide.currentDirX != 0) {
            state->analogStickL[0] = (s_leftSide.currentDirX > 0) ? 32767 : -32767;
        }
        if (s_leftSide.active && s_leftSide.currentDirY != 0) {
            state->analogStickL[1] = (s_leftSide.currentDirY > 0) ? 32767 : -32767;
        }

        if (s_rightSide.active && s_rightSide.currentDirX != 0) {
            state->analogStickR[0] = (s_rightSide.currentDirX > 0) ? 32767 : -32767;
        }
        if (s_rightSide.active && s_rightSide.currentDirY != 0) {
            state->analogStickR[1] = (s_rightSide.currentDirY > 0) ? 32767 : -32767;
        }
    }
}
