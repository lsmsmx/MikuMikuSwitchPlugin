#include "customize_sel.hpp"
#include "imgui/imgui.h"

#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

// Engine and project headers
#include "../diva_nc.hpp"
#include "../nc_state.hpp"
#include "../sound_db.hpp"
#include "../save_data.hpp"
#include "../input.hpp"
#include "../util.hpp"
#include "../game/sound_effects.hpp"
#include "../game/tech_zone.hpp"

namespace CustomizeSelUi
{
    constexpr int32_t PreviewQueueIndex = 3;
    constexpr int32_t SFXQueueIndex = 1;

    constexpr float DialogW = 680.0f;
    constexpr float DialogH = 440.0f;
    constexpr float CanvasW = 1280.0f;
    constexpr float CanvasH = 720.0f;

    constexpr float AnimSpeedIn  = 8.0f;
    constexpr float AnimSpeedOut = 9.0f;

    static AnimState g_animState = AnimState::Closed;
    static float g_animProgress  = 0.0f;

    static bool s_assetsLoaded = false;
    static bool s_inCustomizeScene = false;

    constexpr int32_t TotalPages = 3;
    static int32_t g_currentPage = 0;
    static int32_t g_selectedItem = 0;

    static float g_pulseTimer = 0.0f;

    bool IsOpen()
    {
        return g_animState != AnimState::Closed;
    }

    void Open()
    {
        if (!s_assetsLoaded)
            return;

        if (g_animState == AnimState::Closed || g_animState == AnimState::Closing)
        {
            g_animState = AnimState::Opening;
            nc::BlockInputs();
            sound::PlaySoundEffect(SFXQueueIndex, "se_ft_sys_dialog_open", 1.0f);
        }
    }

    void Close()
    {
        if (g_animState == AnimState::Open || g_animState == AnimState::Opening)
        {
            g_animState = AnimState::Closing;
            sound::PlaySoundEffect(SFXQueueIndex, "se_ft_sys_dialog_close", 1.0f);
            nc::SaveSaveDataNC();
            sound::ReleaseAllCues(PreviewQueueIndex);
        }
    }

    void ForceClose()
    {
        g_animState = AnimState::Closed;
        g_animProgress = 0.0f;
        nc::UnblockInputs();
    }

    void Toggle()
    {
        if (IsOpen())
            Close();
        else
            Open();
    }

    static void PlayPreviewCue(const std::string& se_name)
    {
        if (!se_name.empty())
        {
            sound::ReleaseAllCues(PreviewQueueIndex);
            sound::PlaySoundEffect(PreviewQueueIndex, se_name.c_str(), 1.0f);
        }
    }

    static void PlayCurrentPreview()
    {
        ConfigSet* cfg = nc::GetConfigSet();
        if (!cfg) return;

        std::string se_name;

        if (g_currentPage == 0)
        {
            auto resolveAndPlay = [cfg](int8_t current_id, const std::vector<SoundInfo>& db, int32_t same_id)
            {
                std::string cue;
                if (current_id == -1)
                {
                    if (same_id == 1)
                        cue = sound_effects::GetGameSoundEffect(0);
                    else if (same_id == 2)
                    {
                        const auto* star_snd = util::FindWithID(*sound_db::GetStarSoundDB(), cfg->star_se_id);
                        if (star_snd) cue = star_snd->se_name;
                    }
                }
                else
                {
                    const auto* snd = util::FindWithID(db, current_id);
                    if (snd)
                    {
                        cue = !snd->se_preview_name.empty() ? snd->se_preview_name : snd->se_name;
                    }
                }
                PlayPreviewCue(cue);
            };

            switch (g_selectedItem)
            {
            case 0: resolveAndPlay(cfg->button_l_se_id, *sound_db::GetButtonLongOnSoundDB(), 1); break;
            case 1: resolveAndPlay(cfg->button_w_se_id, *sound_db::GetButtonWSoundDB(),      1); break;
            case 2: resolveAndPlay(cfg->star_se_id,     *sound_db::GetStarSoundDB(),         0); break;
            case 3: resolveAndPlay(cfg->link_se_id,     *sound_db::GetLinkSoundDB(),         2); break;
            case 4: resolveAndPlay(cfg->star_w_se_id,   *sound_db::GetStarWSoundDB(),        2); break;
            }
        }
        else if (g_currentPage == 2 && g_selectedItem == 1)
        {
            if (nc::GetSharedData().stick_control_se == 0)
                se_name = sound_effects::GetGameSoundEffect(3);
            else
            {
                const auto* snd = util::FindWithID(*sound_db::GetStarSoundDB(), nc::GetConfigSet()->star_se_id);
                if (snd) se_name = snd->se_name;
            }
            PlayPreviewCue(se_name);
        }
    }

    static int32_t GetMaxItemsForPage(int32_t page)
    {
        switch (page)
        {
        case 0: return 5;
        case 1: return 2;
        case 2: return 4;
        default: return 1;
        }
    }

    static void ChangeOptionValue(int32_t dir)
    {
        ConfigSet* cfg = nc::GetConfigSet();
        SharedData& shared = nc::GetSharedData();
        if (!cfg) return;

        bool changed = false;

        if (g_currentPage == 0)
        {
            auto cycleSound = [&](int8_t* val, int8_t* val_alias, const std::vector<SoundInfo>& db, int32_t same_id)
            {
                std::vector<int8_t> ids;
                if (same_id == 1 || same_id == 2) ids.push_back(-1);
                for (const auto& s : db) ids.push_back(s.id);

                int cur = 0;
                for (size_t i = 0; i < ids.size(); i++) {
                    if (ids[i] == *val) { cur = static_cast<int>(i); break; }
                }
                cur = util::Wrap(cur + dir, 0, static_cast<int>(ids.size()) - 1);
                *val = ids[cur];
                if (val_alias) *val_alias = *val;
                changed = true;
            };

            switch (g_selectedItem)
            {
            case 0: cycleSound(&cfg->button_l_se_id, &cfg->rush_se_id, *sound_db::GetButtonLongOnSoundDB(), 1); break;
            case 1: cycleSound(&cfg->button_w_se_id, nullptr,          *sound_db::GetButtonWSoundDB(),      1); break;
            case 2: cycleSound(&cfg->star_se_id,     nullptr,          *sound_db::GetStarSoundDB(),         0); break;
            case 3: cycleSound(&cfg->link_se_id,     nullptr,          *sound_db::GetLinkSoundDB(),         2); break;
            case 4: cycleSound(&cfg->star_w_se_id,   nullptr,          *sound_db::GetStarWSoundDB(),        2); break;
            }
        }
        else if (g_currentPage == 1)
        {
            if (g_selectedItem == 0)
            {
                int32_t cur = cfg->tech_zone_style;
                cur = util::Wrap(cur + dir, 0, 2);
                cfg->tech_zone_style = (int8_t)cur;
                shared.tech_zone_style = cur;
                changed = true;
            }
            else if (g_selectedItem == 1)
            {
                int32_t* tzPrio = reinterpret_cast<int32_t*>(&shared.reserved[4]);
                int32_t styles[] = { 0, 1, 2, 3, 6, 20 };
                constexpr int num = 6;
                int cur = 1;
                for (int i = 0; i < num; i++) {
                    if (styles[i] == *tzPrio) { cur = i; break; }
                }
                cur = util::Wrap(cur + dir, 0, num - 1);
                *tzPrio = styles[cur];
                changed = true;
            }
        }
        else if (g_currentPage == 2)
        {
            switch (g_selectedItem)
            {
            case 0:
                shared.stick_sensitivity = std::clamp(shared.stick_sensitivity + (dir * 5), 20, 80);
                changed = true;
                break;
            case 1:
                shared.stick_control_se = util::Wrap<uint8_t>(shared.stick_control_se + dir, 0, 1);
                changed = true;
                break;
            case 2:
                shared.sound_prio = (shared.sound_prio > 0) ? 0 : 2;
                changed = true;
                break;
            case 3:
            {
                int32_t* starCtrl = reinterpret_cast<int32_t*>(&shared.reserved[0]);
                *starCtrl = util::Wrap(*starCtrl + dir, 0, 2);
                changed = true;
                break;
            }
            }
        }

        if (changed) {
            sound::ReleaseAllCues(PreviewQueueIndex);
            sound::PlaySoundEffect(SFXQueueIndex, "se_ft_music_selector_select_01", 1.0f);
        }
    }

    void Update()
    {
        diva_nc::InputState* is = diva_nc::GetInputState(0);
        if (!is) return;

        bool zlTapped = is->IsButtonTappedAbs(92) || is->IsButtonTappedAbs(13);

        if (g_animState == AnimState::Closed)
        {
            if (zlTapped) Open();
            return;
        }

        nc::BlockInputs();

        bool cancelTapped = is->IsButtonTappedAbs(9);
        if (zlTapped || cancelTapped)
        {
            Close();
            return;
        }

        if (g_animState != AnimState::Open) return;

        if (is->IsButtonTappedAbs(11))
        {
            g_currentPage = util::Wrap(g_currentPage - 1, 0, TotalPages - 1);
            g_selectedItem = std::clamp(g_selectedItem, 0, GetMaxItemsForPage(g_currentPage) - 1);
            sound::PlaySelectSE();
        }
        else if (is->IsButtonTappedAbs(12))
        {
            g_currentPage = util::Wrap(g_currentPage + 1, 0, TotalPages - 1);
            g_selectedItem = std::clamp(g_selectedItem, 0, GetMaxItemsForPage(g_currentPage) - 1);
            sound::PlaySelectSE();
        }

        int32_t maxItems = GetMaxItemsForPage(g_currentPage);
        if (is->IsButtonTappedAbs(3) || nc::IsButtonTappedOrRepeat(is, 3))
        {
            g_selectedItem = util::Wrap(g_selectedItem - 1, 0, maxItems - 1);
            sound::PlaySelectSE();
        }
        else if (is->IsButtonTappedAbs(4) || nc::IsButtonTappedOrRepeat(is, 4))
        {
            g_selectedItem = util::Wrap(g_selectedItem + 1, 0, maxItems - 1);
            sound::PlaySelectSE();
        }

        if (is->IsButtonTappedAbs(5) || nc::IsButtonTappedOrRepeat(is, 5))
            ChangeOptionValue(-1);
        else if (is->IsButtonTappedAbs(6) || nc::IsButtonTappedOrRepeat(is, 6))
            ChangeOptionValue(1);

        if (is->IsButtonTappedAbs(10) || is->IsButtonTappedAbs(7))
            PlayCurrentPreview();
    }

    static void DrawBoldText(ImDrawList* drawList, ImVec2 pos, ImU32 col, const char* text)
    {
        drawList->AddText(pos, col, text);
        drawList->AddText(ImVec2(pos.x + 1.0f, pos.y), col, text);
        drawList->AddText(ImVec2(pos.x, pos.y + 0.5f), col, text);
    }

    void Draw()
    {
        if (g_animState == AnimState::Closed) return;

        ImGuiIO& io = ImGui::GetIO();
        float dt = io.DeltaTime > 0.0f ? io.DeltaTime : (1.0f / 60.0f);
        g_pulseTimer += dt * 4.0f;

        if (g_animState == AnimState::Opening)
        {
            g_animProgress += dt * AnimSpeedIn;
            if (g_animProgress >= 1.0f)
            {
                g_animProgress = 1.0f;
                g_animState = AnimState::Open;
            }
        }
        else if (g_animState == AnimState::Closing)
        {
            g_animProgress -= dt * AnimSpeedOut;
            if (g_animProgress <= 0.0f)
            {
                ForceClose();
                return;
            }
        }

        float alpha = std::clamp(g_animProgress, 0.0f, 1.0f);
        float scale = 0.88f + 0.12f * std::sin(alpha * 1.5707963f);

        float curW = DialogW * scale;
        float curH = DialogH * scale;
        ImVec2 center((CanvasW - curW) * 0.5f, (CanvasH - curH) * 0.5f);

        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        ImGui::SetNextWindowPos(center, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(curW, curH), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0f);

        ImGuiWindowFlags winFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;

        if (ImGui::Begin("##NC_CustomPillWindow", nullptr, winFlags))
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRectFullScreen();

            drawList->AddRectFilled(center, ImVec2(center.x + curW, center.y + curH), IM_COL32(245, 252, 252, (int)(245.0f * alpha)), 14.0f);
            drawList->AddRect(center, ImVec2(center.x + curW, center.y + curH), IM_COL32(0, 206, 209, (int)(220.0f * alpha)), 14.0f, 0, 2.5f);

            float topH = 38.0f * scale;
            drawList->AddRectFilled(center, ImVec2(center.x + curW, center.y + topH), IM_COL32(0, 150, 180, (int)(250.0f * alpha)), 14.0f, ImDrawFlags_RoundCornersTop);

            const char* topTitle = "New Classics Options";
            ImGui::SetWindowFontScale(1.35f);
            ImVec2 topTextSz = ImGui::CalcTextSize(topTitle);
            DrawBoldText(drawList, ImVec2(center.x + (curW - topTextSz.x) * 0.5f, center.y + 7.0f), IM_COL32(255, 255, 255, (int)(255.0f * alpha)), topTitle);
            ImGui::SetWindowFontScale(1.0f);

            int32_t targetId = nc::GetConfigSetID();
            std::string slotBadge;
            if (targetId == -1)      slotBadge = "[ Preset 1 (Slot A) ]";
            else if (targetId == -2) slotBadge = "[ Preset 2 (Slot B) ]";
            else if (targetId == -3) slotBadge = "[ Preset 3 (Slot C) ]";
            else if (targetId > 0)   slotBadge = util::Format("[ Song Specific (PV %d) ]", targetId);
            else                     slotBadge = util::Format("[ Config #%d ]", targetId);

            ImVec2 slotSz = ImGui::CalcTextSize(slotBadge.c_str());
            drawList->AddText(ImVec2(center.x + (curW - slotSz.x) * 0.5f, center.y + 44.0f), IM_COL32(0, 130, 155, (int)(240.0f * alpha)), slotBadge.c_str());

            const char* bannerTitles[TotalPages] = { "Sound Config", "Technical Zone Settings", "Other Settings" };
            const char* subTitles[TotalPages] = {
                "This page allows you to modify the audio settings for exclusive console buttons.",
                "This page lets you change the visuals and display of Tech Zones.",
                "This page lets you adjust sensitivity, visuals, delay and control SE."
            };

            float ribbonW = 340.0f * scale;
            float ribbonH = 26.0f * scale;
            ImVec2 ribbonPos(center.x + (curW - ribbonW) * 0.5f, center.y + 64.0f);
            drawList->AddRectFilled(ribbonPos, ImVec2(ribbonPos.x + ribbonW, ribbonPos.y + ribbonH), IM_COL32(24, 88, 160, (int)(235.0f * alpha)), 8.0f);

            ImGui::SetWindowFontScale(1.15f);
            ImVec2 bTextSz = ImGui::CalcTextSize(bannerTitles[g_currentPage]);
            DrawBoldText(drawList, ImVec2(ribbonPos.x + (ribbonW - bTextSz.x) * 0.5f, ribbonPos.y + 4.0f), IM_COL32(255, 255, 255, (int)(255.0f * alpha)), bannerTitles[g_currentPage]);
            ImGui::SetWindowFontScale(1.0f);

            ImVec2 subTextSz = ImGui::CalcTextSize(subTitles[g_currentPage]);
            drawList->AddText(ImVec2(center.x + (curW - subTextSz.x) * 0.5f, center.y + 94.0f), IM_COL32(70, 90, 100, (int)(220.0f * alpha)), subTitles[g_currentPage]);

            ConfigSet* cfg = nc::GetConfigSet();
            SharedData& shared = nc::GetSharedData();

            struct ItemRow { std::string label, value, help_line1, help_line2; };
            std::vector<ItemRow> rows;

            if (g_currentPage == 0 && cfg)
            {
                auto getSoundName = [](int8_t id, const std::vector<SoundInfo>& db, int32_t same_id) -> std::string
                {
                    if (id == -1) return (same_id == 1) ? "Same as Button SE" : "Same as Star";
                    const auto* s = util::FindWithID(db, id);
                    return s ? s->name : "None";
                };

                rows.push_back({ "Sustain", getSoundName(cfg->button_l_se_id, *sound_db::GetButtonLongOnSoundDB(), 1), "Sound effect played when holding Sustain / Rush notes.", "" });
                rows.push_back({ "Double",  getSoundName(cfg->button_w_se_id, *sound_db::GetButtonWSoundDB(),      1), "Sound effect played when hitting Double notes.", "" });
                rows.push_back({ "Star",    getSoundName(cfg->star_se_id,     *sound_db::GetStarSoundDB(),         0), "Sound effect played when hitting standard Star notes.", "" });
                rows.push_back({ "Link",    getSoundName(cfg->link_se_id,     *sound_db::GetLinkSoundDB(),         2), "Sound effect played when hitting Link Star notes.", "" });
                rows.push_back({ "D-Star",  getSoundName(cfg->star_w_se_id,   *sound_db::GetStarWSoundDB(),        2), "Sound effect played when hitting Double Star notes.", "" });
            }
            else if (g_currentPage == 1 && cfg)
            {
                const char* tzDispNames[] = { "Off", "Console/Mixed Only", "Always" };
                int curDisp = std::clamp((int)cfg->tech_zone_style, 0, 2);
                rows.push_back({ "Display", tzDispNames[curDisp], "Choose when to display the visual indicator for the Technical Zone.", "The score is not affected in Arcade-style charts." });

                int32_t tzPrio = *reinterpret_cast<int32_t*>(&shared.reserved[4]);
                std::string prioName = "F 2nd";
                if (tzPrio == 0) prioName = "F"; else if (tzPrio == 1) prioName = "F 2nd"; else if (tzPrio == 2) prioName = "X"; else if (tzPrio == 3) prioName = "Future Tone"; else if (tzPrio == 6) prioName = "Mega Mix+"; else if (tzPrio == 20) prioName = "Match UI";
                rows.push_back({ "Sound Priority", prioName, "Choose which sound effect set takes priority during Technical Zones.", "" });
            }
            else if (g_currentPage == 2)
            {
                rows.push_back({ "Stick Sensitivity", util::Format("%d%%", shared.stick_sensitivity), "Adjust stick sensitivity when hitting Star notes.", "" });
                rows.push_back({ "Flick Control SE", shared.stick_control_se == 0 ? "Slide" : "Star", "Sound effect to play when flicking the analog stick.", "" });
                rows.push_back({ "Sound Priority", shared.sound_prio > 0 ? "Mute" : "Disabled", "Adjust sound playback behavior when both console and arcade sounds trigger.", "" });

                int32_t starCtrl = *reinterpret_cast<int32_t*>(&shared.reserved[0]);
                const char* starNames[] = { "Sticks Only", "Buttons Only", "Both" };
                rows.push_back({ "Star Control", starNames[std::clamp(starCtrl, 0, 2)], "Choose which input method can trigger Star notes.", "" });
            }

            float startY = center.y + 118.0f;
            float rowH = (g_currentPage == 0) ? 36.0f : 42.0f;
            float pillW = 530.0f * scale;
            float pillH = (g_currentPage == 0) ? 30.0f : 34.0f;

            for (size_t i = 0; i < rows.size(); i++)
            {
                bool isSelected = (g_selectedItem == (int)i);
                ImVec2 pPos(center.x + (curW - pillW) * 0.5f, startY + (i * rowH));

                if (isSelected) {
                    drawList->AddRectFilled(pPos, ImVec2(pPos.x + pillW, pPos.y + pillH), IM_COL32(0, 215, 225, (int)(250.0f * alpha)), pillH * 0.5f);
                    drawList->AddRect(pPos, ImVec2(pPos.x + pillW, pPos.y + pillH), IM_COL32(255, 255, 255, (int)(220.0f * alpha)), pillH * 0.5f, 0, 1.5f);
                } else {
                    drawList->AddRectFilled(pPos, ImVec2(pPos.x + pillW, pPos.y + pillH), IM_COL32(225, 233, 238, (int)(210.0f * alpha)), pillH * 0.5f);
                    drawList->AddRect(pPos, ImVec2(pPos.x + pillW, pPos.y + pillH), IM_COL32(185, 200, 210, (int)(150.0f * alpha)), pillH * 0.5f, 0, 1.0f);
                }

                ImGui::SetWindowFontScale(1.10f);
                ImVec2 lblSz = ImGui::CalcTextSize(rows[i].label.c_str());
                float labelColW = pillW - 240.0f;
                ImVec2 lblPos(pPos.x + (labelColW - lblSz.x) * 0.5f, pPos.y + (pillH - lblSz.y) * 0.5f);

                if (isSelected) DrawBoldText(drawList, lblPos, IM_COL32(255, 255, 255, (int)(255.0f * alpha)), rows[i].label.c_str());
                else drawList->AddText(lblPos, IM_COL32(40, 55, 70, (int)(240.0f * alpha)), rows[i].label.c_str());
                ImGui::SetWindowFontScale(1.0f);

                float innerBoxW = 224.0f * scale;
                float innerBoxH = pillH - 6.0f;
                ImVec2 innerPos(pPos.x + pillW - innerBoxW - 6.0f, pPos.y + 3.0f);

                ImU32 innerBg = isSelected ? IM_COL32(0, 155, 170, (int)(235.0f * alpha)) : IM_COL32(198, 208, 216, (int)(220.0f * alpha));
                drawList->AddRectFilled(innerPos, ImVec2(innerPos.x + innerBoxW, innerPos.y + innerBoxH), innerBg, innerBoxH * 0.5f);

                std::string valStr = rows[i].value;
                ImVec2 valSz = ImGui::CalcTextSize(valStr.c_str());
                float valCenterX = innerPos.x + (innerBoxW * 0.5f);
                ImU32 valCol = isSelected ? IM_COL32(255, 255, 255, (int)(255.0f * alpha)) : IM_COL32(30, 45, 60, (int)(240.0f * alpha));

                drawList->AddText(ImVec2(valCenterX - (valSz.x * 0.5f), innerPos.y + (innerBoxH - valSz.y) * 0.5f), valCol, valStr.c_str());

                float arrowPulse = isSelected ? (std::sin(g_pulseTimer) * 1.5f) : 0.0f;
                drawList->AddText(ImVec2(innerPos.x + 10.0f - arrowPulse, innerPos.y + (innerBoxH - valSz.y) * 0.5f), valCol, "<");
                drawList->AddText(ImVec2(innerPos.x + innerBoxW - 18.0f + arrowPulse, innerPos.y + (innerBoxH - valSz.y) * 0.5f), valCol, ">");
            }

            if (!rows.empty() && g_selectedItem < (int)rows.size())
            {
                const auto& item = rows[g_selectedItem];
                float helpCenterY = center.y + curH - (item.help_line2.empty() ? 54.0f : 64.0f);

                ImVec2 h1Sz = ImGui::CalcTextSize(item.help_line1.c_str());
                DrawBoldText(drawList, ImVec2(center.x + (curW - h1Sz.x) * 0.5f, helpCenterY), IM_COL32(50, 70, 85, (int)(250.0f * alpha)), item.help_line1.c_str());

                if (!item.help_line2.empty()) {
                    ImVec2 h2Sz = ImGui::CalcTextSize(item.help_line2.c_str());
                    DrawBoldText(drawList, ImVec2(center.x + (curW - h2Sz.x) * 0.5f, helpCenterY + 16.0f), IM_COL32(50, 70, 85, (int)(250.0f * alpha)), item.help_line2.c_str());
                }
            }

            float pageY = center.y + curH - 30.0f;
            std::string pageStr = util::Format("<   %d / %d   >", g_currentPage + 1, TotalPages);
            ImVec2 pageSz = ImGui::CalcTextSize(pageStr.c_str());
            DrawBoldText(drawList, ImVec2(center.x + (curW - pageSz.x) * 0.5f, pageY), IM_COL32(0, 160, 185, (int)(255.0f * alpha)), pageStr.c_str());

            drawList->AddText(ImVec2(center.x + 36.0f, pageY), IM_COL32(100, 125, 140, (int)(220.0f * alpha)), "[L]");
            drawList->AddText(ImVec2(center.x + curW - 56.0f, pageY), IM_COL32(100, 125, 140, (int)(220.0f * alpha)), "[R]");

            drawList->PopClipRect();
        }
        ImGui::End();
    }

    // 1. Task Init (0x778930): Preloads sound farcs into memory on menu entrance
    HOOK_DEFINE_TRAMPOLINE(CustomizeSelTaskInitHook) {
        static uint64_t Callback(uint64_t a1) {
            uint64_t res = Orig(a1);
            s_inCustomizeScene = true;
            sound::RequestFarcLoad("rom/sound/se_nc.farc");
            sound::RequestFarcLoad("rom/sound/se_nc_option.farc");
            return res;
        }
    };

    // 2. Task Ctrl (0x778980): Wait for assets to load
    HOOK_DEFINE_TRAMPOLINE(CustomizeSelTaskCtrlHook) {
        static uint64_t Callback(uint64_t a1) {
            if (!s_assetsLoaded)
            {
                s_assetsLoaded = !sound::IsFarcLoading("rom/sound/se_nc.farc") &&
                                 !sound::IsFarcLoading("rom/sound/se_nc_option.farc");
            }
            return Orig(a1);
        }
    };

    // 3. Task Dest (0x778990): Unloads sound farcs on exit
    HOOK_DEFINE_TRAMPOLINE(CustomizeSelTaskDestHook) {
        static uint64_t Callback(uint64_t a1) {
            s_inCustomizeScene = false;
            s_assetsLoaded = false;
            ForceClose();
            return Orig(a1);
        }
    };

    // 4. CSTopMenuMainCtrl Hook (0x7a9370)
    HOOK_DEFINE_TRAMPOLINE(CSTopMenuMainCtrlHook) {
        static uint64_t Callback(uint64_t a1) {
            s_inCustomizeScene = true;
            CustomizeSelUi::Update();

            if (CustomizeSelUi::IsOpen()) {
                return 1;
            }

            return Orig(a1);
        }
    };

    void Init()
    {
        CustomizeSelTaskInitHook::InstallAtOffset(0x778930);
        CustomizeSelTaskCtrlHook::InstallAtOffset(0x778980);
        CustomizeSelTaskDestHook::InstallAtOffset(0x778990);
        CSTopMenuMainCtrlHook::InstallAtOffset(0x7a9370);

        // lights perhaps slider controller x13 nullptr fix
        exl::patch::CodePatcher(0x00601eb0).Write<uint32_t>(0x14000028);
    }
}
