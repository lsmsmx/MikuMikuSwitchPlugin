#include "PluginHelpUi.hpp"
#include "lib.hpp"
#include "imgui/imgui.h"
#include <hid.hpp>
#include <cmath>
#include <algorithm>
#include <vector>
#include <string>

#include "../nc/diva_nc.hpp"

namespace ImGui {
    extern bool g_imguiHasFocus;
}

namespace PluginHelpUi {

    // Main menu Init and Dest function addresses
    static constexpr uintptr_t ADDR_MAIN_MENU_INIT = 0x00815150;
    static constexpr uintptr_t ADDR_MAIN_MENU_DEST = 0x008161b0;

    // State and SubState BSS variable offsets (from ChangeSubGameState)
    static constexpr uintptr_t OFFSET_GAME_STATE     = 0x00B2B92640;
    static constexpr uintptr_t OFFSET_GAME_SUB_STATE = 0x00B2B9265C;

    static bool s_isMainMenuActive = false;
    static uintptr_t s_mainMenuInstance = 0;

    constexpr float DialogW = 780.0f;
    constexpr float DialogH = 510.0f;
    constexpr float CanvasW = 1280.0f;
    constexpr float CanvasH = 720.0f;

    constexpr float AnimSpeedIn  = 8.0f;
    constexpr float AnimSpeedOut = 9.0f;

    enum class AnimState { Closed, Opening, Open, Closing };
    static AnimState s_animState = AnimState::Closed;
    static float s_animProgress  = 0.0f;

    constexpr int TotalPages = 2;
    static int s_currentPage = 0;

    // =========================================================
    // STATE CHECK: HOOKS + DIRECT MEMORY READING
    // =========================================================
    bool IsMainMenuActive() {
        if (!s_isMainMenuActive || s_mainMenuInstance == 0) {
            return false;
        }

        int innerState = *reinterpret_cast<int*>(s_mainMenuInstance + 0x64);
        if (innerState != 2) {
            return false;
        }

        // Verify global game state
        uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
        int curState    = *reinterpret_cast<int*>(base + OFFSET_GAME_STATE);
        int curSubState = *reinterpret_cast<int*>(base + OFFSET_GAME_SUB_STATE);
        if (curState != 9 || curSubState != 42) {
            return false;
        }

        return true;
    }

    bool IsOpen() {
        return s_animState != AnimState::Closed;
    }

    void Open() {
        if (s_animState == AnimState::Closed || s_animState == AnimState::Closing) {
            s_animState = AnimState::Opening;
            ImGui::g_imguiHasFocus = true;
            sound::PlaySelectSE();
        }
    }

    void Close() {
        if (s_animState == AnimState::Open || s_animState == AnimState::Opening) {
            s_animState = AnimState::Closing;
            sound::PlaySelectSE();
        }
    }

    void Toggle() {
        if (IsOpen()) Close();
        else Open();
    }

    // =========================================================
    // MAIN MENU SCENE HOOKS
    // =========================================================
    HOOK_DEFINE_INLINE(MainMenuInitHook) {
        static void Callback(exl::hook::InlineCtx* ctx) {
            s_mainMenuInstance = ctx->X[0];
            s_isMainMenuActive = true;
        }
    };

    HOOK_DEFINE_INLINE(MainMenuDestHook) {
        static void Callback(exl::hook::InlineCtx* ctx) {
            s_mainMenuInstance = 0;
            s_isMainMenuActive = false;
            s_animState        = AnimState::Closed;
            s_animProgress     = 0.0f;
        }
    };

    void Init() {
        s_isMainMenuActive = false;
        s_animState        = AnimState::Closed;
        s_animProgress     = 0.0f;
        s_currentPage      = 0;

        MainMenuInitHook::InstallAtOffset(ADDR_MAIN_MENU_INIT);
        MainMenuDestHook::InstallAtOffset(ADDR_MAIN_MENU_DEST);
    }

    // =========================================================
    // INPUT HANDLING (L / R / B)
    // =========================================================
    void Update() {
        if (!IsOpen()) return;

        ImGui::g_imguiHasFocus = true;

        nn::hid::NpadHandheldState s = nn::hid::GetMergedNpadState();
        static uint64_t s_prevBtns = 0;
        uint64_t pressed = s.buttons & ~s_prevBtns;
        s_prevBtns = s.buttons;

        // Close on B button (Plus button handled in Hints.cpp)
        if (pressed & nn::hid::Button::B) {
            Close();
            return;
        }

        // Page navigation
        if (pressed & (nn::hid::Button::L | nn::hid::Button::Left)) {
            if (s_currentPage > 0) {
                s_currentPage--;
                sound::PlaySelectSE();
            }
        }
        else if (pressed & (nn::hid::Button::R | nn::hid::Button::Right)) {
            if (s_currentPage < TotalPages - 1) {
                s_currentPage++;
                sound::PlaySelectSE();
            }
        }
    }

    static void DrawBoldText(ImDrawList* drawList, ImVec2 pos, ImU32 col, const char* text) {
        drawList->AddText(pos, col, text);
        drawList->AddText(ImVec2(pos.x + 1.0f, pos.y), col, text);
        drawList->AddText(ImVec2(pos.x, pos.y + 0.5f), col, text);
    }

    struct HelpEntry {
        std::string combo;
        std::string desc;
        bool isSeparator = false;
    };

    static std::vector<HelpEntry> GetPage1Items() {
        return {
            { "Hold Plus + Minus",       "Toggle Main Debug Menu (Mot&Wind, GamestateSwitcher, SavedataBackup)" },
            { "Hold ZL + ZR",            "Toggle Mouse Focus (Game / ImGUI)" },
            { "Hold L3 + R3 / F10",      "Input Overlay (Gamepad / Keyboard)" },
            { "Hold L3 + R3 + ZR",       "Resolution Scaler & PostProcess Tuning" },
            { "Hold L3 + R3 + R",        "Volume Booster (BGM / Buttons / SE up to 500%)" },
            { "Hold L3 + R3 + L",        "Screen Dimmer (Smooth Brightness Reduction)" },
            { "Hold L3 + R3 + ZL",       "Memory Scanner (L3 to update)" },
            { "Hold L3 + R3 + Plus",     "FPS Counter Overlay" },

            { "", "", true },

            { "Song Search (ID)",        "Direct jump by numerical PV ID" },
            { "Song Search (Name)",      "Direct jump by name (fuzzy search)" },

            { "", "", true },

            { "Random: Partial",         "Swaps note types by groups (Lane Shuffle)" },
            { "Random: Full",            "Randomizes each note individually" },
            { "Random Chance",           "Per-note random chance (0% - 100%)" }
        };
    }

    static std::vector<HelpEntry> GetPage2Items() {
        return {
            // Freecam
            { "L + R + Minus",           "Enable / Disable Camera Override" },
            { "D-Pad (All)",             "Move Forward / Backward / Left / Right" },
            { "Right Stick",             "Look in Desired Direction (Yaw / Pitch)" },
            { "B / X",                   "Move Camera Down / Up (Elevation)" },
            { "L / R",                   "Zoom Out / Zoom In (Field of View)" },
            { "Hold Y + L/R",            "Roll / Lean Camera Left / Right" },
            { "Hold R3",                 "Speed Boost of Camera Movement" },
            { "A (Single Press)",        "Freeze/Pause Scene Without In-Game Menu" },

            { "", "", true },

            // Debug Mode
            { "L + R + Plus",            "Enable / Disable Debug Mode" },
            { "L3",                      "Toggle Virtual Mouse Simulation" },
            { "Left Stick / Sensor",     "Move Mouse Cursor" },
            { "ZL / 1-Finger Tap",       "Left Mouse Click" },
            { "ZR / 2-Fingers Tap",      "Right Mouse Click" },
            { "Hold Y",                  "Mouse Movement Speed Boost" },
            { "L + R + D-Pad Up",        "Jump to Test Mode State" },
            { "L + R + D-Pad Down",      "Jump to Data Test State" },
            { "L + R + D-Pad Left",      "Jump to CS Menu (Instant Crash)" },
            { "L + R + D-Pad Right",     "Jump to Menu Switch (Main Menu)" }
        };
    }

    // =========================================================
    // MAIN RENDER ROUTINE
    // =========================================================
    void Draw() {
        Update();

        if (s_animState == AnimState::Closed) return;

        ImGuiIO& io = ImGui::GetIO();
        float dt = io.DeltaTime > 0.0f ? io.DeltaTime : (1.0f / 60.0f);

        if (s_animState == AnimState::Opening) {
            s_animProgress += dt * AnimSpeedIn;
            if (s_animProgress >= 1.0f) {
                s_animProgress = 1.0f;
                s_animState = AnimState::Open;
            }
        }
        else if (s_animState == AnimState::Closing) {
            s_animProgress -= dt * AnimSpeedOut;
            if (s_animProgress <= 0.0f) {
                s_animProgress = 0.0f;
                s_animState = AnimState::Closed;
                ImGui::g_imguiHasFocus = false;
                return;
            }
        }

        float alpha = std::clamp(s_animProgress, 0.0f, 1.0f);
        float scale = 0.88f + 0.12f * std::sin(alpha * 1.5707963f);

        float curW = DialogW * scale;
        float curH = DialogH * scale;
        ImVec2 center((CanvasW - curW) * 0.5f, (CanvasH - curH) * 0.5f);

        ImGui::SetNextWindowPos(center, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(curW, curH), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0f);

        ImGuiWindowFlags winFlags = ImGuiWindowFlags_NoTitleBar |
                                    ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoScrollbar |
                                    ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_NoFocusOnAppearing;

        if (ImGui::Begin("##PluginHelpNCWindow", nullptr, winFlags)) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRectFullScreen();

            drawList->AddRectFilled(center, ImVec2(center.x + curW, center.y + curH), IM_COL32(245, 252, 252, (int)(248.0f * alpha)), 14.0f);
            drawList->AddRect(center, ImVec2(center.x + curW, center.y + curH), IM_COL32(0, 206, 209, (int)(225.0f * alpha)), 14.0f, 0, 2.5f);

            float topH = 38.0f * scale;
            drawList->AddRectFilled(center, ImVec2(center.x + curW, center.y + topH), IM_COL32(0, 150, 180, (int)(250.0f * alpha)), 14.0f, ImDrawFlags_RoundCornersTop);

            const char* topTitle = "MikuMikuSwitchPlugin Manual";
            ImGui::SetWindowFontScale(1.60f);
            ImVec2 topTextSz = ImGui::CalcTextSize(topTitle);
            DrawBoldText(drawList, ImVec2(center.x + (curW - topTextSz.x) * 0.5f, center.y + 7.0f), IM_COL32(255, 255, 255, (int)(255.0f * alpha)), topTitle);
            ImGui::SetWindowFontScale(1.0f);

            const char* bannerTitles[TotalPages] = { "System & Overlay Hotkeys", "Freecam & In-Game Debug" };
            float ribbonW = 320.0f * scale;
            float ribbonH = 26.0f * scale;
            ImVec2 ribbonPos(center.x + (curW - ribbonW) * 0.5f, center.y + 45.0f);
            drawList->AddRectFilled(ribbonPos, ImVec2(ribbonPos.x + ribbonW, ribbonPos.y + ribbonH), IM_COL32(24, 88, 160, (int)(235.0f * alpha)), 8.0f);

            ImGui::SetWindowFontScale(1.40f);
            ImVec2 bTextSz = ImGui::CalcTextSize(bannerTitles[s_currentPage]);
            DrawBoldText(drawList, ImVec2(ribbonPos.x + (ribbonW - bTextSz.x) * 0.5f, ribbonPos.y + 4.0f), IM_COL32(255, 255, 255, (int)(255.0f * alpha)), bannerTitles[s_currentPage]);
            ImGui::SetWindowFontScale(1.0f);

            const char* leftTag = "Touch / Sensor Supported";
            DrawBoldText(drawList, ImVec2(center.x + 22.0f, center.y + 50.0f), IM_COL32(0, 160, 185, (int)(230.0f * alpha)), leftTag);

            const char* rightTag = "Version 11.0";
            ImVec2 rightTagSz = ImGui::CalcTextSize(rightTag);
            DrawBoldText(drawList, ImVec2(center.x + curW - 24.0f - rightTagSz.x, center.y + 50.0f), IM_COL32(0, 160, 185, (int)(230.0f * alpha)), rightTag);

            auto items = (s_currentPage == 0) ? GetPage1Items() : GetPage2Items();

            float startY  = center.y + 78.0f;
            float rowH    = (s_currentPage == 0) ? 29.0f : 21.0f;
            float pillW   = (curW - 40.0f);
            float pillH   = (s_currentPage == 0) ? 22.5f : 19.5f;
            float keyColW = 210.0f * scale;

            float curY = startY;

            for (size_t i = 0; i < items.size(); i++) {
                if (items[i].isSeparator) {
                    float sepY = curY + 3.0f;
                    drawList->AddLine(ImVec2(center.x + 24.0f, sepY), ImVec2(center.x + curW - 24.0f, sepY),
                                      IM_COL32(0, 180, 200, (int)(180.0f * alpha)), 1.5f);
                    curY += 8.0f;
                    continue;
                }

                ImVec2 pPos(center.x + 20.0f, curY);

                drawList->AddRectFilled(pPos, ImVec2(pPos.x + pillW, pPos.y + pillH), IM_COL32(230, 238, 242, (int)(210.0f * alpha)), pillH * 0.4f);
                drawList->AddRect(pPos, ImVec2(pPos.x + pillW, pPos.y + pillH), IM_COL32(185, 202, 212, (int)(150.0f * alpha)), pillH * 0.4f, 0, 1.0f);

                ImVec2 keyCapMin(pPos.x + 3.0f, pPos.y + 1.5f);
                ImVec2 keyCapMax(pPos.x + keyColW, pPos.y + pillH - 1.5f);
                drawList->AddRectFilled(keyCapMin, keyCapMax, IM_COL32(0, 155, 170, (int)(235.0f * alpha)), (pillH - 3.0f) * 0.4f);

                ImVec2 keyTextSz = ImGui::CalcTextSize(items[i].combo.c_str());
                DrawBoldText(drawList, ImVec2(keyCapMin.x + (keyColW - 3.0f - keyTextSz.x) * 0.5f, keyCapMin.y + (pillH - 3.0f - keyTextSz.y) * 0.5f),
                             IM_COL32(255, 255, 255, (int)(255.0f * alpha)), items[i].combo.c_str());

                float descX = pPos.x + keyColW + 12.0f;
                ImVec2 descTextSz = ImGui::CalcTextSize(items[i].desc.c_str());
                drawList->AddText(ImVec2(descX, pPos.y + (pillH - descTextSz.y) * 0.5f),
                                  IM_COL32(40, 55, 70, (int)(240.0f * alpha)), items[i].desc.c_str());

                curY += rowH;
            }

            float pageY = center.y + curH - 26.0f;
            std::string pageStr = (s_currentPage == 0) ? "<   Page 1 / 2   >" : "<   Page 2 / 2   >";
            ImVec2 pageSz = ImGui::CalcTextSize(pageStr.c_str());
            DrawBoldText(drawList, ImVec2(center.x + (curW - pageSz.x) * 0.5f, pageY), IM_COL32(0, 150, 180, (int)(255.0f * alpha)), pageStr.c_str());

            DrawBoldText(drawList, ImVec2(center.x + 35.0f, pageY), IM_COL32(0, 130, 155, (int)(230.0f * alpha)), "[L]");
            DrawBoldText(drawList, ImVec2(center.x + curW - 55.0f, pageY), IM_COL32(0, 130, 155, (int)(230.0f * alpha)), "[R]");

            drawList->PopClipRect();
        }
        ImGui::End();
    }

} // namespace PluginHelpUi
