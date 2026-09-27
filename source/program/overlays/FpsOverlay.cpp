#include "FpsOverlay.hpp"
#include "imgui/imgui.h"

namespace FpsOverlay {

    bool g_showWindow = false;

    void DrawWindow() {
        if (!g_showWindow) return;

        static float s_timer       = 0.0f;
        static int   s_frameCount  = 0;
        static float s_displayFps  = 60.0f;

        s_timer += ImGui::GetIO().DeltaTime;
        s_frameCount++;

        if (s_timer >= 0.25f) {
            s_displayFps = (float)s_frameCount / s_timer;
            s_frameCount = 0;
            s_timer = 0.0f;
        }

        ImVec4 fpsColor;
        if (s_displayFps >= 59.0f) {
            fpsColor = ImVec4(0.28f, 0.88f, 0.83f, 1.0f); // Miku Teal
        } else if (s_displayFps >= 29.5f) {
            fpsColor = ImVec4(1.0f, 0.85f, 0.20f, 1.0f);  // Yellow
        } else {
            fpsColor = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);  // Red
        }

        ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Always, ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.90f);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                                 ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoFocusOnAppearing |
                                 ImGuiWindowFlags_NoNav |
                                 ImGuiWindowFlags_NoInputs;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.28f, 0.88f, 0.83f, 0.40f));

        if (ImGui::Begin("###FpsHUDOverlay", nullptr, flags)) {
            ImGui::GetWindowDrawList()->PushClipRectFullScreen();

            ImGui::SetWindowFontScale(2.0f);

            ImGui::TextColored(fpsColor, "%.1f", s_displayFps);

            ImGui::SetWindowFontScale(1.0f);
            ImGui::GetWindowDrawList()->PopClipRect();
        }
        ImGui::End();

        ImGui::PopStyleColor(1);
        ImGui::PopStyleVar(3);
    }

} // namespace FpsOverlay
