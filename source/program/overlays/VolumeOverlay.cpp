#include "VolumeOverlay.hpp"
#include "../Sound.hpp"
#include "../Config.hpp"
#include "imgui/imgui.h"
#include <algorithm>
#include <cstdio>

namespace VolumeOverlay {

    bool g_showWindow = false;

    struct ChannelTier {
        int tier = 0;
        bool initialized = false;
    };

    static ChannelTier s_tiers[3]; // 0: BGM, 1: BTN, 2: SE

    void Init() {
        if (Config::bgmBoost < 0) Config::bgmBoost = 0;
        if (Config::btnBoost < 0) Config::btnBoost = 0;
        if (Config::seBoost < 0)  Config::seBoost  = 0;
    }

    void Draw() {
        if (!g_showWindow) return;

        ImGuiIO& io = ImGui::GetIO();
        ImVec2 screenSize = io.DisplaySize;

        const float cardW = 46.0f;
        const float cardH = 216.0f;
        const float cardGap = 8.0f;
        const float totalCardsW = (cardW * 3.0f) + (cardGap * 2.0f);

        const float badgeW = 74.0f;
        const float badgeH = 24.0f;
        const float badgeGapY = 6.0f;

        const float totalOverlayH = badgeH + badgeGapY + cardH;

        const float originX = screenSize.x - totalCardsW - 16.0f;
        const float originY = (screenSize.y - totalOverlayH) * 0.5f;

        ImGui::SetNextWindowPos(ImVec2(originX - 8.0f, originY - 8.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(totalCardsW + 16.0f, totalOverlayH + 16.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0f);

        ImGuiWindowFlags winFlags = ImGuiWindowFlags_NoTitleBar |
                                    ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoScrollbar |
                                    ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_NoBackground;

        if (ImGui::Begin("###VolumeOverlayCardsWindow", nullptr, winFlags)) {
            ImDrawList* draw = ImGui::GetWindowDrawList();

            float badgeX = originX + (totalCardsW - badgeW) * 0.5f;
            float badgeY = originY;

            draw->AddRectFilled(ImVec2(badgeX, badgeY), ImVec2(badgeX + badgeW, badgeY + badgeH),
                                IM_COL32(18, 22, 26, 235), 6.0f);
            draw->AddRect(ImVec2(badgeX, badgeY), ImVec2(badgeX + badgeW, badgeY + badgeH),
                          IM_COL32(255, 255, 255, 35), 6.0f, 0, 1.2f);

            const char* title = "BOOST";
            ImVec2 titleSz = ImGui::CalcTextSize(title);
            draw->AddText(ImVec2(badgeX + (badgeW - titleSz.x) * 0.5f, badgeY + (badgeH - titleSz.y) * 0.5f),
                          IM_COL32(70, 225, 215, 255), title);

            const char* labels[3] = { "BGM", "BTN", "SE" };
            int* boostVals[3] = { &Config::bgmBoost, &Config::btnBoost, &Config::seBoost };
            ImVec4 colors[3] = {
                ImVec4(0.28f, 0.88f, 0.83f, 1.0f), // BGM Cyan
                ImVec4(1.00f, 0.47f, 0.71f, 1.0f), // BTN Pink
                ImVec4(1.00f, 0.85f, 0.35f, 1.0f)  // SE Yellow
            };

            const float cardsBaseY = originY + badgeH + badgeGapY;

            const float btnW = 26.0f;
            const float btnH = 20.0f;
            const float sliderW = 24.0f;
            const float sliderH = 110.0f;

            const float rowHeaderY  = cardsBaseY + 8.0f;
            const float rowPercentY = cardsBaseY + 26.0f;
            const float rowPlusY    = cardsBaseY + 46.0f;
            const float rowSliderY  = cardsBaseY + 70.0f;
            const float rowMinusY   = cardsBaseY + 184.0f;

            for (int i = 0; i < 3; i++) {
                float cX = originX + i * (cardW + cardGap);
                float cY = cardsBaseY;

                draw->AddRectFilled(ImVec2(cX, cY), ImVec2(cX + cardW, cY + cardH),
                                    IM_COL32(18, 22, 26, 235), 8.0f);
                draw->AddRect(ImVec2(cX, cY), ImVec2(cX + cardW, cY + cardH),
                              IM_COL32(255, 255, 255, 30), 8.0f, 0, 1.2f);

                int& totalBoost = *boostVals[i];
                auto& ct = s_tiers[i];

                // BGM limit is 100%, BTN and SE max is 500%
                const int maxBoost = (i == 0) ? 100 : 500;
                if (totalBoost > maxBoost) totalBoost = maxBoost;

                if (!ct.initialized) {
                    ct.tier = (totalBoost >= 100 && totalBoost % 100 == 0) ? (totalBoost / 100 - 1) : (totalBoost / 100);
                    if (ct.tier < 0) ct.tier = 0;
                    ct.initialized = true;
                }

                // For BGM (i == 0), tier is fixed to 0 (single 100% range)
                if (i == 0) {
                    ct.tier = 0;
                } else {
                    while (totalBoost > (ct.tier + 1) * 100 && ct.tier < 4) ct.tier++;
                    while (totalBoost < ct.tier * 100 && ct.tier > 0) ct.tier--;
                }

                ImVec2 lblSz = ImGui::CalcTextSize(labels[i]);
                ImGui::SetCursorScreenPos(ImVec2(cX + (cardW - lblSz.x) * 0.5f, rowHeaderY));
                ImGui::TextColored(colors[i], "%s", labels[i]);

                char valText[16];
                std::snprintf(valText, sizeof(valText), "+%d%%", totalBoost);
                ImVec2 valSz = ImGui::CalcTextSize(valText);
                ImGui::SetCursorScreenPos(ImVec2(cX + (cardW - valSz.x) * 0.5f, rowPercentY));
                ImGui::TextUnformatted(valText);

                ImGui::SetCursorScreenPos(ImVec2(cX + (cardW - btnW) * 0.5f, rowPlusY));
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(colors[i].x * 0.35f, colors[i].y * 0.35f, colors[i].z * 0.35f, 0.75f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors[i]);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);

                char btnPlusId[32];
                std::snprintf(btnPlusId, sizeof(btnPlusId), "+##card_%s", labels[i]);
                if (ImGui::Button(btnPlusId, ImVec2(btnW, btnH))) {
                    if (totalBoost < maxBoost) {
                        totalBoost++;
                        if (i != 0 && totalBoost > (ct.tier + 1) * 100 && ct.tier < 4) {
                            ct.tier++;
                        }
                        SustainAudio::ApplyBoost();
                    }
                }
                ImGui::PopStyleColor(4);
                ImGui::PopStyleVar();

                int baseVal = ct.tier * 100;
                int valInTier = totalBoost - baseVal;
                valInTier = std::clamp(valInTier, 0, 100);

                ImGui::SetCursorScreenPos(ImVec2(cX + (cardW - sliderW) * 0.5f, rowSliderY));
                ImGui::PushStyleColor(ImGuiCol_SliderGrab, colors[i]);
                ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.10f, 0.12f, 0.16f, 0.90f));
                ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.14f, 0.17f, 0.22f, 0.90f));
                ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.18f, 0.22f, 0.28f, 0.90f));
                ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 6.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);

                char sliderId[32];
                std::snprintf(sliderId, sizeof(sliderId), "##slider_card_%s", labels[i]);

                if (ImGui::VSliderInt(sliderId, ImVec2(sliderW, sliderH), &valInTier, 0, 100, "")) {
                    totalBoost = baseVal + valInTier;
                    SustainAudio::ApplyBoost();
                }
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor(5);

                ImGui::SetCursorScreenPos(ImVec2(cX + (cardW - btnW) * 0.5f, rowMinusY));
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(colors[i].x * 0.35f, colors[i].y * 0.35f, colors[i].z * 0.35f, 0.75f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors[i]);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);

                char btnMinusId[32];
                std::snprintf(btnMinusId, sizeof(btnMinusId), "-##card_%s", labels[i]);
                if (ImGui::Button(btnMinusId, ImVec2(btnW, btnH))) {
                    if (totalBoost > 0) {
                        totalBoost--;
                        if (i != 0 && totalBoost <= ct.tier * 100 && ct.tier > 0) {
                            ct.tier--;
                        }
                        SustainAudio::ApplyBoost();
                    }
                }
                ImGui::PopStyleColor(4);
                ImGui::PopStyleVar();
            }
        }
        ImGui::End();
    }

} // namespace VolumeOverlay
