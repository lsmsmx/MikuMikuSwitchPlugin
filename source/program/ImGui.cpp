#include "ImGui.hpp"
#include "macros.hpp"
#include <nn/os.hpp>
#include <hid.hpp>
#include <algorithm>
#include <cmath>

#include "FontLoader.hpp"
#include "keyboard.hpp"
#include "Config.hpp"

#include "overlays/ExtraMenu.hpp"
#include "overlays/InputOverlay.hpp"
#include "overlays/StateSwitcher.hpp"
#include "overlays/MemoryScannerUi.hpp"
#include "overlays/ResScalerUI.hpp"
#include "overlays/PostProcessUI.hpp"
#include "overlays/FpsOverlay.hpp"
#include "overlays/MotionPlayerUi.hpp"
#include "overlays/WindControl.hpp"
#include "overlays/SongQuickJump.hpp"
#include "overlays/DimOverlay.hpp"
#include "overlays/VolumeOverlay.hpp"
#include "overlays/SaveBackup.hpp"
#include "overlays/Hints.hpp"
#include "overlays/VirtualKeyboard.hpp"
#include "overlays/PluginHelpUi.hpp"
#include "nc/ui/customize_sel.hpp"

#include "nvn_hooks.hpp"
#include "backend/imgui_backend/imgui_impl_nvn.hpp"
#include "backend/imgui_alloc.hpp"

#ifndef IMNVNFUNC
#define IMNVNFUNC __attribute__((visibility("default")))
#endif

namespace ImGui {

    bool g_isMenuOpen = false;
    bool g_imguiHasFocus = true;

    static float g_cursorX = 640.0f;
    static float g_cursorY = 360.0f;

    void Init() {
        MemoryScannerUi::Init();
        MotionPlayerUi::Init();
        WindControl::Init();
        if (Config::enableNewClassics) {
            CustomizeSelUi::Init();
        }
        PluginHelpUi::Init();
        Hints::Init();
    }
}

extern "C" IMNVNFUNC ImGuiIO& nvnImguiGetIO() { return ImGui::GetIO(); }
extern "C" IMNVNFUNC void nvnImguiFontGetTexDataAsAlpha8(unsigned char** out_pixels, int* out_width, int* out_height, int* out_bytes_per_pixel) {
    ImGui::GetIO().Fonts->GetTexDataAsAlpha8(out_pixels, out_width, out_height, out_bytes_per_pixel);
}

extern "C" IMNVNFUNC void nvnImguiInitialize() {
    ImGui::CreateContext();

    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding    = 10.0f;
    style.ChildRounding     = 8.0f;
    style.FrameRounding     = 6.0f;
    style.PopupRounding     = 8.0f;
    style.ScrollbarRounding = 9.0f;
    style.GrabRounding      = 6.0f;
    style.TabRounding       = 6.0f;

    style.WindowBorderSize  = 1.2f;
    style.FrameBorderSize   = 1.0f;
    style.WindowPadding     = ImVec2(10.0f, 10.0f);
    style.FramePadding      = ImVec2(8.0f, 7.0f);

    style.Colors[ImGuiCol_Text]                  = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_TextDisabled]          = ImVec4(0.55f, 0.65f, 0.70f, 1.00f);

    style.Colors[ImGuiCol_WindowBg]              = ImVec4(0.07f, 0.09f, 0.11f, 0.94f);

    style.Colors[ImGuiCol_TitleBg]               = ImVec4(0.04f, 0.05f, 0.07f, 0.96f);
    style.Colors[ImGuiCol_TitleBgActive]         = ImVec4(0.08f, 0.11f, 0.14f, 1.00f);
    style.Colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.03f, 0.04f, 0.05f, 0.80f);

    style.Colors[ImGuiCol_Button]                = ImVec4(0.18f, 0.22f, 0.27f, 0.85f);
    style.Colors[ImGuiCol_ButtonHovered]         = ImVec4(0.24f, 0.30f, 0.37f, 0.95f);
    style.Colors[ImGuiCol_ButtonActive]          = ImVec4(0.14f, 0.18f, 0.22f, 1.00f);

    style.Colors[ImGuiCol_Header]                = ImVec4(0.16f, 0.20f, 0.25f, 0.80f);
    style.Colors[ImGuiCol_HeaderHovered]         = ImVec4(0.22f, 0.28f, 0.35f, 0.90f);
    style.Colors[ImGuiCol_HeaderActive]          = ImVec4(0.14f, 0.18f, 0.22f, 1.00f);

    style.Colors[ImGuiCol_FrameBg]               = ImVec4(0.10f, 0.13f, 0.16f, 0.85f);
    style.Colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.15f, 0.19f, 0.24f, 0.90f);
    style.Colors[ImGuiCol_FrameBgActive]         = ImVec4(0.12f, 0.15f, 0.19f, 1.00f);

    style.Colors[ImGuiCol_SliderGrab]            = ImVec4(0.82f, 0.85f, 0.87f, 0.90f);
    style.Colors[ImGuiCol_SliderGrabActive]      = ImVec4(1.00f, 0.47f, 0.71f, 1.00f);

    style.Colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.05f, 0.06f, 0.08f, 0.60f);
    style.Colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.18f, 0.22f, 0.27f, 0.80f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.25f, 0.30f, 0.37f, 0.90f);
    style.Colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.14f, 0.18f, 0.22f, 1.00f);

    style.Colors[ImGuiCol_CheckMark]             = ImVec4(1.00f, 0.47f, 0.71f, 1.00f);
    style.Colors[ImGuiCol_Border]                = ImVec4(1.00f, 1.00f, 1.00f, 0.15f);
    style.Colors[ImGuiCol_Separator]             = ImVec4(1.00f, 1.00f, 1.00f, 0.12f);

    FontLoader::LoadFonts(ImGui::GetIO());
}

extern "C" IMNVNFUNC ImDrawData* nvnImguiCalc() {
    static uint64_t lastTick = 0;
    uint64_t currentTick = nn::os::GetSystemTick().GetInt64Value();
    float dt = 1.0f / 60.0f;
    if (lastTick != 0) {
        uint64_t freq = nn::os::GetSystemTickFrequency();
        if (freq > 0) {
            dt = (float)(currentTick - lastTick) / (float)freq;
            if (dt <= 0.0f || dt > 0.1f) dt = 1.0f / 60.0f;
        }
    }
    lastTick = currentTick;

    nn::hid::NpadHandheldState npad = nn::hid::GetMergedNpadState();

    // Main Menu Hotkey: Plus + Minus (Hold 0.8s)
    static float menu_hold_timer = 0.0f;
    if ((npad.buttons & nn::hid::Button::Plus) && (npad.buttons & nn::hid::Button::Minus)) {
        menu_hold_timer += dt;
        if (menu_hold_timer >= 0.8f && (menu_hold_timer - dt) < 0.8f) {
            ImGui::g_isMenuOpen = !ImGui::g_isMenuOpen;
            ImGui::g_imguiHasFocus = ImGui::g_isMenuOpen;
        }
    } else {
        menu_hold_timer = 0.0f;
    }

    // BSS Gaps Overlay Hotkey: L3 + R3 + ZL
    static float mem_hold_timer = 0.0f;
    bool isMemCombo = (npad.buttons & nn::hid::Button::LStick) &&
                      (npad.buttons & nn::hid::Button::RStick) &&
                      (npad.buttons & nn::hid::Button::ZL);

    if (isMemCombo) {
        mem_hold_timer += dt;
        if (mem_hold_timer >= 0.8f && (mem_hold_timer - dt) < 0.8f) {
            MemoryScannerUi::g_showWindow = !MemoryScannerUi::g_showWindow;
            if (MemoryScannerUi::g_showWindow) {
                ImGui::g_imguiHasFocus = true;
            }
        }
    } else {
        mem_hold_timer = 0.0f;
    }

    // Resolution Scaler and Post Process Overlay Hotkey: L3 + R3 + ZR
    static float res_hold_timer = 0.0f;
    bool isResCombo = (npad.buttons & nn::hid::Button::LStick) &&
                      (npad.buttons & nn::hid::Button::RStick) &&
                      (npad.buttons & nn::hid::Button::ZR);

    if (isResCombo) {
        res_hold_timer += dt;
        if (res_hold_timer >= 0.8f && (res_hold_timer - dt) < 0.8f) {
            ResScalerUi::g_showWindow = !ResScalerUi::g_showWindow;
            PostProcessUi::g_showWindow = ResScalerUi::g_showWindow;
            if (ResScalerUi::g_showWindow) {
                ImGui::g_imguiHasFocus = true;
            }
        }
    } else {
        res_hold_timer = 0.0f;
    }

    // FPS Overlay Hotkey: L3 + R3 + Plus
    static float fps_hold_timer = 0.0f;
    bool isFpsCombo = (npad.buttons & nn::hid::Button::LStick) &&
                      (npad.buttons & nn::hid::Button::RStick) &&
                      (npad.buttons & nn::hid::Button::Plus);

    if (isFpsCombo) {
        fps_hold_timer += dt;
        if (fps_hold_timer >= 0.8f && (fps_hold_timer - dt) < 0.8f) {
            FpsOverlay::g_showWindow = !FpsOverlay::g_showWindow;
        }
    } else {
        fps_hold_timer = 0.0f;
    }

    // Dim Overlay Hotkey: L3 + R3 + L
    static float dim_hold_timer = 0.0f;
    bool isDimCombo = (npad.buttons & nn::hid::Button::LStick) &&
                      (npad.buttons & nn::hid::Button::RStick) &&
                      (npad.buttons & nn::hid::Button::L);

    if (isDimCombo) {
        dim_hold_timer += dt;
        if (dim_hold_timer >= 0.8f && (dim_hold_timer - dt) < 0.8f) {
            DimOverlay::g_showWindow = !DimOverlay::g_showWindow;
            if (DimOverlay::g_showWindow) {
                ImGui::g_imguiHasFocus = true;
            }
        }
    } else {
        dim_hold_timer = 0.0f;
    }

    // Volume Overlay Hotkey: L3 + R3 + R
    static float vol_hold_timer = 0.0f;
    bool isVolCombo = (npad.buttons & nn::hid::Button::LStick) &&
                      (npad.buttons & nn::hid::Button::RStick) &&
                      (npad.buttons & nn::hid::Button::R);

    if (isVolCombo) {
        vol_hold_timer += dt;
        if (vol_hold_timer >= 0.8f && (vol_hold_timer - dt) < 0.8f) {
            VolumeOverlay::g_showWindow = !VolumeOverlay::g_showWindow;
            if (VolumeOverlay::g_showWindow) {
                ImGui::g_imguiHasFocus = true;
            }
        }
    } else {
        vol_hold_timer = 0.0f;
    }

    // Input Overlay Hotkey: L3 + R3 (without ZL, ZR, L, R, and without Plus)
    static float overlay_hold_timer = 0.0f;
    bool isOverlayCombo = (npad.buttons & nn::hid::Button::LStick) &&
                          (npad.buttons & nn::hid::Button::RStick) &&
                          !(npad.buttons & nn::hid::Button::ZL) &&
                          !(npad.buttons & nn::hid::Button::ZR) &&
                          !(npad.buttons & nn::hid::Button::L) &&
                          !(npad.buttons & nn::hid::Button::R) &&
                          !(npad.buttons & nn::hid::Button::Plus);

    if (isOverlayCombo) {
        overlay_hold_timer += dt;
        if (overlay_hold_timer >= 0.8f && (overlay_hold_timer - dt) < 0.8f) {
            int nextMode = (InputOverlay::GetMode() + 1) % 3;
            InputOverlay::SetMode(nextMode);
        }
    } else {
        overlay_hold_timer = 0.0f;
    }

    // Keyboard Hotkey: F10
    static bool s_f10WasDown = false;
    bool isF10Down = keyboard::IsDown(67);
    if (isF10Down && !s_f10WasDown) {
        int curMode = InputOverlay::GetMode();
        InputOverlay::SetMode((curMode == InputOverlay::Mode_Keyboard) ? InputOverlay::Mode_Disabled : InputOverlay::Mode_Keyboard);
    }
    s_f10WasDown = isF10Down;

    bool overlayVisible = InputOverlay::IsVisible();

    // Check if any plugin UI or overlay is active
    bool anyUiOpen = ImGui::g_isMenuOpen || VirtualKeyboard::IsVisible() ||
        MotionPlayerUi::g_showUltimatePlayer || MotionPlayerUi::g_showMotionControlWindow ||
        WindControl::g_showWindow || MemoryScannerUi::g_showWindow || ResScalerUi::g_showWindow ||
        PostProcessUi::g_showWindow || CustomizeSelUi::IsOpen() || FpsOverlay::g_showWindow ||
        SongQuickJump::IsVisible() || DimOverlay::g_showWindow || VolumeOverlay::g_showWindow ||
        PluginHelpUi::IsOpen() || Hints::IsVisible();

    if (!anyUiOpen && !overlayVisible) {
        MotionPlayerUi::ResetPtrCounter();
        return nullptr;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = dt;
    io.DisplaySize = ImVec2(1280.0f, 720.0f);

    bool isLeftClick = false, isRightClick = false, isTouchActive = false;

    // Mouse cursor activity timer
    static float s_mouseActivityTimer = 0.0f;
    if (s_mouseActivityTimer > 0.0f) {
        s_mouseActivityTimer -= dt;
        if (s_mouseActivityTimer < 0.0f) s_mouseActivityTimer = 0.0f;
    }

    // Hide mouse cursor when gamepad buttons are pressed
    uint64_t padActionButtons = nn::hid::Button::A | nn::hid::Button::B | nn::hid::Button::X | nn::hid::Button::Y |
                                nn::hid::Button::Up | nn::hid::Button::Down | nn::hid::Button::Left | nn::hid::Button::Right |
                                nn::hid::Button::L | nn::hid::Button::R | nn::hid::Button::Minus | nn::hid::Button::Plus;
    if (npad.buttons & padActionButtons) {
        s_mouseActivityTimer = 0.0f;
    }

    bool isZLPressed = (npad.buttons & nn::hid::Button::ZL) != 0;
    bool isZRPressed = (npad.buttons & nn::hid::Button::ZR) != 0;

    // Toggle input focus: Hold ZL + ZR for 0.5s
    if (anyUiOpen) {
        static float switchHoldTime = 0.0f;
        if (isZLPressed && isZRPressed) {
            if (switchHoldTime >= 0.0f) {
                switchHoldTime += dt;
                if (switchHoldTime >= 0.5f) {
                    ImGui::g_imguiHasFocus = !ImGui::g_imguiHasFocus;
                    switchHoldTime = -1.0f;
                }
            }
        } else {
            switchHoldTime = 0.0f;
        }
    }

    if (anyUiOpen && ImGui::g_imguiHasFocus) {
        // 1. Touchscreen input
        if (nn::hid::GetTouchScreenState) {
            nn::hid::TouchScreenState ts = {};
            nn::hid::GetTouchScreenState(&ts);
            if (ts.count > 0) {
                ImGui::g_cursorX = (float)ts.touches[0].x;
                ImGui::g_cursorY = (float)ts.touches[0].y;
                isTouchActive = true;
                s_mouseActivityTimer = 0.0f;
                if (ts.count >= 2) isRightClick = true;
                else isLeftClick = true;
            }
        }

        // 2. Physical mouse input
        if (nn::hid::GetMouseState && !isTouchActive) {
            nn::hid::MouseState ms = {};
            nn::hid::GetMouseState(&ms);
            if (ms.deltaX != 0 || ms.deltaY != 0 || ms.buttons != 0 || ms.wheelDeltaY != 0) {
                ImGui::g_cursorX += (float)ms.deltaX;
                ImGui::g_cursorY += (float)ms.deltaY;
                if (ms.buttons & 1) isLeftClick = true;
                if (ms.buttons & 2) isRightClick = true;
                if (ms.wheelDeltaY != 0) {
                    io.AddMouseWheelEvent(0.0f, static_cast<float>(ms.wheelDeltaY));
                }
                s_mouseActivityTimer = 5.0f;
            }
        }

        // 3. Left analog stick mouse cursor emulation
        if (!isTouchActive) {
            float lx = (float)npad.analogStickL[0] / 32767.0f;
            float ly = -(float)npad.analogStickL[1] / 32767.0f;
            if (std::abs(lx) > 0.15f || std::abs(ly) > 0.15f) {
                float speedMult = (npad.buttons & nn::hid::Button::Y) ? 12.0f : 5.0f;
                float frameComp = dt * 60.0f;
                ImGui::g_cursorX += lx * speedMult * frameComp;
                ImGui::g_cursorY += ly * speedMult * frameComp;
            }

            if (!(isZLPressed && isZRPressed)) {
                if (isZLPressed) isLeftClick = true;
                if (isZRPressed) isRightClick = true;
            }
        }

        ImGui::g_cursorX = std::clamp(ImGui::g_cursorX, 0.0f, 1280.0f);
        ImGui::g_cursorY = std::clamp(ImGui::g_cursorY, 0.0f, 720.0f);
    }

    bool isFullWindowOpen = ImGui::g_isMenuOpen || MotionPlayerUi::g_showUltimatePlayer ||
                         MotionPlayerUi::g_showMotionControlWindow || WindControl::g_showWindow;

    // =========================================================================
    // MOUSE CURSOR DISPLAY
    // =========================================================================
    bool shouldDrawCursor = false;
    if (ImGui::g_imguiHasFocus && !isTouchActive) {
        if (isFullWindowOpen) {
            shouldDrawCursor = true;
        }
        // Show cursor if UI is active and mouse was moved
        else if (s_mouseActivityTimer > 0.0f) {
            shouldDrawCursor = true;
        }
    }

    if (anyUiOpen && ImGui::g_imguiHasFocus) {
        io.AddMousePosEvent(ImGui::g_cursorX, ImGui::g_cursorY);
        io.AddMouseButtonEvent(0, isLeftClick);
        io.AddMouseButtonEvent(1, isRightClick);
        io.MouseDrawCursor = shouldDrawCursor;
        ImGui::SetMouseCursor(shouldDrawCursor ? ImGuiMouseCursor_Arrow : ImGuiMouseCursor_None);
    } else {
        io.AddMouseButtonEvent(0, false);
        io.AddMouseButtonEvent(1, false);
        io.MouseDrawCursor = false;
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    }

    ImGui::NewFrame();

    // 1. Main Debug Window
    if (ImGui::g_isMenuOpen) {
        ImGui::SetNextWindowBgAlpha(0.85f);
        std::string title = "Debug UI" + std::string(ImGui::g_imguiHasFocus ? "" : " [GAME HAS FOCUS]") + "###MotionDebugWindow";

        ImGui::Begin(title.c_str(), &ImGui::g_isMenuOpen, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::GetWindowDrawList()->PushClipRectFullScreen();

        // Mot & Wind Control Collapsing Header
        if (ImGui::CollapsingHeader("Mot & Wind Control ")) {
            ImGui::Separator();
            if (ImGui::Button("Motion Control", ImVec2(130.0f, 0.0f))) {
                MotionPlayerUi::g_showMotionControlWindow = !MotionPlayerUi::g_showMotionControlWindow;
            }
            ImGui::SameLine();
            if (ImGui::Button("Wind Control", ImVec2(130.0f, 0.0f))) {
                WindControl::g_showWindow = !WindControl::g_showWindow;
            }

            ImGui::Spacing();
            MotionPlayerUi::DrawRecorderHeader();
        }

        // State Switcher Sub-Menu
        StateSwitcher::Draw();

        // Save backup menu
        SaveBackup::Draw();

        // Extra menu
        ExtraMenu::Draw();

        ImGui::GetWindowDrawList()->PopClipRect();
        ImGui::End();
    }

    // 2. Standalone Windows
    MotionPlayerUi::DrawMotionControlWindow();
    MotionPlayerUi::DrawUltimatePlayerWindow();
    WindControl::DrawWindow();

    // 3. Render All Sub-Overlays
    InputOverlay::Draw();
    MemoryScannerUi::DrawWindow();
    ResScalerUi::DrawWindow();
    PostProcessUi::DrawWindow();
    FpsOverlay::DrawWindow();
    CustomizeSelUi::Draw();
    PluginHelpUi::Draw();
    SongQuickJump::Draw();
    DimOverlay::Draw();
    VolumeOverlay::Draw();
    Hints::Draw();

    VirtualKeyboard::Draw();

    ImGui::Render();
    MotionPlayerUi::ResetPtrCounter();

    ImDrawData* drawData = ImGui::GetDrawData();
    if (!drawData || drawData->CmdListsCount == 0 || drawData->TotalVtxCount == 0) {
        return nullptr;
    }

    return drawData;
}

void MyOnPresent(NVNqueue *queue, NVNwindow *window, int texture_index) {
    static bool s_context_ready = false;
    if (!s_context_ready) {
        nvn_backend::TryConfigureImGuiAllocators();
        nvnImguiInitialize();
        s_context_ready = true;
    }

    ImDrawData* drawData = nvnImguiCalc();

    if (!drawData || drawData->CmdListsCount == 0 || drawData->TotalVtxCount == 0) {
        return;
    }

    static bool s_backend_ready = false;
    if (!s_backend_ready) {
        nvn_hooks::LoadCppProcsOnce();

        ImguiNvnBackend::NvnBackendInitInfo init_info {};
        init_info.device = reinterpret_cast<nvn::Device*>(nvn_hooks::GetDevice());
        init_info.queue  = reinterpret_cast<nvn::Queue*>(queue);

        ImguiNvnBackend::InitBackend(init_info);
        s_backend_ready = true;
    }

    const auto* rt = nvn_hooks::GetSwapchainTextureForBackend(texture_index);
    ImguiNvnBackend::SetRenderTarget(rt);

    ImguiNvnBackend::renderDrawData(drawData);
}
