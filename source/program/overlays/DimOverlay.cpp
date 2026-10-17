#include "DimOverlay.hpp"
#include "../Config.hpp"
#include "imgui/imgui.h"
#include <lib.hpp>

namespace DimOverlay {

    bool g_showWindow = false;

    void Draw() {
        if (!g_showWindow) return;

        ImVec2 screenSize = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(16.0f, screenSize.y * 0.5f), ImGuiCond_Always, ImVec2(0.0f, 0.5f));
        ImGui::SetNextWindowBgAlpha(0.85f);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                                 ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);

        if (ImGui::Begin("###DimOverlayWindow", nullptr, flags)) {
            ImGui::GetWindowDrawList()->PushClipRectFullScreen();

            ImGui::TextColored(ImVec4(0.28f, 0.88f, 0.83f, 1.0f), "Dim");

            // Treat -1 as 0% for UI display
            int displayDim = (Config::dim >= 0) ? Config::dim : 0;
            ImGui::Text("%d%%", displayDim);

            ImVec2 sliderSize(26.0f, 150.0f);

            ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(1.00f, 0.47f, 0.71f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(1.00f, 0.60f, 0.80f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.15f, 0.19f, 0.90f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.16f, 0.20f, 0.25f, 0.95f));

            int sliderVal = displayDim;
            if (ImGui::VSliderInt("##DimSlider", sliderSize, &sliderVal, 0, 100, "")) {
                Config::dim = sliderVal;
            }

            ImGui::PopStyleColor(4);
            ImGui::GetWindowDrawList()->PopClipRect();
        }
        ImGui::End();

        ImGui::PopStyleVar(3);
    }
}
