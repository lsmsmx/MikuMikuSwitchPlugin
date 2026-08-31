#include "StateSwitcher.hpp"
#include "DebugMode.hpp"
#include "imgui/imgui_nvn.h"
#include <cstddef>

namespace StateSwitcher {

struct SceneEntry {
    int st;
    int sub;
    const char* name;
};

static const SceneEntry allScenes[] = {
    { 0, 0, "0: DATA_INITIALIZE" },
    { 0, 1, "1: SYSTEM_STARTUP" },
    { 9, 2, "2: LOGO" },
    { 9, 3, "3: TITLE" },
    { 9, 4, "4: CONCEAL" },
    { 9, 5, "5: GAME" },
    { 9, 6, "6: PLAYLIST" },
    { 9, 7, "7: CAPTURE" },
    { 9, 8, "8: CS_GALLERY (State 9)" },
    { 3, 8, "8: DT_MAIN (State 3)" },
    { 3, 9, "9: DT_MISC" },
    { 3, 10, "10: DT_OBJ" },
    { 3, 11, "11: DT_STG" },
    { 3, 12, "12: DT_MOT" },
    { 3, 13, "13: DT_COLLISION" },
    { 3, 14, "14: DT_SPR" },
    { 3, 15, "15: DT_AET" },
    { 3, 16, "16: DT_AUTH3D" },
    { 3, 17, "17: DT_CHR" },
    { 3, 18, "18: DT_ITEM" },
    { 3, 19, "19: DT_PERF" },
    { 3, 20, "20: DT_PVSCRIPT" },
    { 3, 21, "21: DT_PRINT" },
    { 3, 22, "22: DT_CARD" },
    { 3, 23, "23: DT_OPD" },
    { 3, 24, "24: DT_SLIDER" },
    { 3, 25, "25: DT_GLITTER" },
    { 3, 26, "26: DT_GRAPHICS" },
    { 3, 27, "27: DT_COL_CARD" },
    { 3, 28, "28: DT_PAD" },
    { 4, 29, "29: TEST_MODE" },
    { 5, 30, "30: APP_ERROR" },
    { 3, 31, "31: UNK_31" },
    { 6, 32, "32: CS_MENU" },
    { 6, 33, "33: CS_COMMERCE" },
    { 6, 34, "34: CS_OPTION_MENU" },
    { 6, 35, "35: CS_TUTORIAL" },
    { 6, 36, "36: CS_CUSTOMIZE_SEL" },
    { 6, 37, "37: CS_TUTORIAL_37" },
    { 6, 38, "38: CS_GALLERY_ST38" },
    { 6, 39, "39: UNK_39" },
    { 6, 40, "40: UNK_40" },
    { 6, 41, "41: UNK_41" },
    { 6, 42, "42: MENU_SWITCH_UI" },
    { 6, 43, "43: UNK_43" },
    { 6, 44, "44: OPTION_MENU_UI" },
    { 6, 45, "45: UNK_45" },
    { 6, 46, "46: UNK_46" }
};

void Draw() {
    if (ImGui::CollapsingHeader("State Switcher")) {
        ImGui::Separator();

        ImGui::BeginChild("StateScrollRegion", ImVec2(390, 220), true);
        ImGui::GetWindowDrawList()->PushClipRectFullScreen();

        constexpr size_t count = sizeof(allScenes) / sizeof(allScenes[0]);
        for (size_t i = 0; i < count; i++) {
            if (i % 2 != 0) ImGui::SameLine();

            if (ImGui::Button(allScenes[i].name, ImVec2(180, 26))) {
                DebugMode::RequestStateChange(
                    static_cast<DebugMode::GameState>(allScenes[i].st),
                    static_cast<DebugMode::GameSubState>(allScenes[i].sub)
                );
            }
        }

        ImGui::GetWindowDrawList()->PopClipRect();
        ImGui::EndChild();
    }
}

}
