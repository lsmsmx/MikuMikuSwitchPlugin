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
        int32_t startX = 0, prevX = 0;
        int32_t startY = 0, prevY = 0; // Added: Y tracking
        int holdFramesX = 0;
        int holdFramesY = 0;
        int32_t stickValueX = 0;
        int32_t stickValueY = 0;
    };

    static FingerTracker s_leftSide;
    static FingerTracker s_rightSide;

    void Update(nn::hid::NpadHandheldState* state) {
        // Guard: do not process flicks if plugin UI is focused
        if (ImGui::g_isMenuOpen || DebugMode::g_DebugModeEnabled) return;
        if (!nn::hid::GetTouchScreenState) return;

        nn::hid::TouchScreenState ts = {};
        nn::hid::GetTouchScreenState(&ts);

        const int32_t FLICK_THRESHOLD_X = 14;
        const int32_t FLICK_THRESHOLD_Y = 12; // Slightly more sensitive for vertical movement
        const int HOLD_FRAMES_COUNT     = 2;  // Quick return to center (2 frames instead of 5)

        bool leftFound = false;
        bool rightFound = false;

        // Iterate through all active fingers
        for (int i = 0; i < ts.count; i++) {
            const auto& t = ts.touches[i];
            bool isLeftZone = (t.x < 640);

            // Assign one finger per screen half
            if (isLeftZone && leftFound) continue;
            if (!isLeftZone && rightFound) continue;

            FingerTracker& tracker = isLeftZone ? s_leftSide : s_rightSide;
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

                // 1. Horizontal flick (Left / Right)
                if (std::abs(dx) >= FLICK_THRESHOLD_X || std::abs(totalDx) >= FLICK_THRESHOLD_X) {
                    int32_t dirX = (dx != 0) ? dx : totalDx;
                    tracker.stickValueX = (dirX > 0) ? 32767 : -32767;
                    tracker.holdFramesX = HOLD_FRAMES_COUNT;
                    tracker.startX = (int32_t)t.x;
                }

                // 2. Vertical flick (Up / Down): Up is negative in screen coordinates, positive on stick!
                if (std::abs(dy) >= FLICK_THRESHOLD_Y || std::abs(totalDy) >= FLICK_THRESHOLD_Y) {
                    int32_t dirY = (dy != 0) ? dy : totalDy;
                    tracker.stickValueY = (dirY < 0) ? 32767 : -32767;
                    tracker.holdFramesY = HOLD_FRAMES_COUNT;
                    tracker.startY = (int32_t)t.y;
                }

                tracker.prevX = (int32_t)t.x;
                tracker.prevY = (int32_t)t.y;
            }
        }

        // Reset tracking state when fingers leave the screen
        if (!leftFound)  s_leftSide.active = false;
        if (!rightFound) s_rightSide.active = false;

        // Apply Left Stick (X and Y)
        if (s_leftSide.holdFramesX > 0) {
            state->analogStickL[0] = s_leftSide.stickValueX;
            s_leftSide.holdFramesX--;
        }
        if (s_leftSide.holdFramesY > 0) {
            state->analogStickL[1] = s_leftSide.stickValueY;
            s_leftSide.holdFramesY--;
        }

        // Apply Right Stick (X and Y)
        if (s_rightSide.holdFramesX > 0) {
            state->analogStickR[0] = s_rightSide.stickValueX;
            s_rightSide.holdFramesX--;
        }
        if (s_rightSide.holdFramesY > 0) {
            state->analogStickR[1] = s_rightSide.stickValueY;
            s_rightSide.holdFramesY--;
        }
    }
}
