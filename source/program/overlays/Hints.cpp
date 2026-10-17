#include "Hints.hpp"
#include "imgui/imgui.h"
#include <cmath>
#include <algorithm>

#include "../nc/diva_nc.hpp"
#include "../nc/ui/customize_sel.hpp"
#include "../AutoPlay.hpp"
#include "../Mode2DNoMV.hpp"
#include "../ChartRandomizer.hpp"
#include "Config.hpp"
#include "PluginHelpUi.hpp"
#include "hid.hpp"

namespace ImGui {
    extern bool g_imguiHasFocus;
}

namespace Hints {

using Mode2DNoMV::g_force2D;
using Mode2DNoMV::g_noMovie;

static bool s_isPvSelActive       = false;
static bool s_isSongConfirmActive = false;
static int32_t s_topMenuLifeTicks = 0;

static bool s_randomizerMenuOpen  = false;

static bool IsInCustomize() {
    return CustomizeSelUi::IsInCustomizeScene() || CustomizeSelUi::IsOpen();
}

static const ImU32 kDivaLightPink   = IM_COL32(255, 120, 180, 255);
static const ImU32 kDivaPinkBorder  = IM_COL32(210, 50, 125, 255);
static const ImU32 kDivaCyan        = IM_COL32(20, 220, 230, 255);
static const ImU32 kDivaCyanBorder  = IM_COL32(10, 160, 170, 255);

static void DrawSwitchTriggerZL(ImDrawList* drawList, ImFont* font, ImVec2 pos, ImVec2 size, bool highlighted = false) {
    float x = pos.x, y = pos.y, w = size.x, h = size.y;

    float topCurve = h * 0.35f;
    ImVec2 p0(x + topCurve, y);
    ImVec2 p1(x + w - 3.0f, y);
    ImVec2 p2(x + w, y + 3.0f);
    ImVec2 p3(x + w, y + h);
    ImVec2 p4(x, y + h);
    ImVec2 p5(x + 2.0f, y + h * 0.45f);

    ImVec2 pts[] = { p0, p1, p2, p3, p4, p5 };

    ImU32 fillCol   = highlighted ? kDivaLightPink : IM_COL32(200, 206, 214, 245);
    ImU32 borderCol = highlighted ? kDivaPinkBorder : IM_COL32(115, 125, 138, 255);

    drawList->AddConvexPolyFilled(pts, 6, fillCol);
    drawList->AddPolyline(pts, 6, borderCol, ImDrawFlags_Closed, 1.3f);
    drawList->AddLine(p0, p1, IM_COL32(255, 255, 255, 160), 1.0f);

    const char* txt = "ZL";
    float fontSize = h * 0.70f;
    ImVec2 txtSz = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, txt);
    ImVec2 txtPos(x + (w - txtSz.x) * 0.5f + 1.0f, y + (h - txtSz.y) * 0.5f);

    ImU32 textDarkCol = highlighted ? IM_COL32(35, 42, 48, 255) : IM_COL32(35, 42, 48, 255);
    drawList->AddText(font, fontSize, txtPos, textDarkCol, txt);
}

static void DrawSwitchTriggerZR(ImDrawList* drawList, ImFont* font, ImVec2 pos, ImVec2 size, bool highlighted = false) {
    float x = pos.x, y = pos.y, w = size.x, h = size.y;

    float topCurve = h * 0.35f;
    ImVec2 p0(x + 3.0f, y);
    ImVec2 p1(x + w - topCurve, y);
    ImVec2 p2(x + w - 2.0f, y + h * 0.45f);
    ImVec2 p3(x + w, y + h);
    ImVec2 p4(x, y + h);
    ImVec2 p5(x, y + 3.0f);

    ImVec2 pts[] = { p0, p1, p2, p3, p4, p5 };

    ImU32 fillCol   = highlighted ? kDivaLightPink : IM_COL32(200, 206, 214, 245);
    ImU32 borderCol = highlighted ? kDivaPinkBorder : IM_COL32(115, 125, 138, 255);

    drawList->AddConvexPolyFilled(pts, 6, fillCol);
    drawList->AddPolyline(pts, 6, borderCol, ImDrawFlags_Closed, 1.3f);
    drawList->AddLine(p0, p1, IM_COL32(255, 255, 255, 160), 1.0f);

    const char* txt = "ZR";
    float fontSize = h * 0.70f;
    ImVec2 txtSz = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, txt);
    ImVec2 txtPos(x + (w - txtSz.x) * 0.5f - 1.0f, y + (h - txtSz.y) * 0.5f);

    ImU32 textDarkCol = highlighted ? IM_COL32(35, 42, 48, 255) : IM_COL32(35, 42, 48, 255);
    drawList->AddText(font, fontSize, txtPos, textDarkCol, txt);
}

static void DrawRoundButtonPlus(ImDrawList* drawList, ImVec2 center, float radius) {
    drawList->AddCircleFilled(center, radius, kDivaLightPink, 32);
    drawList->AddCircle(center, radius, kDivaPinkBorder, 32, 2.0f);

    float plusHalf = 7.0f;
    ImU32 textDarkCol = IM_COL32(35, 42, 48, 255);
    drawList->AddLine(ImVec2(center.x - plusHalf, center.y), ImVec2(center.x + plusHalf, center.y), textDarkCol, 3.0f);
    drawList->AddLine(ImVec2(center.x, center.y - plusHalf), ImVec2(center.x, center.y + plusHalf), textDarkCol, 3.0f);
}

static void DrawRoundButtonMinus(ImDrawList* drawList, ImVec2 center, float radius) {
    drawList->AddCircleFilled(center, radius, kDivaCyan, 32);
    drawList->AddCircle(center, radius, kDivaCyanBorder, 32, 2.0f);

    float minusHalf = 7.0f;
    ImU32 textDarkCol = IM_COL32(35, 42, 48, 255);
    drawList->AddLine(ImVec2(center.x - minusHalf, center.y), ImVec2(center.x + minusHalf, center.y), textDarkCol, 3.0f);
}

static void DrawSwitchButtonX(ImDrawList* drawList, ImFont* font, ImVec2 center, float radius) {
    drawList->AddCircleFilled(center, radius, IM_COL32(48, 54, 62, 245), 24);
    drawList->AddCircle(center, radius, IM_COL32(130, 140, 150, 255), 24, 1.4f);

    const char* txt = "X";
    float fontSize = font->FontSize * 0.95f;
    ImVec2 sz = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, txt);
    drawList->AddText(font, fontSize, ImVec2(center.x - sz.x * 0.5f, center.y - sz.y * 0.5f), IM_COL32(220, 226, 232, 255), txt);
}

static void DrawStickIcon(ImDrawList* drawList, ImFont* font, ImVec2 pos, const char* txt, bool highlighted) {
    float radius = 11.0f;
    ImVec2 center(pos.x + radius, pos.y + radius);

    ImU32 fillCol   = highlighted ? kDivaLightPink : IM_COL32(210, 216, 222, 245);
    ImU32 borderCol = highlighted ? kDivaPinkBorder : IM_COL32(125, 135, 145, 255);

    drawList->AddCircleFilled(center, radius, fillCol, 24);
    drawList->AddCircle(center, radius, borderCol, 24, 1.5f);

    float fontSize = font->FontSize * 0.95f;
    ImVec2 txtSz = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, txt);
    ImVec2 txtPos(center.x - txtSz.x * 0.5f, center.y - txtSz.y * 0.5f);

    ImU32 textDarkCol = highlighted ? IM_COL32(35, 42, 48, 255) : IM_COL32(40, 48, 55, 255);
    drawList->AddText(font, fontSize, txtPos, textDarkCol, txt);
}

static void DrawRandomizerConfirmHint(ImDrawList* drawList, ImFont* font) {
    if (!s_isSongConfirmActive || IsInCustomize())
        return;

    ImGuiIO& io = ImGui::GetIO();

    float minusX = 200.0f;
    float minusY = 536.0f;
    float radius = 14.0f;
    ImVec2 minusCenter(minusX + radius, minusY + radius);

    float dist = std::hypot(io.MousePos.x - minusCenter.x, io.MousePos.y - minusCenter.y);
    if (dist <= radius && ImGui::IsMouseClicked(0)) {
        s_randomizerMenuOpen = !s_randomizerMenuOpen;
    }

    DrawRoundButtonMinus(drawList, minusCenter, radius);

    if (s_randomizerMenuOpen)
    {
        float menuX  = minusX;
        float menuY  = minusY + (radius * 2.0f) + 6.0f;
        float menuW  = 220.0f;
        float rowH   = 30.0f;
        float totalH = rowH * 2.0f;

        ImVec2 pMin(menuX, menuY);
        ImVec2 pMax(menuX + menuW, menuY + totalH);

        drawList->AddRectFilled(pMin, pMax, IM_COL32(18, 22, 28, 240), 8.0f);
        drawList->AddRect(pMin, pMax, kDivaPinkBorder, 8.0f, 0, 1.5f);
        drawList->AddLine(ImVec2(menuX + 8.0f, menuY + rowH), ImVec2(menuX + menuW - 8.0f, menuY + rowH), IM_COL32(255, 255, 255, 30), 1.0f);

        float row1MidY = menuY + (rowH * 0.5f);

        const char* tRandomizer = "Randomizer";
        ImVec2 tRandSz = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, tRandomizer);
        drawList->AddText(font, font->FontSize, ImVec2(menuX + 12.0f, row1MidY - tRandSz.y * 0.5f), IM_COL32(240, 244, 248, 255), tRandomizer);

        auto mode = ChartRandomizer::GetMode();
        const char* modeTxt = (mode == ChartRandomizer::Mode_Off) ? "OFF" : (mode == ChartRandomizer::Mode_Partial ? "PARTIAL" : "FULL");
        ImU32 modeCol = (mode == ChartRandomizer::Mode_Off) ? IM_COL32(160, 170, 180, 220) : (mode == ChartRandomizer::Mode_Partial ? kDivaCyan : kDivaLightPink);

        ImVec2 modeSz = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, modeTxt);

        float rightCenter1 = menuX + menuW - 58.0f;
        float btnRad = 9.5f;

        ImVec2 btnX1(rightCenter1 - modeSz.x * 0.5f - 15.0f, row1MidY);
        ImVec2 btnX2(rightCenter1 + modeSz.x * 0.5f + 15.0f, row1MidY);

        DrawSwitchButtonX(drawList, font, btnX1, btnRad);
        drawList->AddText(font, font->FontSize, ImVec2(rightCenter1 - modeSz.x * 0.5f, row1MidY - modeSz.y * 0.5f), modeCol, modeTxt);
        DrawSwitchButtonX(drawList, font, btnX2, btnRad);

        if (io.MousePos.x >= rightCenter1 - 65.0f && io.MousePos.x <= menuX + menuW &&
            io.MousePos.y >= menuY && io.MousePos.y <= menuY + rowH && ImGui::IsMouseClicked(0)) {
            ChartRandomizer::CycleMode();
        }

        float row2MidY = menuY + rowH + (rowH * 0.5f);

        const char* tChance = "Random chance";
        ImVec2 tChanceSz = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, tChance);
        drawList->AddText(font, font->FontSize, ImVec2(menuX + 12.0f, row2MidY - tChanceSz.y * 0.5f), IM_COL32(240, 244, 248, 255), tChance);

        float marginRight = 12.0f;
        ImVec2 btnTriggerSize(26.0f, 17.0f);

        float zrX = menuX + menuW - btnTriggerSize.x - marginRight;
        float zrY = row2MidY - btnTriggerSize.y * 0.5f;

        char pctBuf[16];
        snprintf(pctBuf, sizeof(pctBuf), "%d%%", ChartRandomizer::GetChance());
        ImVec2 pctSz = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, pctBuf);
        float pctX = zrX - 8.0f - pctSz.x;
        float pctY = row2MidY - pctSz.y * 0.5f;

        float zlX = pctX - 8.0f - btnTriggerSize.x;
        float zlY = row2MidY - btnTriggerSize.y * 0.5f;

        DrawSwitchTriggerZL(drawList, font, ImVec2(zlX, zlY), btnTriggerSize, false);
        drawList->AddText(font, font->FontSize, ImVec2(pctX, pctY), IM_COL32(255, 255, 255, 255), pctBuf);
        DrawSwitchTriggerZR(drawList, font, ImVec2(zrX, zrY), btnTriggerSize, false);

        if (io.MousePos.x >= zlX - 3.0f && io.MousePos.x <= zlX + btnTriggerSize.x + 3.0f &&
            io.MousePos.y >= zlY - 3.0f && io.MousePos.y <= zlY + btnTriggerSize.y + 3.0f && ImGui::IsMouseClicked(0)) {
            ChartRandomizer::AdjustChance(-5);
        }
        if (io.MousePos.x >= zrX - 3.0f && io.MousePos.x <= zrX + btnTriggerSize.x + 3.0f &&
            io.MousePos.y >= zrY - 3.0f && io.MousePos.y <= zrY + btnTriggerSize.y + 3.0f && ImGui::IsMouseClicked(0)) {
            ChartRandomizer::AdjustChance(5);
        }
    }
}

static void DrawMainMenuHelpHint(ImDrawList* drawList, ImFont* font) {
    if (!PluginHelpUi::IsMainMenuActive() || PluginHelpUi::IsOpen())
        return;

    float startX = 90.0f, startY = 682.0f, btnRadius = 14.0f;
    ImVec2 btnCenter(startX + btnRadius, startY + 13.0f);
    DrawRoundButtonPlus(drawList, btnCenter, btnRadius);

    const char* text = "MikuMikuSwitchPlugin Help";
    float fontSize = font->FontSize * 2.3f;
    ImVec2 txtSz = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
    ImVec2 textPos(startX + (btnRadius * 2.0f) + 12.0f, startY + (26.0f - txtSz.y) * 0.5f);

    drawList->AddText(font, fontSize, ImVec2(textPos.x + 1.2f, textPos.y + 1.2f), IM_COL32(0, 0, 0, 160), text);
    drawList->AddText(font, fontSize, textPos, kDivaLightPink, text);
}

static void DrawCustomizeHint(ImDrawList* drawList, ImFont* font) {
    if (s_topMenuLifeTicks <= 0 || CustomizeSelUi::IsOpen())
        return;

    float startX = 90.0f, startY = 682.0f;
    ImVec2 btnSize(44.0f, 26.0f);
    DrawSwitchTriggerZL(drawList, font, ImVec2(startX, startY), btnSize, true);

    const char* text = "NC OPTIONS";
    float fontSize = font->FontSize * 1.75f;
    ImVec2 txtSz = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
    ImVec2 textPos(startX + btnSize.x + 12.0f, startY + (btnSize.y - txtSz.y) * 0.5f);

    drawList->AddText(font, fontSize, textPos, kDivaLightPink, text);
}

static void DrawAutoPlayBadge(ImDrawList* drawList, ImFont* font) {
    if (!s_isPvSelActive || IsInCustomize())
        return;

    float w = 122.0f, h = 50.0f, posX = 1280.0f - w - 24.0f, posY = 64.0f;
    drawList->AddRectFilled(ImVec2(posX, posY), ImVec2(posX + w, posY + h), IM_COL32(18, 22, 26, 230), 10.0f);
    drawList->AddRect(ImVec2(posX, posY), ImVec2(posX + w, posY + h), IM_COL32(255, 255, 255, 35), 10.0f, 0, 1.2f);

    ImVec2 zrSize(28.0f, 15.0f);
    DrawSwitchTriggerZR(drawList, font, ImVec2(posX + w - 12.0f - zrSize.x, posY + 6.0f), zrSize, false);

    const char* title = "AUTOPLAY";
    ImVec2 titleSz = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, title);
    drawList->AddText(font, font->FontSize, ImVec2(posX + 14.0f, posY + 6.0f + (zrSize.y - titleSz.y) * 0.5f), IM_COL32(255, 255, 255, 255), title);

    float capW = w - 24.0f, capH = 19.0f;
    ImVec2 capMin(posX + 12.0f, posY + 25.0f), capMax(capMin.x + capW, capMin.y + capH);
    ImU32 stateColor = AutoPlay::g_enabled ? IM_COL32(18, 205, 205, 255) : IM_COL32(165, 175, 185, 255);
    ImU32 stateBg    = AutoPlay::g_enabled ? IM_COL32(18, 205, 205, 40)  : IM_COL32(165, 175, 185, 40);

    drawList->AddRectFilled(capMin, capMax, stateBg, 9.5f);
    drawList->AddRect(capMin, capMax, AutoPlay::g_enabled ? IM_COL32(18, 205, 205, 140) : IM_COL32(165, 175, 185, 140), 9.5f, 0, 1.2f);

    float labelFontSize = font->FontSize;
    const char* label = AutoPlay::g_enabled ? "ON" : "OFF";
    ImVec2 lblSz = font->CalcTextSizeA(labelFontSize, FLT_MAX, 0.0f, label);
    drawList->AddText(font, labelFontSize, ImVec2(capMin.x + (capW - lblSz.x) * 0.5f, capMin.y + (capH - lblSz.y) * 0.5f - 0.5f), stateColor, label);
}

static void Draw2DModeBadge(ImDrawList* drawList, ImFont* font) {
    if (!s_isPvSelActive || IsInCustomize())
        return;

    float w = 160.0f, h = 34.0f, posX = 10.0f, posY = 682.0f;
    ImU32 bgCol     = g_force2D ? IM_COL32(38, 16, 30, 235)    : IM_COL32(18, 22, 26, 205);
    ImU32 borderCol = g_force2D ? kDivaLightPink               : IM_COL32(255, 255, 255, 30);
    ImU32 textCol   = g_force2D ? IM_COL32(255, 255, 255, 255) : IM_COL32(165, 175, 185, 215);

    drawList->AddRectFilled(ImVec2(posX, posY), ImVec2(posX + w, posY + h), bgCol, 7.0f);
    drawList->AddRect(ImVec2(posX, posY), ImVec2(posX + w, posY + h), borderCol, 7.0f, 0, g_force2D ? 1.6f : 1.0f);

    const char* label = "2D MODE";
    float labelFontSize = font->FontSize;
    ImVec2 lblSz = font->CalcTextSizeA(labelFontSize, FLT_MAX, 0.0f, label);
    float textStartX = posX + 12.0f;
    drawList->AddText(font, font->FontSize, ImVec2(textStartX, posY + (h - lblSz.y) * 0.5f), textCol, label);

    float btnX = textStartX + lblSz.x + 10.0f;
    DrawStickIcon(drawList, font, ImVec2(btnX, posY + (h - 22.0f) * 0.5f), "L3", g_force2D);
    btnX += 28.0f;
    drawList->AddText(font, font->FontSize, ImVec2(btnX, posY + (h - lblSz.y) * 0.5f), textCol, "+");
    btnX += 14.0f;
    DrawSwitchTriggerZR(drawList, font, ImVec2(btnX, posY + (h - 18.0f) * 0.5f), ImVec2(28.0f, 18.0f), g_force2D);
}

static void DrawNoMovieBadge(ImDrawList* drawList, ImFont* font) {
    if (!s_isPvSelActive || IsInCustomize())
        return;

    float w = 124.0f, h = 34.0f, posX = 676.0f, posY = 573.0f;
    ImU32 bgCol     = g_noMovie ? IM_COL32(38, 16, 30, 235)    : IM_COL32(18, 22, 26, 205);
    ImU32 borderCol = g_noMovie ? kDivaLightPink               : IM_COL32(255, 255, 255, 30);
    ImU32 textCol   = g_noMovie ? IM_COL32(255, 255, 255, 255) : IM_COL32(165, 175, 185, 215);

    drawList->AddRectFilled(ImVec2(posX, posY), ImVec2(posX + w, posY + h), bgCol, 7.0f);
    drawList->AddRect(ImVec2(posX, posY), ImVec2(posX + w, posY + h), borderCol, 7.0f, 0, g_noMovie ? 1.6f : 1.0f);

    const char* label = "NO MOVIE";
    float labelFontSize = font->FontSize;
    ImVec2 lblSz = font->CalcTextSizeA(labelFontSize, FLT_MAX, 0.0f, label);
    float textStartX = posX + 8.0f;
    drawList->AddText(font, font->FontSize, ImVec2(textStartX, posY + (h - lblSz.y) * 0.5f), textCol, label);

    float btnX = textStartX + lblSz.x + 5.0f;
    DrawSwitchTriggerZR(drawList, font, ImVec2(btnX, posY + (h - 18.0f) * 0.5f), ImVec2(28.0f, 18.0f), g_noMovie);
    btnX += 32.0f;
    drawList->AddText(font, font->FontSize, ImVec2(btnX, posY + (h - lblSz.y) * 0.5f + 1.0f), g_noMovie ? kDivaLightPink : IM_COL32(175, 185, 195, 200), "x2");
}

bool IsVisible() {
    if (CustomizeSelUi::IsOpen()) return false;
    if (PluginHelpUi::IsMainMenuActive()) return true;
    return (s_topMenuLifeTicks > 0) || s_isPvSelActive || s_isSongConfirmActive;
}

void SetPvSelActive(bool active) {
    s_isPvSelActive = active;
}

void SetSongConfirmActive(bool active) {
    s_isSongConfirmActive = active;
    if (!active) {
        s_randomizerMenuOpen = false;
    }
}

void SetCustomizeSceneActive() {
    s_topMenuLifeTicks = 2;
}

void Init() {
    s_isPvSelActive       = false;
    s_isSongConfirmActive = false;
    s_topMenuLifeTicks    = 0;
    s_randomizerMenuOpen  = false;
}

void ProcessInput() {
    nn::hid::NpadHandheldState state = nn::hid::GetMergedNpadState();
    uint64_t hid_buttons = state.buttons;

    static uint64_t s_prev_buttons = 0;
    uint64_t pressed = hid_buttons & ~s_prev_buttons;
    s_prev_buttons = hid_buttons;

    // Main menu (Plus button)
    if (PluginHelpUi::IsMainMenuActive()) {
        if (pressed & nn::hid::Button::Plus) {
            PluginHelpUi::Toggle();
        }
        return;
    }

    // Song confirmation screen (State 8): Randomizer controls
    if (s_isSongConfirmActive && !IsInCustomize()) {
        if (pressed & nn::hid::Button::Minus) {
            s_randomizerMenuOpen = !s_randomizerMenuOpen;
            sound::PlaySelectSE();
        }

        if (s_randomizerMenuOpen) {
            // Toggle OFF / PARTIAL / FULL mode
            if (pressed & nn::hid::Button::X) {
                ChartRandomizer::CycleMode();
                sound::PlaySelectSE();
            }
            // Adjust randomizer chance percentage
            if (pressed & nn::hid::Button::ZL) {
                ChartRandomizer::AdjustChance(-5);
                sound::PlaySelectSE();
            }
            if (pressed & nn::hid::Button::ZR) {
                ChartRandomizer::AdjustChance(5);
                sound::PlaySelectSE();
            }
        }
        return;
    }

    // Song selection menu
    if (!s_isPvSelActive || IsInCustomize())
        return;

    constexpr uint64_t kStickLMask = (1ULL << 4); // L3
    constexpr uint64_t kZRMask     = (1ULL << 9); // ZR

    bool zr_down = (hid_buttons & kZRMask) != 0;
    bool l3_down = (hid_buttons & kStickLMask) != 0;

    static bool s_last_zr = false;
    static bool s_last_l3 = false;

    bool zr_pressed  = zr_down && !s_last_zr;
    bool zr_released = !zr_down && s_last_zr;
    bool l3_tapped   = l3_down && !s_last_l3;

    s_last_zr = zr_down;
    s_last_l3 = l3_down;

    static int s_tap_count = 0;
    static int s_tap_timer = 0;

    if ((l3_down && zr_pressed) || (zr_down && l3_tapped)) {
        if (!Config::no3D) {
            g_force2D = !g_force2D;
            sound::PlaySelectSE();
        }
        s_tap_count = 0;
        s_tap_timer = 0;
        return;
    }

    if (l3_down) return;

    if (zr_pressed) {
        if (s_tap_count == 0) s_tap_count = 1;
        else if (s_tap_count == 1) {
            s_tap_count = 2;
            s_tap_timer = 0;
            if (!Config::noMV) {
                g_noMovie = !g_noMovie;
                sound::PlaySelectSE();
            }
        }
    }

    if (zr_released) {
        if (s_tap_count == 1) s_tap_timer = 18;
        else if (s_tap_count == 2) s_tap_count = 0;
    }

    if (s_tap_timer > 0) {
        s_tap_timer--;
        if (s_tap_timer <= 0 && s_tap_count == 1) {
            s_tap_count = 0;
            AutoPlay::g_enabled = !AutoPlay::g_enabled;
            sound::PlaySelectSE();
        }
    }
}

void Draw() {
    ProcessInput();

    if (!IsVisible()) {
        if (s_topMenuLifeTicks > 0) s_topMenuLifeTicks--;
        return;
    }

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    drawList->PushClipRectFullScreen();

    ImFont* font = ImGui::GetFont();

    DrawMainMenuHelpHint(drawList, font);
    DrawCustomizeHint(drawList, font);
    DrawAutoPlayBadge(drawList, font);
    Draw2DModeBadge(drawList, font);
    DrawNoMovieBadge(drawList, font);
    DrawRandomizerConfirmHint(drawList, font);

    drawList->PopClipRect();

    if (s_topMenuLifeTicks > 0)
        s_topMenuLifeTicks--;
}

} // namespace Hints
