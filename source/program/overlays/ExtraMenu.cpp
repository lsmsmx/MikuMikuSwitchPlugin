#include "ExtraMenu.hpp"
#include "imgui/imgui.h"
#include <algorithm>

#include "InputOverlay.hpp"
#include "MemoryScannerUi.hpp"
#include "ResScalerUI.hpp"
#include "PostProcessUI.hpp"
#include "FpsOverlay.hpp"
#include "DimOverlay.hpp"
#include "VolumeOverlay.hpp"
#include "MotionPlayerUi.hpp"

namespace ImGui {
    extern bool g_imguiHasFocus;
}

namespace ExtraMenu {

// Helper button that ensures balanced style color stack
static void DrawOverlayToggleButton(const char* label, bool& state, float btnW, float btnH = 26.0f) {
    bool wasActive = state;

    if (wasActive) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.45f, 0.40f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.58f, 0.52f, 1.00f));
    }

    if (ImGui::Button(label, ImVec2(btnW, btnH))) {
        state = !state;
        if (state) {
            ImGui::g_imguiHasFocus = true;
        }
    }

    if (wasActive) {
        ImGui::PopStyleColor(2);
    }
}

void Draw() {
    if (ImGui::CollapsingHeader("Extra Overlays")) {
        ImGui::Separator();

        const char* modeNames[] = { "Disabled", "Gamepad Overlay", "Keyboard Overlay" };
        int curMode = std::clamp(InputOverlay::GetMode(), 0, 2);

        ImGui::Text("Active Input Overlay:");
        ImGui::PushItemWidth(180);
        if (ImGui::BeginCombo("##ActiveInputOverlayCombo", modeNames[curMode])) {
            ImGui::GetWindowDrawList()->PushClipRectFullScreen();
            for (int n = 0; n < 3; n++) {
                bool is_selected = (curMode == n);
                if (ImGui::Selectable(modeNames[n], is_selected)) {
                    InputOverlay::SetMode(n);
                }
                if (is_selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::GetWindowDrawList()->PopClipRect();
            ImGui::EndCombo();
        }
        ImGui::PopItemWidth();

        ImGui::Spacing();
        ImGui::Separator();

        float totalAvailW = ImGui::GetContentRegionAvail().x;
        float spacing     = ImGui::GetStyle().ItemSpacing.x;
        float btnW        = (totalAvailW - spacing) * 0.5f;

        ImGui::TextColored(ImVec4(0.30f, 0.85f, 0.80f, 1.0f), "Quick Open Overlays:");

        DrawOverlayToggleButton(ResScalerUi::g_showWindow ? "Close ResScaler" : "Open ResScaler", ResScalerUi::g_showWindow, btnW);
        ImGui::SameLine();
        DrawOverlayToggleButton(PostProcessUi::g_showWindow ? "Close PostProcess" : "Open PostProcess", PostProcessUi::g_showWindow, btnW);

        DrawOverlayToggleButton(DimOverlay::g_showWindow ? "Close Dim UI" : "Open Dim UI", DimOverlay::g_showWindow, btnW);
        ImGui::SameLine();
        DrawOverlayToggleButton(VolumeOverlay::g_showWindow ? "Close Volume UI" : "Open Volume UI", VolumeOverlay::g_showWindow, btnW);

        DrawOverlayToggleButton(FpsOverlay::g_showWindow ? "Close FPS" : "Open FPS", FpsOverlay::g_showWindow, btnW);
        ImGui::SameLine();
        DrawOverlayToggleButton(MemoryScannerUi::g_showWindow ? "Close BSS Gaps" : "Open BSS Gaps", MemoryScannerUi::g_showWindow, btnW);

        DrawOverlayToggleButton(MotionPlayerUi::g_showUltimatePlayer ? "Close Motion Player" : "Open Motion Player", MotionPlayerUi::g_showUltimatePlayer, totalAvailW);

        ImGui::Spacing();
        ImGui::Separator();

        ImGui::TextColored(ImVec4(1.00f, 0.47f, 0.71f, 1.0f), "Hotkey Quick Reference:");

        ImGui::BulletText("ResScaler & PostProcess: Hold L3 + R3 + ZR");
        ImGui::BulletText("Volume Booster: Hold L3 + R3 + R");
        ImGui::BulletText("Dim Screen (Darkness): Hold L3 + R3 + L");
        ImGui::BulletText("FPS Counter: Hold L3 + R3 + Plus");
        ImGui::BulletText("BSS Gaps Memory Scanner: Hold L3 + R3 + ZL");
        ImGui::BulletText("Input Display: Hold L3 + R3 (Toggle) / F10");
        ImGui::BulletText("Main Debug Menu: Hold Plus + Minus (0.8s)");
    }
}

} // namespace ExtraMenu
