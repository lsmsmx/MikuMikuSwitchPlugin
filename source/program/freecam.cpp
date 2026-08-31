#include "freecam.hpp"
#include "lib.hpp"
#include "hid.hpp"
#include "Config.hpp"
#include <cmath>
#include <algorithm>
#include "macros.hpp"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// =========================================================
// ADDRESSES & CONSTANTS (NSO = Ghidra - 0x100)
// =========================================================

#define ADDR_RENDER_PAUSED_TICK     FIX(0x002015D0)
#define ADDR_SET_CAMERA_DATA        FIX(0x0025F350)
#define ADDR_GET_BUTTON_PRESSED     FIX(0x001FCA10)
#define ADDR_UPDATE_CAMERA          FIX(0x0025F710)

// =========================================================
// BSS SECTION ABSOLUTE OFFSETS
// =========================================================

#define CAM_BSS_VIEWPOINT_X         0x00B2C7B818
#define CAM_BSS_VIEWPOINT_Y         0x00B2C7B81C
#define CAM_BSS_VIEWPOINT_Z         0x00B2C7B820
#define CAM_BSS_INTEREST_X          0x00B2C7B824
#define CAM_BSS_INTEREST_Y          0x00B2C7B828
#define CAM_BSS_INTEREST_Z          0x00B2C7B82C
#define CAM_BSS_ROLL                0x00B2C7B830
#define CAM_BSS_FOV                 0x00B2C7B834
#define OFFSET_IS_PLAY              0x004D285858
#define OFFSET_IS_PV                0x004D2C9990
#define OFFSET_PAUSE_STATE          0x00BE9B1048
#define OFFSET_PV_END               0x00B2E1BBCC

// Globals
static bool g_cameraOverwrite      = false;
static float g_verticalRotation    = 0.0f; // Yaw
static float g_horizontalRotation  = 0.0f; // Pitch

static uint32_t* g_is_play_addr      = nullptr;
static uint32_t* g_is_pv_addr        = nullptr;
static uint32_t* g_pause_state_addr  = nullptr;
static uint32_t* g_is_pv_end_addr    = nullptr;

struct Vec2 { float x, y; Vec2(float _x = 0, float _y = 0) : x(_x), y(_y) {} };

// PC math match
inline Vec2 PointFromAngle(float degrees, float distance) {
    float radians = (degrees + 90.0f) * M_PI / 180.0f;
    return Vec2(-1.0f * std::cos(radians) * distance, -1.0f * std::sin(radians) * distance);
}

// 1. Block camera write
HOOK_DEFINE_TRAMPOLINE(SetCameraDataHook) {
    static void Callback(void* cam) {
        if (!g_cameraOverwrite) Orig(cam);
    }
};

// 2. Smart button filter (allow A & Plus for pause)
HOOK_DEFINE_TRAMPOLINE(GetButtonPressedHook) {
    static bool Callback(void* inputState, uint32_t button, uint32_t param3) {
        if (g_cameraOverwrite) {
            if (button == 10 || button == 0) return Orig(inputState, button, param3);
            return false;
        }
        return Orig(inputState, button, param3);
    }
};

// 3. Main loop
HOOK_DEFINE_TRAMPOLINE(UpdateCameraHook) {
    static void* Callback() {


        nn::hid::NpadHandheldState state = nn::hid::GetMergedNpadState();
        uint64_t all_buttons = state.buttons;

        // Toggle: L + R + Minus
        static bool f11Held = false;
        bool toggleBtn = (all_buttons & nn::hid::Button::L) && (all_buttons & nn::hid::Button::R) && (all_buttons & nn::hid::Button::Minus);

        if (toggleBtn && !f11Held) {
            g_cameraOverwrite = !g_cameraOverwrite;
            f11Held = true;
            g_verticalRotation = 0.0f;
            g_horizontalRotation = 0.0f;
        }
        if (!toggleBtn && f11Held) f11Held = false;

        if (!g_cameraOverwrite) return Orig();

        // Memory access
        uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
        float* vX   = (float*)(base + CAM_BSS_VIEWPOINT_X);
        float* vY   = (float*)(base + CAM_BSS_VIEWPOINT_Y);
        float* vZ   = (float*)(base + CAM_BSS_VIEWPOINT_Z);
        float* iX   = (float*)(base + CAM_BSS_INTEREST_X);
        float* iY   = (float*)(base + CAM_BSS_INTEREST_Y);
        float* iZ   = (float*)(base + CAM_BSS_INTEREST_Z);
        float* roll = (float*)(base + CAM_BSS_ROLL);
        float* fov  = (float*)(base + CAM_BSS_FOV);

        // Speed
        bool fast = (all_buttons & nn::hid::Button::RStick);
        float speed = fast ? 0.5f : 0.1f;

        // Movement (vX, vZ)
        if (all_buttons & (nn::hid::Button::Up | nn::hid::Button::Down)) {
            Vec2 move = PointFromAngle(g_verticalRotation + ((all_buttons & nn::hid::Button::Up) ? 0.0f : -180.0f), speed);
            *vX += move.x; *vZ += move.y;
        }
        if (all_buttons & (nn::hid::Button::Left | nn::hid::Button::Right)) {
            Vec2 move = PointFromAngle(g_verticalRotation + ((all_buttons & nn::hid::Button::Right) ? 90.0f : -90.0f), speed);
            *vX += move.x; *vZ += move.y;
        }

        // Height (vY)
        if (all_buttons & nn::hid::Button::X) *vY += speed * 0.25f;
        if (all_buttons & nn::hid::Button::B) *vY -= speed * 0.25f;

        // Roll & Zoom
        bool yHeld = (all_buttons & nn::hid::Button::Y);
        if (all_buttons & nn::hid::Button::L) {
            if (yHeld) *roll -= speed / 5.0f;
            else *fov += speed;
        }
        if (all_buttons & nn::hid::Button::R) {
            if (yHeld) *roll -= speed / 5.0f;
            else *fov -= speed;
        }
        *fov = std::clamp(*fov, 1.0f, 200.0f);

        // Rotation
        float rx = ((float)state.analogStickR[0] / 32768.0f);
        float ry = ((float)state.analogStickR[1] / 32768.0f);

        if (std::abs(rx) > 0.1f) g_verticalRotation += rx * 2.0f;
        if (std::abs(ry) > 0.1f) g_horizontalRotation += ry * (2.0f / 5.0f);
        g_horizontalRotation = std::clamp(g_horizontalRotation, -75.0f, 75.0f);

        // Look At
        Vec2 intXZ = PointFromAngle(g_verticalRotation, 1.0f);
        *iX = *vX + intXZ.x;
        *iZ = *vZ + intXZ.y;
        *iY = *vY + PointFromAngle(g_horizontalRotation, 5.0f).x;

        return Orig();
    }
};

// 4. Pause rendering hook
HOOK_DEFINE_TRAMPOLINE(RenderWhilePausedHook) {
    static uint64_t Callback() {
        if (g_is_play_addr && g_is_pv_addr && g_pause_state_addr && g_is_pv_end_addr) {
            if (*g_is_play_addr == 1 && *g_is_pv_addr == 3 && *g_pause_state_addr == 3) {
                *g_pause_state_addr = 0;
                uint64_t ret = Orig();
                if (*g_is_pv_end_addr != 1) *g_pause_state_addr = 3;
                return ret;
            }
        }
        return Orig();
    }
};

void InitFreeCam() {
    if (!Config::enableDebug) return;

    uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
    g_is_play_addr      = (uint32_t*)(base + OFFSET_IS_PLAY);
    g_is_pv_addr        = (uint32_t*)(base + OFFSET_IS_PV);
    g_pause_state_addr  = (uint32_t*)(base + OFFSET_PAUSE_STATE);
    g_is_pv_end_addr    = (uint32_t*)(base + OFFSET_PV_END);

    RenderWhilePausedHook::InstallAtOffset(ADDR_RENDER_PAUSED_TICK);
    SetCameraDataHook::InstallAtOffset(ADDR_SET_CAMERA_DATA);
    GetButtonPressedHook::InstallAtOffset(ADDR_GET_BUTTON_PRESSED);
    UpdateCameraHook::InstallAtOffset(ADDR_UPDATE_CAMERA);
}
