#include "PostProcessUI.hpp"
#include "Config.hpp"
#include "imgui/imgui_nvn.h"
#include <hid.hpp>
#include <lib.hpp>
#include <algorithm>

namespace PostProcessUi {

    bool g_showWindow = false;

    void DrawWindow() {
        if (!g_showWindow) return;

        uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;

        // Resolve live Render singleton (0x0049bbc0)
        auto render_get = reinterpret_cast<uintptr_t(*)()>(base + 0x0049bbc0);
        uintptr_t rend = render_get ? render_get() : 0;

        // Engine setters
        auto set_tone_map_method = reinterpret_cast<void(*)(uintptr_t, int32_t)>(base + 0x004a0120);
        auto set_exposure        = reinterpret_cast<void(*)(float, uintptr_t)>(base + 0x004a0130);
        auto set_gamma           = reinterpret_cast<void(*)(uintptr_t, float)>(base + 0x004a0150);

        // Safe resolution of post-process context (offset +0x1a40)
        uintptr_t ctx = rend ? *reinterpret_cast<uintptr_t*>(rend + 0x1a40) : 0;

        // Query live engine values
        int   liveToneMap = rend ? *reinterpret_cast<int32_t*>(rend + 0x19c4) : 0;
        float liveExp     = rend ? *reinterpret_cast<float*>(rend + 0x19c8)   : 1.0f;
        float liveGamma   = rend ? *reinterpret_cast<float*>(rend + 0x19cc)   : 1.0f;
        float liveSat     = rend ? *reinterpret_cast<float*>(rend + 0x17f4)   : 1.0f;
        float livePse     = ctx  ? *reinterpret_cast<float*>(ctx + 0xb70)     : 0.49f;

        int   curToneMap = (Config::toneMapMethod >= 0)  ? Config::toneMapMethod : liveToneMap;
        float curExp     = (Config::exposure >= 0.0f)     ? Config::exposure      : liveExp;
        float curGamma   = (Config::gamma >= 0.0f)        ? Config::gamma         : liveGamma;
        float curSat     = (Config::saturateCoef >= 0.0f) ? Config::saturateCoef  : liveSat;
        float curPse     = (Config::exposurePse >= 0.0f)  ? Config::exposurePse   : livePse;

        // Anchor window to bottom-left corner with 12px margin
        ImVec2 screenSize = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(12.0f, screenSize.y - 12.0f), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.85f);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                                 ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings;

        // Compact UI styling
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(7.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 3.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));

        if (ImGui::Begin("###PostProcessHUDOverlay", nullptr, flags)) {
            ImGui::GetWindowDrawList()->PushClipRectFullScreen();

            // Header line: Title on the left, single Reset button [R] on the right
            ImGui::TextColored(ImVec4(0.28f, 0.88f, 0.83f, 1.0f), "Post Process Tuning");
            ImGui::SameLine(ImGui::GetWindowWidth() - 32.0f);

            // Single unified Reset [R] button
            if (ImGui::Button("R", ImVec2(24, 19))) {
                // 1. Release overrides in Config
                Config::toneMapMethod = -1;
                Config::exposure      = -1.0f;
                Config::gamma         = -1.0f;
                Config::saturateCoef  = -1.0f;
                Config::exposurePse   = -1.0f;

                // 2. Immediately force vanilla defaults directly back into game memory!
                if (rend) {
                    if (set_tone_map_method) set_tone_map_method(rend, 0);    // 0 = YCC EXP
                    if (set_exposure)        set_exposure(2.0f, rend);        // Triggers engine exposure pass
                    if (set_gamma)           set_gamma(rend, 1.0f);           // Triggers engine gamma pass
                    // Reset Saturation 2 to 1.0
                    *reinterpret_cast<float*>(rend + 0x17f4) = 1.0f;
                    *reinterpret_cast<int32_t*>(rend + 0x1288) = 1;
                    *reinterpret_cast<int32_t*>(rend + 0x1a3c) = 1;

                    if (ctx) {
                        *reinterpret_cast<float*>(ctx + 0xb70) = 1.0f; // Default PSE exposure scale
                    }
                }
            }

            ImGui::Separator();

            const float itemWidth = 110.0f;

            // 1. Tone Map Method
            const char* toneNames[] = { "YCC EXP", "RGB LIN", "RGB LIN2", "OFF" };
            ImGui::PushItemWidth(itemWidth);
            if (ImGui::BeginCombo("Tone Map", toneNames[std::clamp(curToneMap, 0, 3)])) {
                ImGui::GetWindowDrawList()->PushClipRectFullScreen();

                for (int n = 0; n < 4; n++) {
                    bool is_selected = (curToneMap == n);
                    if (ImGui::Selectable(toneNames[n], is_selected)) {
                        Config::toneMapMethod = n;
                        if (rend && set_tone_map_method) {
                            set_tone_map_method(rend, Config::toneMapMethod);
                        }
                    }
                    if (is_selected) ImGui::SetItemDefaultFocus();
                }

                ImGui::GetWindowDrawList()->PopClipRect();
                ImGui::EndCombo();
            }
            ImGui::PopItemWidth();

            // 2. Exposure (0.0f - 5.0f)
            ImGui::PushItemWidth(itemWidth);
            if (ImGui::SliderFloat("Exp", &curExp, 0.0f, 5.0f, "%.2f")) {
                Config::exposure = curExp;
                if (rend && set_exposure) {
                    set_exposure(Config::exposure, rend);
                }
            }
            ImGui::PopItemWidth();

            // 3. Gamma (0.0f - 3.0f)
            ImGui::PushItemWidth(itemWidth);
            if (ImGui::SliderFloat("Gamma", &curGamma, 0.0f, 3.0f, "%.2f")) {
                Config::gamma = curGamma;
                if (rend && set_gamma) {
                    set_gamma(rend, Config::gamma);
                }
            }
            ImGui::PopItemWidth();

            // 4. Saturate Coef (0.0f - 1.0f)
            ImGui::PushItemWidth(itemWidth);
            if (ImGui::SliderFloat("Sat Coef", &curSat, 0.0f, 1.0f, "%.2f")) {
                Config::saturateCoef = curSat;
                if (rend) {
                    *reinterpret_cast<float*>(rend + 0x17f4) = Config::saturateCoef;
                    *reinterpret_cast<int32_t*>(rend + 0x1288) = 1;
                    *reinterpret_cast<int32_t*>(rend + 0x1a3c) = 1;
                }
            }
            ImGui::PopItemWidth();

            // 5. Exp PSE (0.0f - 1.0f)
            ImGui::PushItemWidth(itemWidth);
            if (ImGui::SliderFloat("Exp PSE", &curPse, 0.0f, 1.0f, "%.3f")) {
                Config::exposurePse = curPse;
                if (ctx) {
                    *reinterpret_cast<float*>(ctx + 0xb70) = Config::exposurePse;
                    *reinterpret_cast<int32_t*>(rend + 0x1288) = 1;
                }
            }
            ImGui::PopItemWidth();

            ImGui::GetWindowDrawList()->PopClipRect();
        }
        ImGui::End();

        ImGui::PopStyleVar(3);
    }

} // namespace PostProcessUi
