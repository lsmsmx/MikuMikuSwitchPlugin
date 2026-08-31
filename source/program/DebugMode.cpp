#include "DebugMode.hpp"
#include "Config.hpp"
#include "macros.hpp"
#include "fs.hpp"
#include <cmath>
#include <cstdio>
#include <string>
#include <algorithm>
#include <nn/os.hpp>
#include "ImGui.hpp"
#include "InputOverlay.hpp"

// =========================================================
// ADDRESSES & CONSTANTS (NSO = Ghidra - 0x100)
// =========================================================
#define ADDR_CHANGE_GAME_STATE      FIX(0x00217010)
#define ADDR_CHANGE_GAME_SUB_STATE  FIX(0x00216810)
#define ADDR_ENGINE_UPDATE_TICK     FIX(0x00201010)
#define ADDR_GET_INPUT_STATE        FIX(0x001FD580)
#define ADDR_BOUNDING_BOX           FIX(0x00341BB0)

#define ADDR_PATCH_GUI_DRAW         FIX(0x00201658)
#define ADDR_PATCH_SHOW_SPRITES_1   FIX(0x0041EC10)
#define ADDR_PATCH_SHOW_SPRITES_2   FIX(0x0041EC30)
#define ADDR_PATCH_NOP_1            FIX(0x003354AC)
#define ADDR_PATCH_NOP_2            FIX(0x003354C4)

namespace DebugMode {

    bool g_DebugModeEnabled = false;

    // Safe transition queue (PC delay simulation)
    static GameState g_targetState = GameState::MAX;
    static GameSubState g_targetSubState = GameSubState::MAX;
    static int g_transitionTimer = 0;

    typedef void (*ChangeGameStateT)(int32_t state);
    typedef void (*ChangeGameSubStateT)(uint32_t state, int32_t substate);
    typedef DivaInputState* (*GetInputStateT)(uint32_t player);

    void ChangeGameState(GameState state) {
        auto func = (ChangeGameStateT)(exl::util::GetMainModuleInfo().m_Total.m_Start + ADDR_CHANGE_GAME_STATE);
        func(static_cast<int32_t>(state));
    }

    void ChangeGameSubState(GameState state, GameSubState substate) {
        auto func = (ChangeGameSubStateT)(exl::util::GetMainModuleInfo().m_Total.m_Start + ADDR_CHANGE_GAME_SUB_STATE);
        func(static_cast<uint32_t>(state), static_cast<int32_t>(substate));
    }

    void RequestStateChange(GameState state, GameSubState substate) {
        ChangeGameState(GameState::DATA_TEST);
        g_targetState = state;
        g_targetSubState = substate;
        g_transitionTimer = 15;
    }

    void ToggleDebugModePatches() {
        if (g_DebugModeEnabled) {
            exl::patch::CodePatcher(ADDR_PATCH_GUI_DRAW).Write<uint32_t>(0x2A0003F3);
            exl::patch::CodePatcher(ADDR_PATCH_SHOW_SPRITES_1).Write<uint32_t>(0x1400000B);
            exl::patch::CodePatcher(ADDR_PATCH_SHOW_SPRITES_2).Write<uint32_t>(0x14000003);
            exl::patch::CodePatcher(ADDR_PATCH_NOP_1).Write<uint32_t>(0xD503201F);
            exl::patch::CodePatcher(ADDR_PATCH_NOP_2).Write<uint32_t>(0xD503201F);
        } else {
            exl::patch::CodePatcher(ADDR_PATCH_GUI_DRAW).Write<uint32_t>(0x320007E0);
            exl::patch::CodePatcher(ADDR_PATCH_SHOW_SPRITES_1).Write<uint32_t>(0x36000160);
            exl::patch::CodePatcher(ADDR_PATCH_SHOW_SPRITES_2).Write<uint32_t>(0x36000060);
            exl::patch::CodePatcher(ADDR_PATCH_NOP_1).Write<uint32_t>(0x360000A0);
            exl::patch::CodePatcher(ADDR_PATCH_NOP_2).Write<uint32_t>(0x360000A0);
        }
    }

    void ProcessDebugInputs() {
        if (ImGui::g_isMenuOpen && ImGui::g_imguiHasFocus) {
            return;
        }

        nn::hid::NpadHandheldState mergedState = nn::hid::GetMergedNpadState();
        uint64_t keysHeld = mergedState.buttons;

        static uint64_t s_prevKeys = 0;
        uint64_t keysDown = keysHeld & ~s_prevKeys;
        s_prevKeys = keysHeld;

        // Toggle debug mode (L + R + Plus)
        if ((keysHeld & nn::hid::Button::L) && (keysHeld & nn::hid::Button::R) && (keysDown & nn::hid::Button::Plus)) {
            g_DebugModeEnabled = !g_DebugModeEnabled;
            ToggleDebugModePatches();
        }

        if (!g_DebugModeEnabled) return;

        // D-Pad Hotkeys (Safe transitions)
        if ((keysHeld & nn::hid::Button::L) && (keysHeld & nn::hid::Button::R)) {
            if (keysDown & nn::hid::Button::Down)   { ChangeGameState(GameState::DATA_TEST); }
            if (keysDown & nn::hid::Button::Up)     { ChangeGameState(GameState::TEST_MODE); }
            if (keysDown & nn::hid::Button::Right)  { ChangeGameState(GameState::MENU_SWITCH); }
            if (keysDown & nn::hid::Button::Left)   { RequestStateChange(GameState::CS_MENU, static_cast<GameSubState>(32)); }
        }

        GetInputStateT GetInput = (GetInputStateT)(exl::util::GetMainModuleInfo().m_Total.m_Start + ADDR_GET_INPUT_STATE);
        DivaInputState* dis = GetInput(0);
        if (!dis) return;

        // Toggle mouse input
        static bool s_mouseEnabled = false;
        if (keysDown & nn::hid::Button::LStick) { s_mouseEnabled = !s_mouseEnabled; }

        if (keysHeld & nn::hid::Button::RStick) {
            dis->Key = 0x11; // VK_CONTROL

            if (!s_mouseEnabled) {
                struct SceneEntry { int32_t state; int32_t sub; };
                static const SceneEntry SCENES[] = {
                    {0, 0}, {0, 1}, {9, 2}, {9, 3}, {9, 4}, {9, 5}, {9, 6}, {9, 7},
                    {9, 8}, {3, 8}, {3, 9}, {3, 10}, {3, 11}, {3, 12}, {3, 13}, {3, 14}, {3, 15}, {3, 16},
                    {3, 17}, {3, 18}, {3, 19}, {3, 20}, {3, 21}, {3, 22}, {3, 23}, {3, 24},
                    {3, 25}, {3, 26}, {3, 27}, {3, 28}, {4, 29}, {5, 30}, {3, 31}, {6, 32},
                    {6, 33}, {6, 34}, {6, 35}, {6, 36}, {6, 37}, {6, 38}, {6, 39}, {6, 40},
                    {6, 41}, {6, 42}, {6, 43}, {6, 44}, {6, 45}, {6, 46}
                };
                constexpr int SCENE_COUNT = sizeof(SCENES) / sizeof(SCENES[0]);

                static int32_t s_SelectedIdx = 9;

                if (keysDown & nn::hid::Button::Down) { s_SelectedIdx = (s_SelectedIdx + 1) % SCENE_COUNT; }
                if (keysDown & nn::hid::Button::Up)   { s_SelectedIdx = (s_SelectedIdx - 1 + SCENE_COUNT) % SCENE_COUNT; }
                if (keysDown & nn::hid::Button::A) {
                    RequestStateChange(static_cast<GameState>(SCENES[s_SelectedIdx].state),
                                       static_cast<GameSubState>(SCENES[s_SelectedIdx].sub));
                }
            }
        } else {
            dis->Key = 0;
        }

        if (s_mouseEnabled) {
            bool block = (keysHeld & nn::hid::Button::L) || (keysHeld & nn::hid::Button::R);
            bool isZL = (keysHeld & nn::hid::Button::ZL) && !block;
            bool isZR = (keysHeld & nn::hid::Button::ZR) && !block;

            static uint64_t lastTick = 0;
            uint64_t currentTick = nn::os::GetSystemTick().GetInt64Value();
            float dt = 1.0f / 60.0f;
            if (lastTick != 0) {
                uint64_t freq = nn::os::GetSystemTickFrequency();
                dt = (float)(currentTick - lastTick) / (float)freq;
                if (dt <= 0.0f || dt > 0.1f) dt = 1.0f / 60.0f;
            }
            lastTick = currentTick;

            float frameComp = dt * 60.0f;

            // 1. Joystick
            float lx = (float)mergedState.analogStickL[0] / 32767.0f;
            float ly = (float)mergedState.analogStickL[1] / 32767.0f;

            float speed = (keysHeld & nn::hid::Button::Y) ? 12.0f : 5.0f;

            if (std::abs(lx) > 0.15f) dis->MouseX += (int32_t)(speed * lx * frameComp);
            if (std::abs(ly) > 0.15f) dis->MouseY -= (int32_t)(speed * ly * frameComp);

            // 2. USB Mouse
            if (nn::hid::GetMouseState) {
                nn::hid::MouseState ms = {};
                nn::hid::GetMouseState(&ms);
                dis->MouseX += ms.deltaX;
                dis->MouseY += ms.deltaY;
                if (ms.buttons & 1) isZL = true;
                if (ms.buttons & 2) isZR = true;
            }

            // 3. Touch Screen
            if (nn::hid::GetTouchScreenStates) {
                nn::hid::TouchScreenState ts = {};
                nn::hid::GetTouchScreenStates(&ts, 1);
                if (ts.count > 0) {
                    dis->MouseX = (int32_t)(ts.touches[0].x * (1920.0f / 1280.0f));
                    dis->MouseY = (int32_t)(ts.touches[0].y * (1080.0f / 720.0f));
                    isZL = true;
                }
            }

            dis->MouseX = std::clamp(dis->MouseX, 0, 1919);
            dis->MouseY = std::clamp(dis->MouseY, 0, 1079);

            static bool s_zlPressed = false;
            static bool s_zrPressed = false;

            if (isZL) {
                dis->SetBit(100, !s_zlPressed, DivaInputState::Type_Tapped);
                dis->SetBit(100, true, DivaInputState::Type_Down);
                dis->SetBit(100, false, DivaInputState::Type_Released);
                s_zlPressed = true;
            } else {
                dis->SetBit(100, false, DivaInputState::Type_Tapped);
                dis->SetBit(100, false, DivaInputState::Type_Down);
                dis->SetBit(100, s_zlPressed, DivaInputState::Type_Released);
                s_zlPressed = false;
            }

            if (isZR) {
                dis->SetBit(102, !s_zrPressed, DivaInputState::Type_Tapped);
                dis->SetBit(102, true, DivaInputState::Type_Down);
                dis->SetBit(102, false, DivaInputState::Type_Released);
                s_zrPressed = true;
            } else {
                dis->SetBit(102, false, DivaInputState::Type_Tapped);
                dis->SetBit(102, false, DivaInputState::Type_Down);
                dis->SetBit(102, s_zrPressed, DivaInputState::Type_Released);
                s_zrPressed = false;
            }
        }
    }

    struct BoundingBox { float x, y, w, h; };

    HOOK_DEFINE_TRAMPOLINE(DwScrollableGetBoundingBoxHook) {
        static BoundingBox Callback(void* this_ptr) {
            if (!g_DebugModeEnabled) return Orig(this_ptr);

            uintptr_t a1 = (uintptr_t)this_ptr;
            float v2_orig = *(float*)(a1 + 0x40);
            float v3_orig = *(float*)(a1 + 0x44);
            bool v4_flag = (*(uint8_t*)(a1 + 0x51) & 0x08) == 0;

            BoundingBox box = { 0.0f, 0.0f, v2_orig, v3_orig };

            if (!v4_flag) {
                box.x = 2.0f;
                box.y = 2.0f;
                box.w = v2_orig - 4.0f;
                box.h = v3_orig - 4.0f;
            }

            uint64_t v5_parent = *(uint64_t*)(a1 + 0xD8);
            if (v5_parent) {
                box.w -= *(float*)(v5_parent + 0x40);
            }
            return box;
        }
    };

    HOOK_DEFINE_TRAMPOLINE(EngineUpdateTickHook) {
        static uint64_t Callback() {
            uint64_t result = Orig();

            if (g_transitionTimer > 0) {
                g_transitionTimer--;
                if (g_transitionTimer == 0 && g_targetState != GameState::MAX) {
                    ChangeGameSubState(g_targetState, g_targetSubState);
                    g_targetState = GameState::MAX;
                    g_targetSubState = GameSubState::MAX;
                }
            } else {
                ProcessDebugInputs();
            }
            return result;
        }
    };

    void Init() {
        if (!Config::enableDebug) return;

        DwScrollableGetBoundingBoxHook::InstallAtOffset(ADDR_BOUNDING_BOX);
        EngineUpdateTickHook::InstallAtOffset(ADDR_ENGINE_UPDATE_TICK);

        exl::patch::CodePatcher(FIX(0x002F8370)).Write<uint32_t>(0x7144011F);
        exl::patch::CodePatcher(FIX(0x0028DB18)).Write<uint32_t>(0x7144007F);
        exl::patch::CodePatcher(FIX(0x0028DB6C)).Write<uint32_t>(0x7144007F);
        exl::patch::CodePatcher(FIX(0x0028E2F4)).Write<uint32_t>(0x7144007F);
        //exl::patch::CodePatcher(FIX(0x00294D10)).Write<uint32_t>(0x7144011F); // not safe, breaks array
        exl::patch::CodePatcher(FIX(0x002FEB80)).Write<uint32_t>(0x714402DF);
        exl::patch::CodePatcher(FIX(0x003680D0)).Write<uint32_t>(0xF14402BF);
        exl::patch::CodePatcher(FIX(0x00316EB0)).Write<uint32_t>(0x92800003);
        exl::patch::CodePatcher(FIX(0x00316F4C)).Write<uint32_t>(0x92800003);
        exl::patch::CodePatcher(FIX(0x002F8504)).Write<uint32_t>(0x92800003);
    }
}
