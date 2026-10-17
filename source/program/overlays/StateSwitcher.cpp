#include "StateSwitcher.hpp"
#include "../DebugMode.hpp"
#include "imgui/imgui.h"
#include <cstddef>
#include <algorithm>
#include <cstdio>

namespace StateSwitcher {

struct SceneEntry {
    int st;
    int sub;
    const char* name;
};

static const SceneEntry allScenes[] = {
    { 0, 0, "DATA_INITIALIZE" },
    { 0, 1, "SYSTEM_STARTUP" },
    { 12, 2, "LOGO" },
    { 12, 3, "TITLE" },
    { 12, 4, "CONCEAL" },
    { 2, 47, "GAME" },
    { 12, 6, "PLAYLIST" },
    { 12, 7, "CAPTURE" },
    { 9, 8, "CS_GALLERY (State 9)" },
    { 3, 8, "DT_MAIN (State 3)" },
    { 3, 9, "DT_MISC" },
    { 3, 10, "DT_OBJ" },
    { 3, 11, "DT_STG" },
    { 3, 12, "DT_MOT" },
    { 3, 13, "DT_COLLISION" },
    { 3, 14, "DT_SPR" },
    { 3, 15, "DT_AET" },
    { 3, 16, "DT_AUTH3D" },
    { 3, 17, "DT_CHR" },
    { 3, 18, "DT_ITEM" },
    { 3, 19, "DT_PERF" },
    { 3, 20, "DT_PVSCRIPT" },
    { 3, 21, "DT_PRINT" },
    { 3, 22, "DT_CARD" },
    { 3, 23, "DT_OPD" },
    { 3, 24, "DT_SLIDER" },
    { 3, 25, "DT_GLITTER" },
    { 3, 26, "DT_GRAPHICS" },
    { 3, 27, "DT_COL_CARD" },
    { 3, 28, "DT_PAD" },
    { 4, 29, "TEST_MODE" },
    { 5, 30, "APP_ERROR" },
    { 3, 31, "UNK_31" },
    { 12, 32, "CS_MENU" },
    { 12, 33, "CS_COMMERCE" },
    { 12, 34, "CS_OPTION_MENU" },
    { 12, 35, "CS_TUTORIAL" },
    { 12, 312, "CS_CUSTOMIZE_SEL" },
    { 12, 37, "CS_TUTORIAL_37" },
    { 12, 38, "CS_GALLERY_ST38" },
    { 12, 39, "UNK_39" },
    { 12, 40, "UNK_40" },
    { 12, 41, "UNK_41" },
    { 12, 42, "MENU_SWITCH_UI" },
    { 12, 43, "UNK_43" },
    { 12, 44, "OPTION_MENU_UI" },
    { 12, 45, "UNK_45" },
    { 12, 46, "UNK_46" }
};

static int s_currentPage = 0;

void Draw() {
    if (ImGui::CollapsingHeader("State Switcher")) {
        ImGui::Separator();

        if (ImGui::Checkbox("Debug Mode Patches", &DebugMode::g_DebugModeEnabled)) {
            DebugMode::ToggleDebugModePatches();
        }

        ImGui::Separator();

        constexpr int total_items    = sizeof(allScenes) / sizeof(allScenes[0]); // 48
        constexpr int items_per_page = 12; // 6 rows x 2 buttons
        constexpr int total_pages    = (total_items + items_per_page - 1) / items_per_page; // 4

        s_currentPage = std::clamp(s_currentPage, 0, total_pages - 1);

        int startIdx = s_currentPage * items_per_page;
        int endIdx   = std::min(startIdx + items_per_page, total_items);

        // 1. Grid of 2 columns
        for (int i = startIdx; i < endIdx; i++) {
            if ((i - startIdx) % 2 != 0) {
                ImGui::SameLine();
            }

            if (ImGui::Button(allScenes[i].name, ImVec2(130.0f, 26.0f))) {
                DebugMode::RequestStateChange(
                    static_cast<DebugMode::GameState>(allScenes[i].st),
                    static_cast<DebugMode::GameSubState>(allScenes[i].sub)
                );
            }
        }

        ImGui::Spacing();
        ImGui::Separator();

        // 2. Navigation bar
        float startX    = ImGui::GetCursorPosX();
        float gridWidth = 130.0f * 2.0f + ImGui::GetStyle().ItemSpacing.x;
        float btnW      = 34.0f;

        if (ImGui::Button("<##PrevPage", ImVec2(btnW, 24.0f))) {
            if (s_currentPage > 0) s_currentPage--;
        }

        char pageText[32];
        snprintf(pageText, sizeof(pageText), "Page %d / %d", s_currentPage + 1, total_pages);
        float textWidth = ImGui::CalcTextSize(pageText).x;

        ImGui::SameLine();
        ImGui::SetCursorPosX(startX + (gridWidth - textWidth) * 0.5f);
        ImGui::Text("%s", pageText);

        ImGui::SameLine();
        ImGui::SetCursorPosX(startX + gridWidth - btnW);
        if (ImGui::Button(">##NextPage", ImVec2(btnW, 24.0f))) {
            if (s_currentPage < total_pages - 1) s_currentPage++;
        }
    }
}

} // namespace StateSwitcher
