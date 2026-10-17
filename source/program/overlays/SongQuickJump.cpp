#include "SongQuickJump.hpp"
#include "VirtualKeyboard.hpp"
#include "keyboard.hpp"
#include "lib.hpp"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include <string>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <vector>
#include <cmath>

#include "Config.hpp"
#include "nc/diva_nc.hpp"
#include "../nc/ui/customize_sel.hpp"
#include "../logger.hpp"

namespace ImGui {
    extern bool g_imguiHasFocus;
}

namespace SongQuickJump {

static constexpr uintptr_t OFFSET_GLOBAL_PV_INFO      = 0x4d243e70;
static constexpr uintptr_t ADDR_CHANGE_SORT_FILTER    = 0x883760;
static constexpr uintptr_t ADDR_CREATE_SORTED_PV_LIST = 0x8882e0;

using ChangeSortFilter_t   = void (*)(void* sel, int32_t play_sound);
using CreateSortedPVList_t = bool (*)(void* sel);

struct SongEntry {
    int32_t id;
    std::string name_jp;
    std::string name_en;
};

static std::vector<SongEntry> s_songDb;
static bool s_isPvSelActive = false;
static char s_inputBuf[256] = "";

static void* s_selectorPtr  = nullptr;
static int32_t s_targetPvId = -1;

static bool s_searchById    = false;

static float s_notFoundTimer = 0.0f;
static float s_notFoundAnim  = 0.0f;

static std::string ToLowerAscii(const std::string& str) {
    std::string res = str;
    for (char& c : res) {
        if (static_cast<unsigned char>(c) >= 'A' && static_cast<unsigned char>(c) <= 'Z') {
            c += ('a' - 'A');
        }
    }
    return res;
}

void ParsePvDbBuffer(const char* data, size_t length) {
    if (!data || length == 0) return;

    bool isUtf16 = (length >= 2 && (uint8_t)data[0] == 0xFF && (uint8_t)data[1] == 0xFE);
    std::string text;

    if (isUtf16) {
        const uint16_t* u16 = reinterpret_cast<const uint16_t*>(data + 2);
        size_t u16_len = (length - 2) / 2;
        text.reserve(u16_len);
        for (size_t i = 0; i < u16_len; i++) {
            uint16_t c = u16[i];
            if (c < 0x80) {
                text += (char)c;
            } else if (c < 0x800) {
                text += (char)(0xC0 | (c >> 6));
                text += (char)(0x80 | (c & 0x3F));
            } else {
                text += (char)(0xE0 | (c >> 12));
                text += (char)(0x80 | ((c >> 6) & 0x3F));
                text += (char)(0x80 | (c & 0x3F));
            }
        }
    } else {
        text.assign(data, length);
    }

    size_t pos = 0;
    while (pos < text.size()) {
        size_t nextPos = text.find('\n', pos);
        if (nextPos == std::string::npos) nextPos = text.size();

        std::string line = text.substr(pos, nextPos - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();

        pos = nextPos + 1;

        if (line.rfind("pv_", 0) != 0) continue;

        size_t dotPos = line.find('.', 3);
        if (dotPos == std::string::npos) continue;

        int id = std::atoi(line.substr(3, dotPos - 3).c_str());
        if (id <= 0) continue;

        size_t eqPos = line.find('=', dotPos);
        if (eqPos == std::string::npos) continue;

        std::string key = line.substr(dotPos, eqPos - dotPos);
        std::string val = line.substr(eqPos + 1);

        SongEntry* entry = nullptr;
        for (auto& e : s_songDb) {
            if (e.id == id) {
                entry = &e;
                break;
            }
        }
        if (!entry) {
            s_songDb.push_back({ id, "", "" });
            entry = &s_songDb.back();
        }

        if (key == ".song_name") {
            entry->name_jp = val;
        } else if (key == ".song_name_en") {
            entry->name_en = val;
        }
    }
}

void SetPvSelActive(bool active, void* sel) {
    s_isPvSelActive = active;
    s_selectorPtr = sel;
    if (!active) {
        VirtualKeyboard::Close();
        s_selectorPtr = nullptr;
        s_notFoundTimer = 0.0f;
        s_notFoundAnim = 0.0f;
        s_targetPvId = -1;
    }
}

bool IsInCustomize() {
    return CustomizeSelUi::IsInCustomizeScene() || CustomizeSelUi::IsOpen();
}

bool IsVisible() {
    return s_isPvSelActive && !IsInCustomize();
}

static bool DoesPvExistInDb(int32_t id) {
    if (id <= 0) return false;
    for (const auto& song : s_songDb) {
        if (song.id == id) return true;
    }
    return false;
}

static std::string CleanUtf8String(const std::string& str) {
    std::string s = str;
    std::string normalized;
    for (size_t i = 0; i < s.size(); ) {
        if (i + 2 < s.size() && (uint8_t)s[i] == 0xEF && (uint8_t)s[i+1] == 0xBC && (uint8_t)s[i+2] >= 0x90 && (uint8_t)s[i+2] <= 0x99) {
            normalized += (char)('0' + ((uint8_t)s[i+2] - 0x90));
            i += 3;
        } else {
            normalized += s[i];
            i++;
        }
    }
    s = normalized;

    while (!s.empty()) {
        if ((uint8_t)s[0] <= 0x20) s.erase(0, 1);
        else if (s.size() >= 3 && (uint8_t)s[0] == 0xE3 && (uint8_t)s[1] == 0x80 && (uint8_t)s[2] == 0x80) s.erase(0, 3);
        else break;
    }
    while (!s.empty()) {
        if ((uint8_t)s.back() <= 0x20) s.pop_back();
        else if (s.size() >= 3 && (uint8_t)s[s.size() - 3] == 0xE3 && (uint8_t)s[s.size() - 2] == 0x80 && (uint8_t)s[s.size() - 1] == 0x80) s.erase(s.size() - 3, 3);
        else break;
    }
    return s;
}

static std::vector<std::string> ExtractAlnumWords(const std::string& str) {
    std::vector<std::string> words;
    std::string current;
    size_t i = 0;
    while (i < str.size()) {
        unsigned char c = (unsigned char)str[i];
        if (i + 2 < str.size() && (uint8_t)str[i] == 0xE3 && (uint8_t)str[i+1] == 0x80 && (uint8_t)str[i+2] == 0x80) {
            if (!current.empty()) { words.push_back(current); current.clear(); }
            i += 3; continue;
        }
        if (std::isalnum(c)) { current += (char)std::tolower(c); i++; }
        else if (c >= 0x80) { current += (char)c; i++; }
        else {
            if (!current.empty()) { words.push_back(current); current.clear(); }
            i++;
        }
    }
    if (!current.empty()) words.push_back(current);
    return words;
}

static int32_t ResolvePvId(const std::string& rawQuery) {
    std::string cleanQuery = CleanUtf8String(rawQuery);
    if (cleanQuery.empty()) return -1;

    if (s_searchById) {
        if (std::all_of(cleanQuery.begin(), cleanQuery.end(), ::isdigit)) {
            int id = std::atoi(cleanQuery.c_str());
            if (id > 0 && DoesPvExistInDb(id)) return id;
        }
        return -1;
    }

    std::string lowerQuery = ToLowerAscii(cleanQuery);

    for (const auto& entry : s_songDb) {
        if (entry.name_jp == cleanQuery) return entry.id;
        if (!entry.name_en.empty()) {
            std::string lowerEn = ToLowerAscii(entry.name_en);
            if (lowerEn == lowerQuery) return entry.id;
        }
    }

    for (const auto& entry : s_songDb) {
        if (!entry.name_jp.empty() && entry.name_jp.find(cleanQuery) != std::string::npos) return entry.id;
        if (!entry.name_en.empty()) {
            std::string lowerEn = ToLowerAscii(entry.name_en);
            if (lowerEn.find(lowerQuery) != std::string::npos) return entry.id;
        }
    }

    auto queryWords = ExtractAlnumWords(cleanQuery);
    if (!queryWords.empty()) {
        for (const auto& entry : s_songDb) {
            std::string titleCombined = ToLowerAscii(entry.name_jp + " " + entry.name_en);
            bool allWordsPresent = true;
            for (const auto& w : queryWords) {
                if (titleCombined.find(w) == std::string::npos) { allWordsPresent = false; break; }
            }
            if (allWordsPresent) return entry.id;
        }
    }

    if (std::all_of(cleanQuery.begin(), cleanQuery.end(), ::isdigit)) {
        int id = std::atoi(cleanQuery.c_str());
        if (id > 0 && DoesPvExistInDb(id)) return id;
    }

    return -1;
}

static void ExecuteSearchAndJump() {
    int32_t id = ResolvePvId(s_inputBuf);
    if (id > 0) {
        s_targetPvId = id;
        s_notFoundTimer = 0.0f;
    } else {
        s_targetPvId = -1;
        s_notFoundTimer = 2.8f;
    }
}

static void ProcessJumpLogic() {
    if (s_targetPvId <= 0) return;

    uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
    uintptr_t pv_info = base + OFFSET_GLOBAL_PV_INFO;

    if (s_selectorPtr != nullptr) {
        uintptr_t sel = reinterpret_cast<uintptr_t>(s_selectorPtr);

        int32_t mode = *reinterpret_cast<int32_t*>(sel + 0x26198);
        if (mode < 0 || mode > 3) mode = 1;

        int32_t all_folder = 19;
        switch (mode) {
            case 0: all_folder = 10; break;
            case 1: all_folder = 19; break;
            case 2: all_folder = 6;  break;
            case 3: all_folder = 5;  break;
        }

        *reinterpret_cast<int32_t*>(sel + 0x261A0) = 3;
        *reinterpret_cast<int32_t*>(sel + 0x261A4) = 0;

        int32_t list_idx = (8 * mode) + (2 * 3) + 0;
        uintptr_t extreme_vec_ptr = sel + 0x25DD0 + (list_idx * 0x18);
        *reinterpret_cast<uintptr_t*>(sel + 0x260D0) = extreme_vec_ptr;

        auto CreateSortedPVList = reinterpret_cast<CreateSortedPVList_t>(base + ADDR_CREATE_SORTED_PV_LIST);
        CreateSortedPVList(reinterpret_cast<void*>(sel));

        *reinterpret_cast<int32_t*>(sel + 0x2617C) = s_targetPvId;
        *reinterpret_cast<int32_t*>(sel + 0x26184 + (mode * 4)) = all_folder;

        int32_t* folder_ptr = reinterpret_cast<int32_t*>(sel + 0x26184 + (mode * 4));
        *reinterpret_cast<int32_t**>(sel + 0x26118) = folder_ptr;

        *reinterpret_cast<int32_t*>(sel + 0x26194) = 1;

        *reinterpret_cast<int32_t*>(pv_info + 0x04) = s_targetPvId;
        *reinterpret_cast<int32_t*>(pv_info + 0x08) = mode;
        *reinterpret_cast<int32_t*>(pv_info + 0x0C) = 3;
        *reinterpret_cast<int32_t*>(pv_info + 0x10) = all_folder;

        *reinterpret_cast<uint8_t*>(sel + 0x25998) = 0;
        *reinterpret_cast<uint8_t*>(sel + 0x25999) = 1;

        auto ChangeSortFilter = reinterpret_cast<ChangeSortFilter_t>(base + ADDR_CHANGE_SORT_FILTER);
        ChangeSortFilter(reinterpret_cast<void*>(sel), 0);

        sound::PlaySoundEffectWrapped("se_ft_music_selector_sortfilter_change_01", 1.0f);
    }

    s_targetPvId = -1;
}

void Draw() {
    ProcessJumpLogic();

    if (!IsVisible()) return;

    ImGui::g_imguiHasFocus = true;

    ImGuiIO& io = ImGui::GetIO();
    float dt = io.DeltaTime;

    if (s_notFoundTimer > 0.0f) {
        s_notFoundTimer -= dt;
        s_notFoundAnim += (1.0f - s_notFoundAnim) * std::min(dt * 12.0f, 1.0f);
    } else {
        s_notFoundAnim += (0.0f - s_notFoundAnim) * std::min(dt * 12.0f, 1.0f);
    }

    const float posX = 800.0f;
    const float posY = 44.0f;
    const float barW = 300.0f;
    const float barH = 34.0f;

    const float topBtnW = 70.0f;
    const float topTabH = 25.0f;
    const float bannerH = 26.0f;

    ImGui::SetNextWindowPos(ImVec2(posX, posY - topTabH), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(barW, barH + topTabH + bannerH + 6.0f), ImGuiCond_Always);
    ImGui::SetNextWindowCollapsed(false, ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border,   ImVec4(0, 0, 0, 0));

    if (ImGui::Begin("##QuickSongJumpBar", nullptr, flags)) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->PushClipRectFullScreen();

        const ImU32 colBarBg   = IM_COL32(18, 22, 26, 240);
        const ImU32 colBarBdr  = IM_COL32(255, 255, 255, 35);
        const ImU32 colBtnFlat = IM_COL32(32, 38, 46, 245);
        const ImU32 colText    = IM_COL32(235, 240, 245, 255);

        if (s_notFoundAnim > 0.01f) {
            float curBannerY = posY + barH - 2.0f + (bannerH * s_notFoundAnim);
            ImVec2 bMin(posX + 15.0f, posY + barH - 2.0f);
            ImVec2 bMax(posX + barW - 15.0f, curBannerY);

            draw->AddRectFilled(bMin, bMax, IM_COL32(40, 20, 24, (int)(245 * s_notFoundAnim)), 6.0f, ImDrawFlags_RoundCornersBottom);
            draw->AddRect(bMin, bMax, IM_COL32(245, 65, 85, (int)(225 * s_notFoundAnim)), 6.0f, ImDrawFlags_RoundCornersBottom, 1.2f);

            ImFont* font = ImGui::GetFont();
            float bFontSize = font->FontSize * 0.95f;
            const char* notFoundTxt = "Song not found!";
            ImVec2 txtSz = font->CalcTextSizeA(bFontSize, FLT_MAX, 0.0f, notFoundTxt);

            ImVec2 txtPos(bMin.x + (bMax.x - bMin.x - txtSz.x) * 0.5f, bMin.y + (bMax.y - bMin.y - txtSz.y) * 0.5f);
            draw->AddText(font, bFontSize, txtPos, IM_COL32(255, 190, 200, (int)(255 * s_notFoundAnim)), notFoundTxt);
        }

        draw->AddRectFilled(ImVec2(posX, posY), ImVec2(posX + barW, posY + barH), colBarBg, 6.0f);
        draw->AddRect(ImVec2(posX, posY), ImVec2(posX + barW, posY + barH), colBarBdr, 6.0f, 0, 1.2f);

        ImFont* font = ImGui::GetFont();
        float badgeFontSize = font->FontSize * 1.15f;

        const float topBtnX = posX;
        const float topBtnY = posY - topTabH + 1.0f;
        ImVec2 topMin(topBtnX, topBtnY);
        ImVec2 topMax(topBtnX + topBtnW, posY + 1.0f);
        bool topHov = (io.MousePos.x >= topMin.x && io.MousePos.x <= topMax.x && io.MousePos.y >= topMin.y && io.MousePos.y <= topMax.y);
        if (topHov && ImGui::IsMouseClicked(0)) s_searchById = !s_searchById;
        draw->AddRectFilled(topMin, topMax, colBarBg, 6.0f, ImDrawFlags_RoundCornersAll);
        draw->AddRect(topMin, topMax, colBarBdr, 6.0f, ImDrawFlags_RoundCornersAll, 1.2f);

        const char* topLabel = s_searchById ? "ID" : "NAME";
        ImVec2 topSz = font->CalcTextSizeA(badgeFontSize, FLT_MAX, 0.0f, topLabel);
        draw->AddText(font, badgeFontSize, ImVec2(topMin.x + (topBtnW - topSz.x) * 0.5f, topMin.y + (topTabH - topSz.y) * 0.5f), colText, topLabel);

        const float kbBtnW = 44.0f;
        const float kbBtnH = barH - 8.0f;
        const float kbBtnX = posX + 5.0f;
        const float kbBtnY = posY + 4.0f;
        ImVec2 kbMin(kbBtnX, kbBtnY);
        ImVec2 kbMax(kbBtnX + kbBtnW, kbBtnY + kbBtnH);
        bool kbHov = (io.MousePos.x >= kbMin.x && io.MousePos.x <= kbMax.x && io.MousePos.y >= kbMin.y && io.MousePos.y <= kbMax.y);

        if (kbHov && ImGui::IsMouseClicked(0)) {
            VirtualKeyboard::Open(s_inputBuf, sizeof(s_inputBuf), "JUMP TO SONG", ExecuteSearchAndJump);
        }

        draw->AddRectFilled(kbMin, kbMax, colBtnFlat, 4.0f);
        draw->AddRect(kbMin, kbMax, colBarBdr, 4.0f, 0, 1.0f);
        const char* kbLabel = "KB";
        ImVec2 kbSz = font->CalcTextSizeA(badgeFontSize, FLT_MAX, 0.0f, kbLabel);
        draw->AddText(font, badgeFontSize, ImVec2(kbMin.x + (kbBtnW - kbSz.x) * 0.5f, kbMin.y + (kbBtnH - kbSz.y) * 0.5f), colText, kbLabel);

        const float jumpW = 34.0f;
        const float jumpH = barH - 8.0f;
        const float jumpX = posX + barW - jumpW - 5.0f;
        const float jumpY = posY + 4.0f;
        ImVec2 jMin(jumpX, jumpY);
        ImVec2 jMax(jumpX + jumpW, jumpY + jumpH);
        bool jHov = (io.MousePos.x >= jMin.x && io.MousePos.x <= jMax.x && io.MousePos.y >= jMin.y && io.MousePos.y <= jMax.y);
        if (jHov && ImGui::IsMouseClicked(0)) ExecuteSearchAndJump();
        draw->AddRectFilled(jMin, jMax, jHov ? IM_COL32(0, 235, 185, 255) : IM_COL32(0, 200, 160, 235), 4.0f);
        draw->AddRect(jMin, jMax, IM_COL32(255, 255, 255, 120), 4.0f, 0, 1.0f);
        float jFontSize = font->FontSize * 1.20f;
        ImVec2 jSz = font->CalcTextSizeA(jFontSize, FLT_MAX, 0.0f, "->");
        draw->AddText(font, jFontSize, ImVec2(jMin.x + (jumpW - jSz.x) * 0.5f, jMin.y + (jumpH - jSz.y) * 0.5f), IM_COL32(20, 25, 30, 255), "->");

        const float inX = kbBtnX + kbBtnW + 5.0f;
        const float inW = (jumpX - 5.0f) - inX;

        ImGui::SetCursorScreenPos(ImVec2(inX, posY + 4.0f));
        ImGui::SetNextItemWidth(inW);

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 7.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.10f, 0.13f, 0.16f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.14f, 0.18f, 0.22f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.98f, 1.00f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(0.55f, 0.60f, 0.68f, 0.85f));

        const char* hint = s_searchById ? "PV ID..." : "Song Name...";

        // Synchronize callback: active only when virtual keyboard is open
        auto InputTextScrollCallback = [](ImGuiInputTextCallbackData* data) -> int {
            if (VirtualKeyboard::IsOpen()) {
                if (strcmp(data->Buf, s_inputBuf) != 0) {
                    data->DeleteChars(0, data->BufTextLen);
                    data->InsertChars(0, s_inputBuf);
                }

                int curPos = VirtualKeyboard::GetCursorPos();
                if (curPos >= 0 && curPos <= data->BufTextLen) {
                    data->CursorPos = curPos;
                    data->SelectionStart = curPos;
                    data->SelectionEnd = curPos;
                }
                return VirtualKeyboard::InputTextCallback(data);
            }

            // When virtual keyboard is closed, allow native ImGui input management
            return 0;
        };

        if (VirtualKeyboard::IsOpen()) {
            ImGui::SetKeyboardFocusHere(0);
        }

        // Enter key triggers search
        bool enterPressed = ImGui::InputTextWithHint(
            "##song_input", hint, s_inputBuf, sizeof(s_inputBuf),
            ImGuiInputTextFlags_CallbackAlways | ImGuiInputTextFlags_EnterReturnsTrue,
            InputTextScrollCallback
        );

        if (enterPressed) {
            ExecuteSearchAndJump();
        }

        // Escape key clears the input buffer
        if (io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            s_inputBuf[0] = '\0';
        }

        // Open virtual keyboard on click unless physical keyboard was recently used
        if (ImGui::IsItemClicked()) {
            if (!keyboard::IsRecentlyUsed()) {
                VirtualKeyboard::Open(s_inputBuf, sizeof(s_inputBuf), "JUMP TO SONG", ExecuteSearchAndJump);
            }
        }

        // Blinking cursor for virtual keyboard mode
        if (VirtualKeyboard::IsOpen()) {
            ImVec2 itemMin = ImGui::GetItemRectMin();
            ImVec2 itemMax = ImGui::GetItemRectMax();

            int curPos = VirtualKeyboard::GetCursorPos();
            if (curPos < 0) curPos = 0;
            int textLen = static_cast<int>(strlen(s_inputBuf));
            if (curPos > textLen) curPos = textLen;

            float textWidth = 0.0f;
            if (curPos > 0) {
                textWidth = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, s_inputBuf, s_inputBuf + curPos).x;
            }

            ImGuiInputTextState* state = ImGui::GetInputTextState(ImGui::GetID("##song_input"));
            float scrollX = (state != nullptr) ? state->ScrollX : 0.0f;

            float cursorX  = itemMin.x + 6.0f + textWidth - scrollX;
            float cursorY1 = itemMin.y + 5.0f;
            float cursorY2 = itemMax.y - 5.0f;

            if (cursorX >= itemMin.x + 4.0f && cursorX <= itemMax.x - 4.0f) {
                static float s_blinkTimer = 0.0f;
                s_blinkTimer += io.DeltaTime;
                if (fmodf(s_blinkTimer, 0.9f) < 0.55f) {
                    draw->AddLine(ImVec2(cursorX, cursorY1), ImVec2(cursorX, cursorY2), IM_COL32(255, 255, 255, 255), 1.5f);
                }
            }
        }

        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);

        draw->PopClipRect();
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
}

} // namespace SongQuickJump
