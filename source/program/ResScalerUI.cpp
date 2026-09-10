#include "ResScalerUI.hpp"
#include "Config.hpp"
#include "imgui/imgui_nvn.h"
#include <hid.hpp>
#include <lib.hpp>
#include <cmath>

namespace ResScalerUi {

    bool g_showWindow = false;

    void DrawWindow() {
        if (!g_showWindow) return;

        // Resolve live graphics manager pointer on demand
        uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
        uintptr_t lvar1 = *(uintptr_t*)(base + 0x00cdf890);

        auto applyScaler = [lvar1](float val) {
            Config::resScaler = val;
            if (lvar1) {
                *(float*)(lvar1 + 0x6570907c) = val; // Handheld offset
                *(float*)(lvar1 + 0x65709088) = val; // Docked offset
            }
        };

        ImVec2 screenSize = ImGui::GetIO().DisplaySize;
        // Anchor to bottom-right corner with 12px margin
        ImGui::SetNextWindowPos(ImVec2(screenSize.x - 12.0f, screenSize.y - 12.0f), ImGuiCond_Always, ImVec2(1.0f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.85f);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                                 ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings;

        if (ImGui::Begin("###ResScalerHUDOverlay", nullptr, flags)) {
            ImGui::GetWindowDrawList()->PushClipRectFullScreen();

            // Header showing currently active scale
            ImGui::TextColored(ImVec4(0.28f, 0.88f, 0.83f, 1.0f), "Resolution: %.3fx", Config::resScaler);
            ImGui::Separator();
            ImGui::Spacing();

            const ImVec2 btnSize(48, 26);

            // Helper to render buttons and highlight the active one
            auto DrawScaleBtn = [&](const char* label, float val) {
                bool isActive = std::abs(Config::resScaler - val) < 0.005f;
                if (isActive) {
                    // Highlight active button with Miku teal
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.28f, 0.88f, 0.83f, 0.95f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
                }

                if (ImGui::Button(label, btnSize)) {
                    applyScaler(val);
                }

                if (isActive) {
                    ImGui::PopStyleColor(2);
                }
            };

            // --- ROW 1: 0.3, 0.4, 0.5, 0.6 ---
            DrawScaleBtn("0.3x", 0.300f); ImGui::SameLine();
            DrawScaleBtn("0.4x", 0.400f); ImGui::SameLine();
            DrawScaleBtn("0.5x", 0.500f); ImGui::SameLine();
            DrawScaleBtn("0.6x", 0.600f);

            // --- ROW 2: 0.675, 0.8, 0.9, 1.0 ---
            DrawScaleBtn("0.675x", 0.675f); ImGui::SameLine();
            DrawScaleBtn("0.8x", 0.800f);  ImGui::SameLine();
            DrawScaleBtn("0.9x", 0.900f);  ImGui::SameLine();
            DrawScaleBtn("1.0x", 1.000f);

            ImGui::GetWindowDrawList()->PopClipRect();
        }
        ImGui::End();
    }

} // namespace ResScalerUi
