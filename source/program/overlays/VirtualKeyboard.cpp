#include "VirtualKeyboard.hpp"
#include "lib.hpp"
#include "imgui/imgui.h"
#include <string>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace VirtualKeyboard {

static bool   s_isOpen          = false;
static float  s_kbAnim          = 0.0f;
static char*  s_targetBuf       = nullptr;
static size_t s_bufSize         = 0;
static std::string s_submitLabel = "SUBMIT";
static void (*s_onSubmitCb)()   = nullptr;

static bool s_capsLock          = false;
static int  s_skipCloseFrames   = 0;
static int  s_cursorPos         = 0;

std::string ShowSwitchKeyboard(const char* guideText, const char* initialText) {
    return initialText ? std::string(initialText) : "";
}

bool IsOpen() {
    return s_isOpen;
}

int GetCursorPos() {
    return s_cursorPos;
}

bool IsVisible() {
    return s_isOpen || (s_kbAnim > 0.005f);
}

void Open(char* target_buf, size_t buf_size, const char* submit_label, void (*on_submit_cb)()) {
    s_targetBuf       = target_buf;
    s_bufSize         = buf_size;
    s_submitLabel     = submit_label ? submit_label : "SUBMIT";
    s_onSubmitCb      = on_submit_cb;
    s_isOpen          = true;
    s_skipCloseFrames = 3;

    if (target_buf) {
        s_cursorPos = static_cast<int>(strlen(target_buf));
    } else {
        s_cursorPos = 0;
    }
}

void Close() {
    s_isOpen = false;
}

bool ShouldFocusTarget() {
    return false;
}

int InputTextCallback(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackAlways) {
        if (data->CursorPos != s_cursorPos && s_cursorPos >= 0 && s_cursorPos <= data->BufTextLen) {
            data->CursorPos = s_cursorPos;
            data->SelectionStart = s_cursorPos;
            data->SelectionEnd = s_cursorPos;
        } else {
            s_cursorPos = data->CursorPos;
        }
    }
    return 0;
}

static void InsertTextAtCursor(const char* text) {
    if (!s_targetBuf || s_bufSize == 0) return;
    int curLen = static_cast<int>(strlen(s_targetBuf));
    int addLen = static_cast<int>(strlen(text));
    if (curLen + addLen >= static_cast<int>(s_bufSize)) return;

    if (s_cursorPos < 0) s_cursorPos = 0;
    if (s_cursorPos > curLen) s_cursorPos = curLen;

    memmove(s_targetBuf + s_cursorPos + addLen, s_targetBuf + s_cursorPos, curLen - s_cursorPos + 1);
    memcpy(s_targetBuf + s_cursorPos, text, addLen);

    s_cursorPos += addLen;
}

static void BackspaceAtCursor() {
    if (!s_targetBuf || s_bufSize == 0) return;
    int curLen = static_cast<int>(strlen(s_targetBuf));
    if (curLen == 0 || s_cursorPos <= 0) return;

    if (s_cursorPos > curLen) s_cursorPos = curLen;

    int delStart = s_cursorPos - 1;
    while (delStart > 0 && (static_cast<uint8_t>(s_targetBuf[delStart]) & 0xC0) == 0x80) {
        delStart--;
    }

    memmove(s_targetBuf + delStart, s_targetBuf + s_cursorPos, curLen - s_cursorPos + 1);
    s_cursorPos = delStart;
}

static void ClearAtCursor() {
    if (!s_targetBuf || s_bufSize == 0) return;
    s_targetBuf[0] = '\0';
    s_cursorPos = 0;
}

void Draw() {
    ImGuiIO& io = ImGui::GetIO();
    float dt = io.DeltaTime;

    if (s_skipCloseFrames > 0) {
        s_skipCloseFrames--;
    } else if (s_isOpen && ImGui::IsMouseClicked(0) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) {
        s_isOpen = false;
    }

    float targetAnim = s_isOpen ? 1.0f : 0.0f;
    s_kbAnim += (targetAnim - s_kbAnim) * std::min(dt * 15.0f, 1.0f);

    if (s_kbAnim <= 0.005f) return;

    const float kbWidth  = 1180.0f;
    const float kbHeight = 250.0f;
    const float startX   = (io.DisplaySize.x - kbWidth) * 0.5f;
    const float startY   = io.DisplaySize.y - (kbHeight + 10.0f) * s_kbAnim;

    ImGui::SetNextWindowPos(ImVec2(startX, startY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kbWidth, kbHeight), ImGuiCond_Always);

    ImGuiWindowFlags kbFlags = ImGuiWindowFlags_NoTitleBar |
                               ImGuiWindowFlags_NoResize |
                               ImGuiWindowFlags_NoMove |
                               ImGuiWindowFlags_NoCollapse |
                               ImGuiWindowFlags_NoSavedSettings |
                               ImGuiWindowFlags_NoScrollbar |
                               ImGuiWindowFlags_NoFocusOnAppearing |
                               ImGuiWindowFlags_NoNavFocus;

    if (ImGui::Begin("##VirtualKeyboardOverlay", nullptr, kbFlags)) {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->PushClipRectFullScreen();

        ImU32 colBg = IM_COL32(18, 22, 26, (int)(250 * s_kbAnim));
        ImU32 colBd = IM_COL32(255, 255, 255, (int)(45 * s_kbAnim));
        draw->AddRectFilled(ImVec2(startX, startY), ImVec2(startX + kbWidth, startY + kbHeight), colBg, 10.0f);
        draw->AddRect(ImVec2(startX, startY), ImVec2(startX + kbWidth, startY + kbHeight), colBd, 10.0f, 0, 1.2f);

        ImFont* font = ImGui::GetFont();
        float baseFontSz = (font->FontSize > 18.0f) ? (font->FontSize * 0.65f) : font->FontSize;

        const float btnH = 40.0f;
        const float gap  = 4.0f;

        bool isMouseClicked = ImGui::IsMouseClicked(0);
        ImVec2 mousePos = io.MousePos;

        auto DrawPassiveButton = [&](const char* label, float x, float y, float w, float h, ImU32 colorBase, ImU32 colorHov, ImU32 colorPrs, ImU32 colorTxt) -> bool {
            ImVec2 pMin(x, y);
            ImVec2 pMax(x + w, y + h);
            bool isHovered = (mousePos.x >= pMin.x && mousePos.x <= pMax.x && mousePos.y >= pMin.y && mousePos.y <= pMax.y);
            bool isDown = isHovered && ImGui::IsMouseDown(0);

            ImU32 bg = isDown ? colorPrs : (isHovered ? colorHov : colorBase);
            draw->AddRectFilled(pMin, pMax, bg, 4.0f);

            float fSize = baseFontSz * (strlen(label) <= 2 ? 1.30f : 1.10f);
            ImVec2 txtSz = font->CalcTextSizeA(fSize, FLT_MAX, 0.0f, label);
            ImVec2 txtPos(pMin.x + (w - txtSz.x) * 0.5f, pMin.y + (h - txtSz.y) * 0.5f);
            draw->AddText(font, fSize, txtPos, colorTxt, label);

            return isHovered && isMouseClicked;
        };

        ImU32 btnBg  = IM_COL32(32, 38, 46, (int)(235 * s_kbAnim));
        ImU32 btnHov = IM_COL32(55, 70, 90, (int)(255 * s_kbAnim));
        ImU32 btnPrs = IM_COL32(210, 216, 222, (int)(255 * s_kbAnim));
        ImU32 txtCol = IM_COL32(235, 240, 245, (int)(250 * s_kbAnim));
        ImU32 txtPrs = IM_COL32(35, 42, 48, (int)(255 * s_kbAnim));

        float curY = startY + 8.0f;

        const char* r0[] = {"1","2","3","4","5","6","7","8","9","0","-","_","!","&","?"};
        float r0_btnW = (kbWidth - 16.0f - (15 * gap) - 75.0f) / 15.0f;
        float curX = startX + 8.0f;
        for (int i = 0; i < 15; i++) {
            if (DrawPassiveButton(r0[i], curX, curY, r0_btnW, btnH, btnBg, btnHov, btnPrs, txtCol)) {
                InsertTextAtCursor(r0[i]);
            }
            curX += r0_btnW + gap;
        }
        if (DrawPassiveButton("<-", curX, curY, 75.0f, btnH, btnBg, btnHov, btnPrs, txtCol)) {
            BackspaceAtCursor();
        }

        curY += btnH + gap;
        curX = startX + 8.0f;

        const char* r1_upper[] = {"Q","W","E","R","T","Y","U","I","O","P","(",")","/","'"};
        const char* r1_lower[] = {"q","w","e","r","t","y","u","i","o","p","(",")","/","'"};
        const char** r1 = s_capsLock ? r1_upper : r1_lower;
        float r1_btnW = (kbWidth - 16.0f - (13 * gap)) / 14.0f;
        for (int i = 0; i < 14; i++) {
            if (DrawPassiveButton(r1[i], curX, curY, r1_btnW, btnH, btnBg, btnHov, btnPrs, txtCol)) {
                InsertTextAtCursor(r1[i]);
            }
            curX += r1_btnW + gap;
        }

        curY += btnH + gap;
        curX = startX + 8.0f;

        float capsBtnW = 65.0f;
        float clrBtnW  = 75.0f;
        ImU32 capsBg  = s_capsLock ? IM_COL32(0, 204, 166, (int)(245 * s_kbAnim)) : btnBg;
        ImU32 capsTxt = s_capsLock ? IM_COL32(20, 25, 30, (int)(255 * s_kbAnim))  : txtCol;
        if (DrawPassiveButton("CAPS", curX, curY, capsBtnW, btnH, capsBg, btnHov, btnPrs, capsTxt)) {
            s_capsLock = !s_capsLock;
        }
        curX += capsBtnW + gap;

        const char* r2_upper[] = {"A","S","D","F","G","H","J","K","L",":",";","~"};
        const char* r2_lower[] = {"a","s","d","f","g","h","j","k","l",":",";","~"};
        const char** r2 = s_capsLock ? r2_upper : r2_lower;
        float r2_btnW = (kbWidth - 16.0f - (13 * gap) - capsBtnW - clrBtnW) / 12.0f;
        for (int i = 0; i < 12; i++) {
            if (DrawPassiveButton(r2[i], curX, curY, r2_btnW, btnH, btnBg, btnHov, btnPrs, txtCol)) {
                InsertTextAtCursor(r2[i]);
            }
            curX += r2_btnW + gap;
        }
        if (DrawPassiveButton("CLR", curX, curY, clrBtnW, btnH, btnBg, btnHov, btnPrs, txtCol)) {
            ClearAtCursor();
        }

        curY += btnH + gap;
        curX = startX + 8.0f;

        const char* r3_upper[] = {"Z","X","C","V","B","N","M",",",".","\"","+","@"};
        const char* r3_lower[] = {"z","x","c","v","b","n","m",",",".","\"","+","@"};
        const char** r3 = s_capsLock ? r3_upper : r3_lower;
        float r3_btnW = (kbWidth - 16.0f - (11 * gap)) / 12.0f;
        for (int i = 0; i < 12; i++) {
            if (DrawPassiveButton(r3[i], curX, curY, r3_btnW, btnH, btnBg, btnHov, btnPrs, txtCol)) {
                InsertTextAtCursor(r3[i]);
            }
            curX += r3_btnW + gap;
        }

        curY += btnH + gap;
        curX = startX + 8.0f;

        float closeW  = 65.0f;
        float arrowW  = 50.0f;
        float jumpW   = 200.0f;
        float spaceW  = kbWidth - 16.0f - (4 * gap) - closeW - (arrowW * 2.0f) - jumpW;

        if (DrawPassiveButton("CLOSE", curX, curY, closeW, btnH, IM_COL32(60, 30, 35, 235), IM_COL32(90, 40, 45, 255), btnPrs, txtCol)) {
            s_isOpen = false;
        }
        curX += closeW + gap;

        if (DrawPassiveButton("<", curX, curY, arrowW, btnH, btnBg, btnHov, btnPrs, txtCol)) {
            if (s_cursorPos > 0) {
                int p = s_cursorPos - 1;
                while (p > 0 && (static_cast<uint8_t>(s_targetBuf[p]) & 0xC0) == 0x80) {
                    p--;
                }
                s_cursorPos = p;
            }
        }
        curX += arrowW + gap;

        if (DrawPassiveButton(">", curX, curY, arrowW, btnH, btnBg, btnHov, btnPrs, txtCol)) {
            int curLen = s_targetBuf ? static_cast<int>(strlen(s_targetBuf)) : 0;
            if (s_cursorPos < curLen) {
                int p = s_cursorPos + 1;
                while (p < curLen && (static_cast<uint8_t>(s_targetBuf[p]) & 0xC0) == 0x80) {
                    p++;
                }
                s_cursorPos = p;
            }
        }
        curX += arrowW + gap;

        if (DrawPassiveButton("SPACE", curX, curY, spaceW, btnH, btnBg, btnHov, btnPrs, txtCol)) {
            InsertTextAtCursor(" ");
        }
        curX += spaceW + gap;

        ImU32 jBg  = IM_COL32(0, 204, 166, (int)(220 * s_kbAnim));
        ImU32 jHov = IM_COL32(25, 242, 204, (int)(255 * s_kbAnim));
        if (DrawPassiveButton(s_submitLabel.c_str(), curX, curY, jumpW, btnH, jBg, jHov, btnPrs, txtPrs)) {
            if (s_onSubmitCb) s_onSubmitCb();
            s_isOpen = false;
        }

        draw->PopClipRect();
    }
    ImGui::End();
}

} // namespace VirtualKeyboard
