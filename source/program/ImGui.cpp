#include "ImGui.hpp"
#include "macros.hpp"
#include "imgui/imgui_nvn.h"
#include <nn/fs.hpp>
#include <nn/os.hpp>
#include <hid.hpp>
#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>
#include <cmath>
#include <vector>
#include <set>
#include "InputOverlay.hpp"
#include "DebugMode.hpp"
#include "keyboard_sliders.hpp"
#include "StateSwitcher.hpp"
#include "MemoryScannerUi.hpp"
#include "ResScalerUI.hpp"

#ifndef IMNVNFUNC
#define IMNVNFUNC __attribute__((visibility("default")))
#endif

#define ADDR_UPDATE_BONES FIX(0x00252A70)

namespace ImGui {

    bool g_isMenuOpen = false;
    bool g_imguiHasFocus = true;

    static float* mikupos_a1[9999];
    static float* mikupos_a2[9999];
    static int mikuposptrcounter = 0;
    static int curmikupos = 0;
    static bool mikuoverride = false;
    static bool mikushowall = false;
    static int mikupos_a3[9999], mikupos_a4[9999], mikupos_a5[9999];

    static int g_maxBones = 0;

    static float g_cursorX = 640.0f;
    static float g_cursorY = 360.0f;

    // =====================================
    // DUMP STRUCTURES
    // =====================================
    struct FloatChange {
        int boneId;
        int arrType;
        int idx;
        float val;
    };

    struct DumpFrame {
        int framePct;
        std::vector<FloatChange> changes;
    };

    struct FloatSig {
        int boneId, arrType, idx;
        bool operator<(const FloatSig& o) const {
            if (boneId != o.boneId) return boneId < o.boneId;
            if (arrType != o.arrType) return arrType < o.arrType;
            return idx < o.idx;
        }
    };

    struct AnimDumpMemory {
        std::vector<DumpFrame> frames;
        bool hasData = false;
        int maxFrame = 0;
    };

    struct DumpFileInfo {
        std::string relPath;
        std::string fileName;
    };

    // =====================================
    // RECORDING SYSTEM
    // =====================================
    int g_recordState = 0;
    int g_recordFrameCounter = 0;
    int g_silenceCounter = 0;

    AnimDumpMemory g_recordBuffer;
    std::set<FloatSig> g_activeFloats;

    static float g_baseSnapshot_a1[1000][24];
    static float g_baseSnapshot_a2[1000][24];
    static float g_prevSnapshot_a1[1000][24];
    static float g_prevSnapshot_a2[1000][24];
    static bool g_hasBaseSnapshot = false;

    // =====================================
    // PLAYBACK SYSTEM
    // =====================================
    struct DynamicSlot {
        bool enabled = true;
        int selectedIndex = -1;
        float currentFrame = 0.0f;
        AnimDumpMemory dump;
    };

    std::vector<DynamicSlot> g_slots;
    std::vector<DumpFileInfo> g_allFiles;

    bool g_showUltimatePlayer = false;
    bool g_ultimateEnable = false;

    void EnsureDirectories() {
        nn::fs::DirectoryHandle dir;
        if (R_SUCCEEDED(nn::fs::OpenDirectory(&dir, "ExlSD:/MikuMikuSwitchPlugin/Dumps", 1))) {
            nn::fs::CloseDirectory(dir);
        } else {
            nn::fs::CreateDirectory("ExlSD:/MikuMikuSwitchPlugin/Dumps");
        }
    }

    void RefreshFiles() {
        EnsureDirectories();
        g_allFiles.clear();

        const char* baseDir = "ExlSD:/MikuMikuSwitchPlugin/Dumps";
        nn::fs::DirectoryHandle dir;
        int64_t readCount;
        nn::fs::DirectoryEntry entry;

        if (R_SUCCEEDED(nn::fs::OpenDirectory(&dir, baseDir, 2))) {
            while (R_SUCCEEDED(nn::fs::ReadDirectory(&readCount, &entry, dir, 1)) && readCount > 0) {
                std::string fname = entry.m_Name;
                if (fname.find(".txt") != std::string::npos) {
                    DumpFileInfo info;
                    info.relPath = fname;
                    info.fileName = fname;
                    g_allFiles.push_back(info);
                }
            }
            nn::fs::CloseDirectory(dir);
        }

        std::vector<std::string> subFolders;
        if (R_SUCCEEDED(nn::fs::OpenDirectory(&dir, baseDir, 1))) {
            while (R_SUCCEEDED(nn::fs::ReadDirectory(&readCount, &entry, dir, 1)) && readCount > 0) {
                std::string dname = entry.m_Name;
                if (dname != "." && dname != "..") {
                    subFolders.push_back(dname);
                }
            }
            nn::fs::CloseDirectory(dir);
        }

        for (const auto& folder : subFolders) {
            char subPath[256];
            snprintf(subPath, sizeof(subPath), "%s/%s", baseDir, folder.c_str());

            if (R_SUCCEEDED(nn::fs::OpenDirectory(&dir, subPath, 2))) {
                while (R_SUCCEEDED(nn::fs::ReadDirectory(&readCount, &entry, dir, 1)) && readCount > 0) {
                    std::string fname = entry.m_Name;
                    if (fname.find(".txt") != std::string::npos) {
                        DumpFileInfo info;
                        info.relPath = folder + "/" + fname;
                        info.fileName = fname;
                        g_allFiles.push_back(info);
                    }
                }
                nn::fs::CloseDirectory(dir);
            }
        }

        for (auto& slot : g_slots) {
            if (slot.selectedIndex >= (int)g_allFiles.size()) {
                slot.selectedIndex = -1;
                slot.dump.hasData = false;
            }
        }
    }

    std::string GenerateDumpFilename() {
        for (int i = 1; i <= 999; i++) {
            char fname[64];
            snprintf(fname, sizeof(fname), "motion_dump_%d.txt", i);
            char path[128];
            snprintf(path, sizeof(path), "ExlSD:/MikuMikuSwitchPlugin/Dumps/%s", fname);

            nn::fs::FileHandle h;
            if (R_FAILED(nn::fs::OpenFile(&h, path, nn::fs::OpenMode_Read))) {
                return std::string(fname);
            }
            nn::fs::CloseFile(h);
        }
        return "motion_dump_999.txt";
    }

    void SaveDumpToSD() {
        EnsureDirectories();
        std::string filename = GenerateDumpFilename();
        char path[128];
        snprintf(path, sizeof(path), "ExlSD:/MikuMikuSwitchPlugin/Dumps/%s", filename.c_str());

        std::string outData = "";
        char buf[128];

        snprintf(buf, sizeof(buf), "Frame 0\n");
        outData += buf;
        for (const auto& sig : g_activeFloats) {
            float v = (sig.arrType == 1) ? g_baseSnapshot_a1[sig.boneId][sig.idx] : g_baseSnapshot_a2[sig.boneId][sig.idx];
            snprintf(buf, sizeof(buf), "B %d %d %d %f\n", sig.boneId, sig.arrType, sig.idx, v);
            outData += buf;
        }

        for (const auto& frame : g_recordBuffer.frames) {
            snprintf(buf, sizeof(buf), "Frame %d\n", frame.framePct);
            outData += buf;
            for (const auto& c : frame.changes) {
                snprintf(buf, sizeof(buf), "B %d %d %d %f\n", c.boneId, c.arrType, c.idx, c.val);
                outData += buf;
            }
        }

        nn::fs::DeleteFile(path);
        nn::fs::CreateFile(path, outData.length());

        nn::fs::FileHandle h;
        if (R_SUCCEEDED(nn::fs::OpenFile(&h, path, nn::fs::OpenMode_Write))) {
            nn::fs::WriteFile(h, 0, outData.c_str(), outData.length(), nn::fs::WriteOption::CreateOption(nn::fs::WriteOptionFlag_Flush));
            nn::fs::CloseFile(h);
        }
    }

    void LoadDumpToSlot(DynamicSlot& slot) {
        if (slot.selectedIndex < 0 || slot.selectedIndex >= (int)g_allFiles.size()) {
            slot.dump.hasData = false;
            return;
        }

        char path[256];
        snprintf(path, sizeof(path), "ExlSD:/MikuMikuSwitchPlugin/Dumps/%s", g_allFiles[slot.selectedIndex].relPath.c_str());

        nn::fs::FileHandle h;
        if (R_SUCCEEDED(nn::fs::OpenFile(&h, path, nn::fs::OpenMode_Read))) {
            int64_t sz = 0;
            nn::fs::GetFileSize(&sz, h);

            std::vector<char> buf(sz + 1);
            nn::fs::ReadFile(h, 0, buf.data(), sz);
            nn::fs::CloseFile(h);
            buf[sz] = '\0';

            slot.dump.frames.clear();
            slot.dump.hasData = false;
            slot.dump.maxFrame = 0;

            DumpFrame currentFrame;
            bool hasFrame = false;

            char* line = strtok(buf.data(), "\r\n");
            while (line != nullptr) {
                if (strncmp(line, "Frame", 5) == 0) {
                    if (hasFrame) slot.dump.frames.push_back(currentFrame);
                    currentFrame.changes.clear();
                    sscanf(line, "Frame %d", &currentFrame.framePct);
                    hasFrame = true;
                }
                else if (strncmp(line, "B", 1) == 0) {
                    FloatChange c;
                    sscanf(line, "B %d %d %d %f", &c.boneId, &c.arrType, &c.idx, &c.val);
                    currentFrame.changes.push_back(c);
                }
                line = strtok(nullptr, "\r\n");
            }
            if (hasFrame) slot.dump.frames.push_back(currentFrame);

            if (slot.dump.frames.size() > 1) {
                for (size_t i = 1; i < slot.dump.frames.size(); i++) {
                    for (const auto& prevC : slot.dump.frames[i-1].changes) {
                        bool found = false;
                        for (const auto& curC : slot.dump.frames[i].changes) {
                            if (curC.boneId == prevC.boneId && curC.arrType == prevC.arrType && curC.idx == prevC.idx) {
                                found = true; break;
                            }
                        }
                        if (!found) {
                            slot.dump.frames[i].changes.push_back(prevC);
                        }
                    }
                }
            }

            slot.dump.hasData = !slot.dump.frames.empty();
            if (slot.dump.hasData) {
                slot.dump.maxFrame = slot.dump.frames.back().framePct;
            }
        }
    }

    void ApplySlotPlaybackLerp(DynamicSlot& slot, int boneId, float* out_a1, float* out_a2) {
        if (!slot.enabled || !slot.dump.hasData || slot.dump.frames.empty()) return;

        float targetPct = slot.currentFrame;
        const DumpFrame* leftFrame = &slot.dump.frames.front();
        const DumpFrame* rightFrame = &slot.dump.frames.back();

        for (size_t i = 0; i < slot.dump.frames.size() - 1; i++) {
            if (targetPct >= slot.dump.frames[i].framePct && targetPct <= slot.dump.frames[i+1].framePct) {
                leftFrame = &slot.dump.frames[i];
                rightFrame = &slot.dump.frames[i+1];
                break;
            }
        }

        float factor = (rightFrame->framePct == leftFrame->framePct) ? 0.0f : (targetPct - leftFrame->framePct) / (float)(rightFrame->framePct - leftFrame->framePct);

        for (const auto& lb : leftFrame->changes) {
            if (lb.boneId == boneId) {
                float rightVal = lb.val;
                for (const auto& rb : rightFrame->changes) {
                    if (rb.boneId == boneId && rb.arrType == lb.arrType && rb.idx == lb.idx) {
                        rightVal = rb.val; break;
                    }
                }

                float lerped = lb.val + (rightVal - lb.val) * factor;
                if (lb.arrType == 1) out_a1[lb.idx] = lerped;
                if (lb.arrType == 2) out_a2[lb.idx] = lerped;
            }
        }
    }

    // =====================================
    // BONE UPDATE HOOK
    // =====================================
    HOOK_DEFINE_TRAMPOLINE(BonesUpdateHook) {
        static uint64_t Callback(float* a1, float* a2, uint32_t a3, uint32_t a4, uint32_t a5) {

            if (mikuposptrcounter == -1) {
                mikuposptrcounter = 0;

                if (g_maxBones > 0) {
                    if (g_recordState == 1) {
                        if (!g_hasBaseSnapshot) {
                            for (int i = 0; i < g_maxBones && i < 1000; i++) {
                                if (mikupos_a1[i]) std::memcpy(g_baseSnapshot_a1[i], mikupos_a1[i], sizeof(float)*24);
                                if (mikupos_a2[i]) std::memcpy(g_baseSnapshot_a2[i], mikupos_a2[i], sizeof(float)*24);
                            }
                            g_hasBaseSnapshot = true;
                        }

                        bool motionStarted = false;
                        for (int i = 0; i < g_maxBones && i < 1000; i++) {
                            if (!mikupos_a1[i] || !mikupos_a2[i]) continue;
                            for (int j = 0; j < 24; j++) {
                                if (j == 8) continue;
                                if (std::abs(g_baseSnapshot_a1[i][j] - mikupos_a1[i][j]) > 0.0001f ||
                                    std::abs(g_baseSnapshot_a2[i][j] - mikupos_a2[i][j]) > 0.0001f) {
                                    motionStarted = true; break;
                                }
                            }
                            if (motionStarted) break;
                        }

                        if (motionStarted) {
                            g_recordState = 2; g_recordFrameCounter = 0; g_silenceCounter = 0;
                            g_recordBuffer.frames.clear(); g_activeFloats.clear();

                            for (int i = 0; i < g_maxBones && i < 1000; i++) {
                                if (mikupos_a1[i]) std::memcpy(g_prevSnapshot_a1[i], mikupos_a1[i], sizeof(float)*24);
                                if (mikupos_a2[i]) std::memcpy(g_prevSnapshot_a2[i], mikupos_a2[i], sizeof(float)*24);
                            }
                        }
                    }
                    else if (g_recordState == 2) {
                        g_recordFrameCounter++;
                        bool changedThisFrame = false;

                        for (int i = 0; i < g_maxBones && i < 1000; i++) {
                            if (!mikupos_a1[i] || !mikupos_a2[i]) continue;
                            for (int j = 0; j < 24; j++) {
                                if (j == 8) continue;
                                if (std::abs(g_prevSnapshot_a1[i][j] - mikupos_a1[i][j]) > 0.0001f ||
                                    std::abs(g_prevSnapshot_a2[i][j] - mikupos_a2[i][j]) > 0.0001f) {
                                    changedThisFrame = true;
                                    g_prevSnapshot_a1[i][j] = mikupos_a1[i][j];
                                    g_prevSnapshot_a2[i][j] = mikupos_a2[i][j];
                                }
                            }
                        }

                        if (changedThisFrame) g_silenceCounter = 0; else g_silenceCounter++;

                        if (g_recordFrameCounter % 5 == 0) {
                            DumpFrame diffFrame;
                            diffFrame.framePct = g_recordFrameCounter;

                            for (int i = 0; i < g_maxBones && i < 1000; i++) {
                                if (!mikupos_a1[i] || !mikupos_a2[i]) continue;
                                for (int j = 0; j < 24; j++) {
                                    if (j == 8) continue;
                                    if (std::abs(g_baseSnapshot_a1[i][j] - mikupos_a1[i][j]) > 0.0001f) {
                                        diffFrame.changes.push_back({i, 1, j, mikupos_a1[i][j]});
                                        g_activeFloats.insert({i, 1, j});
                                    }
                                    if (std::abs(g_baseSnapshot_a2[i][j] - mikupos_a2[i][j]) > 0.0001f) {
                                        diffFrame.changes.push_back({i, 2, j, mikupos_a2[i][j]});
                                        g_activeFloats.insert({i, 2, j});
                                    }
                                }
                            }
                            if (!diffFrame.changes.empty()) g_recordBuffer.frames.push_back(diffFrame);
                        }

                        if (g_silenceCounter >= 30) {
                            int trueEndFrame = g_recordFrameCounter - 30;
                            while (g_recordBuffer.frames.size() > 1 && g_recordBuffer.frames.back().framePct > trueEndFrame) {
                                g_recordBuffer.frames.pop_back();
                            }

                            SaveDumpToSD();
                            g_recordState = 0;
                            RefreshFiles();
                        }
                    }
                }
            }

            uint64_t output;

            if (mikuoverride) {
                output = Orig(a1, a2, mikupos_a3[mikuposptrcounter], mikupos_a4[mikuposptrcounter], mikupos_a5[mikuposptrcounter]);
            } else {
                output = Orig(a1, a2, a3, a4, a5);
            }

            if (g_ultimateEnable) {
                for (auto& slot : g_slots) {
                    ApplySlotPlaybackLerp(slot, mikuposptrcounter, a1, a2);
                }
            }

            if (!mikuoverride) {
                mikupos_a1[mikuposptrcounter] = a1; mikupos_a2[mikuposptrcounter] = a2;
                mikupos_a3[mikuposptrcounter] = a3; mikupos_a4[mikuposptrcounter] = a4; mikupos_a5[mikuposptrcounter] = a5;
            }

            mikuposptrcounter++;
            if (mikuposptrcounter > g_maxBones) g_maxBones = mikuposptrcounter;

            if (mikuposptrcounter >= 9999) mikuposptrcounter = 0;
            return output;
        }
    };

    void Init() {
        RefreshFiles();
        MemoryScannerUi::Init();
        BonesUpdateHook::InstallAtOffset(ADDR_UPDATE_BONES);
    }
}

extern "C" IMNVNFUNC ImGuiIO& nvnImguiGetIO() { return ImGui::GetIO(); }
extern "C" IMNVNFUNC void nvnImguiFontGetTexDataAsAlpha8(unsigned char** out_pixels, int* out_width, int* out_height, int* out_bytes_per_pixel) {
    ImGui::GetIO().Fonts->GetTexDataAsAlpha8(out_pixels, out_width, out_height, out_bytes_per_pixel);
}

extern "C" IMNVNFUNC void nvnImguiInitialize() {
    ImGui::CreateContext();

    ImGuiStyle& style = ImGui::GetStyle();

    // 1. Text: pure white
    style.Colors[ImGuiCol_Text]                  = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_TextDisabled]          = ImVec4(0.60f, 0.75f, 0.73f, 1.00f);

    // 2. Windows & Titlebars: Miku dark cyan background
    style.Colors[ImGuiCol_WindowBg]              = ImVec4(0.06f, 0.16f, 0.15f, 0.88f);
    style.Colors[ImGuiCol_TitleBg]               = ImVec4(0.12f, 0.38f, 0.36f, 0.90f);
    style.Colors[ImGuiCol_TitleBgActive]         = ImVec4(0.18f, 0.58f, 0.55f, 1.00f);
    style.Colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.08f, 0.25f, 0.24f, 0.75f);

    // 3. Buttons: bright Miku #47dfd3 teal base, ultra bright on hover
    style.Colors[ImGuiCol_Button]                = ImVec4(0.24f, 0.75f, 0.71f, 0.65f);
    style.Colors[ImGuiCol_ButtonHovered]         = ImVec4(0.33f, 0.95f, 0.90f, 0.85f);
    style.Colors[ImGuiCol_ButtonActive]          = ImVec4(0.16f, 0.52f, 0.49f, 0.95f);

    // 4. Headers & Collapsing tabs
    style.Colors[ImGuiCol_Header]                = ImVec4(0.24f, 0.75f, 0.71f, 0.60f);
    style.Colors[ImGuiCol_HeaderHovered]         = ImVec4(0.33f, 0.95f, 0.90f, 0.85f);
    style.Colors[ImGuiCol_HeaderActive]          = ImVec4(0.16f, 0.52f, 0.49f, 0.95f);

    // 5. Frames & Dropdowns
    style.Colors[ImGuiCol_FrameBg]               = ImVec4(0.10f, 0.30f, 0.28f, 0.60f);
    style.Colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.20f, 0.60f, 0.57f, 0.75f);
    style.Colors[ImGuiCol_FrameBgActive]         = ImVec4(0.16f, 0.52f, 0.49f, 0.95f);

    // 6. Sliders
    style.Colors[ImGuiCol_SliderGrab]            = ImVec4(0.33f, 0.95f, 0.90f, 0.85f);
    style.Colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.20f, 0.65f, 0.62f, 1.00f);

    // 7. Scrollbars
    style.Colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.05f, 0.14f, 0.13f, 0.50f);
    style.Colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.22f, 0.68f, 0.65f, 0.60f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.33f, 0.95f, 0.90f, 0.80f);
    style.Colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.16f, 0.52f, 0.49f, 0.95f);

    // 8. Checkmarks & Borders
    style.Colors[ImGuiCol_CheckMark]             = ImVec4(0.33f, 0.95f, 0.90f, 1.00f);
    style.Colors[ImGuiCol_Border]                = ImVec4(0.28f, 0.88f, 0.83f, 0.45f);
    style.Colors[ImGuiCol_Separator]             = ImVec4(0.28f, 0.88f, 0.83f, 0.45f);

    ImGui::GetIO().Fonts->AddFontDefault();
    ImGui::GetIO().Fonts->Build();
}

extern "C" IMNVNFUNC ImDrawData* nvnImguiCalc() {
    static uint64_t lastTick = 0;
    uint64_t currentTick = nn::os::GetSystemTick().GetInt64Value();
    float dt = 1.0f / 60.0f;
    if (lastTick != 0) {
        uint64_t freq = nn::os::GetSystemTickFrequency();
        if (freq > 0) {
            dt = (float)(currentTick - lastTick) / (float)freq;
            if (dt <= 0.0f || dt > 0.1f) dt = 1.0f / 60.0f;
        }
    }
    lastTick = currentTick;

    nn::hid::NpadHandheldState npad = nn::hid::GetMergedNpadState();

    // Main Menu Hotkey: Plus + Minus
    static float menu_hold_timer = 0.0f;
    if ((npad.buttons & nn::hid::Button::Plus) && (npad.buttons & nn::hid::Button::Minus)) {
        menu_hold_timer += dt;
        if (menu_hold_timer >= 0.8f && (menu_hold_timer - dt) < 0.8f) {
            ImGui::g_isMenuOpen = !ImGui::g_isMenuOpen;
            ImGui::g_imguiHasFocus = ImGui::g_isMenuOpen;
        }
    } else {
        menu_hold_timer = 0.0f;
    }

    // BSS Gaps Overlay Hotkey: L3 + R3 + ZL
    static float mem_hold_timer = 0.0f;
    bool isMemCombo = (npad.buttons & nn::hid::Button::LStick) &&
                      (npad.buttons & nn::hid::Button::RStick) &&
                      (npad.buttons & nn::hid::Button::ZL);

    if (isMemCombo) {
        mem_hold_timer += dt;
        if (mem_hold_timer >= 0.8f && (mem_hold_timer - dt) < 0.8f) {
            MemoryScannerUi::g_showWindow = !MemoryScannerUi::g_showWindow;
            if (MemoryScannerUi::g_showWindow) {
                ImGui::g_imguiHasFocus = true;
            }
        }
    } else {
        mem_hold_timer = 0.0f;
    }

    // Resolution Scaler Overlay Hotkey: L3 + R3 + ZR
    static float res_hold_timer = 0.0f;
    bool isResCombo = (npad.buttons & nn::hid::Button::LStick) &&
                      (npad.buttons & nn::hid::Button::RStick) &&
                      (npad.buttons & nn::hid::Button::ZR);

    if (isResCombo) {
        res_hold_timer += dt;
        if (res_hold_timer >= 0.8f && (res_hold_timer - dt) < 0.8f) {
            ResScalerUi::g_showWindow = !ResScalerUi::g_showWindow;
            if (ResScalerUi::g_showWindow) {
                ImGui::g_imguiHasFocus = true;
            }
        }
    } else {
        res_hold_timer = 0.0f;
    }

    // Input Overlay Hotkey: L3 + R3 (WITHOUT ZL and WITHOUT ZR)
    static float overlay_hold_timer = 0.0f;
    bool isOverlayCombo = (npad.buttons & nn::hid::Button::LStick) &&
                          (npad.buttons & nn::hid::Button::RStick) &&
                          !(npad.buttons & nn::hid::Button::ZL) &&
                          !(npad.buttons & nn::hid::Button::ZR);

    if (isOverlayCombo) {
        overlay_hold_timer += dt;
        if (overlay_hold_timer >= 0.8f && (overlay_hold_timer - dt) < 0.8f) {
            int nextMode = (InputOverlay::GetMode() + 1) % 3;
            InputOverlay::SetMode(nextMode);
        }
    } else {
        overlay_hold_timer = 0.0f;
    }

    // Keyboard Hotkey: F10
    static bool s_f10WasDown = false;
    bool isF10Down = keyboard_sliders::IsDown(67);
    if (isF10Down && !s_f10WasDown) {
        int curMode = InputOverlay::GetMode();
        InputOverlay::SetMode((curMode == InputOverlay::Mode_Keyboard) ? InputOverlay::Mode_Disabled : InputOverlay::Mode_Keyboard);
    }
    s_f10WasDown = isF10Down;

    bool overlayVisible = InputOverlay::IsVisible();
    bool anyUiOpen = ImGui::g_isMenuOpen || ImGui::g_showUltimatePlayer || MemoryScannerUi::g_showWindow || ResScalerUi::g_showWindow;

    if (!anyUiOpen && !overlayVisible) {
        ImGui::mikuposptrcounter = -1;
        return nullptr;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = dt;
    io.DisplaySize = ImVec2(1280.0f, 720.0f);

    bool isLeftClick = false, isRightClick = false, isTouchActive = false;

    // Direct hardware button reading
    bool isZLPressed = (npad.buttons & nn::hid::Button::ZL) != 0;
    bool isZRPressed = (npad.buttons & nn::hid::Button::ZR) != 0;

    // Toggle focus: Hold ZL + ZR for 0.5s
    if (anyUiOpen) {
        static float switchHoldTime = 0.0f;
        if (isZLPressed && isZRPressed) {
            if (switchHoldTime >= 0.0f) {
                switchHoldTime += dt;
                if (switchHoldTime >= 0.5f) {
                    ImGui::g_imguiHasFocus = !ImGui::g_imguiHasFocus;
                    switchHoldTime = -1.0f;
                }
            }
        } else {
            switchHoldTime = 0.0f;
        }
    }

    // Only update cursor position if ImGui currently has focus
    if (anyUiOpen && ImGui::g_imguiHasFocus) {
        if (nn::hid::GetTouchScreenState) {
            nn::hid::TouchScreenState ts = {};
            nn::hid::GetTouchScreenState(&ts);
            if (ts.count > 0) {
                ImGui::g_cursorX = (float)ts.touches[0].x;
                ImGui::g_cursorY = (float)ts.touches[0].y;
                isTouchActive = true;

                if (ts.count >= 2) {
                    isRightClick = true;
                } else {
                    isLeftClick = true;
                }
            }
        }

        if (nn::hid::GetMouseState && !isTouchActive) {
            nn::hid::MouseState ms = {};
            nn::hid::GetMouseState(&ms);
            ImGui::g_cursorX += (float)ms.deltaX;
            ImGui::g_cursorY += (float)ms.deltaY;
            if (ms.buttons & 1) isLeftClick = true;
            if (ms.buttons & 2) isRightClick = true;
        }

        if (!isTouchActive) {
            float lx = (float)npad.analogStickL[0] / 32767.0f;
            float ly = -(float)npad.analogStickL[1] / 32767.0f;
            if (std::abs(lx) > 0.15f || std::abs(ly) > 0.15f) {
                float speedMult = (npad.buttons & nn::hid::Button::Y) ? 12.0f : 5.0f;
                float frameComp = dt * 60.0f;
                ImGui::g_cursorX += lx * speedMult * frameComp;
                ImGui::g_cursorY += ly * speedMult * frameComp;
            }

            if (!(isZLPressed && isZRPressed)) {
                if (isZLPressed) isLeftClick = true;
                if (isZRPressed) isRightClick = true;
            }
        }

        ImGui::g_cursorX = std::clamp(ImGui::g_cursorX, 0.0f, 1280.0f);
        ImGui::g_cursorY = std::clamp(ImGui::g_cursorY, 0.0f, 720.0f);
    }

    // True only if interactive tool windows are open (not just HUD overlay)
    bool isFullWindowOpen = ImGui::g_isMenuOpen || ImGui::g_showUltimatePlayer;
    bool shouldDrawCursor = isFullWindowOpen && ImGui::g_imguiHasFocus && !isTouchActive;

    if (anyUiOpen && ImGui::g_imguiHasFocus) {
        io.AddMousePosEvent(ImGui::g_cursorX, ImGui::g_cursorY);
        io.AddMouseButtonEvent(0, isLeftClick);
        io.AddMouseButtonEvent(1, isRightClick);

        io.MouseDrawCursor = shouldDrawCursor;
        ImGui::SetMouseCursor(shouldDrawCursor ? ImGuiMouseCursor_Arrow : ImGuiMouseCursor_None);
    } else {
        io.AddMouseButtonEvent(0, false);
        io.AddMouseButtonEvent(1, false);
        io.MouseDrawCursor = false;
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    }

    ImGui::NewFrame();

    // =====================================
    // MAIN DEBUG WINDOW
    // =====================================
    if (ImGui::g_isMenuOpen) {
        ImGui::SetNextWindowBgAlpha(ImGui::g_imguiHasFocus ? 0.85f : 0.35f);
        std::string title = "Debug Ui" + std::string(ImGui::g_imguiHasFocus ? "" : " [GAME HAS FOCUS]") + "###MotionDebugWindow";

        ImGui::Begin(title.c_str(), &ImGui::g_isMenuOpen, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::GetWindowDrawList()->PushClipRectFullScreen();

        // 1. SMART MOTION RECORDER
        if (ImGui::CollapsingHeader("Smart Motion Recorder")) {
            ImGui::Separator();

            if (ImGui::g_recordState == 0) {
                if (ImGui::Button("Arm Recording (Wait for Motion)")) {
                    ImGui::g_recordState = 1;
                    ImGui::g_hasBaseSnapshot = false;
                }
                ImGui::SameLine(); ImGui::Text("Status: IDLE");
            }
            else if (ImGui::g_recordState == 1) {
                if (ImGui::Button("Cancel")) ImGui::g_recordState = 0;
                ImGui::SameLine(); ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Status: ARMED (Play Anim!)");
            }
            else if (ImGui::g_recordState == 2) {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Status: RECORDING... (Frame %d)", ImGui::g_recordFrameCounter);
                if (ImGui::Button("Stop & Save Now")) {
                    ImGui::SaveDumpToSD();
                    ImGui::g_recordState = 0;
                    ImGui::RefreshFiles();
                }
            }

            ImGui::Spacing(); ImGui::Separator();

            if (ImGui::Button("Refresh All Files")) ImGui::RefreshFiles();
            ImGui::SameLine();
            if (ImGui::Button("Open Ultimate Motion Player")) ImGui::g_showUltimatePlayer = true;
        }

        // 2. MOTION CONTROL
        if (ImGui::CollapsingHeader("Motion Control")) {
            ImGui::Separator();

            int displayBones = ImGui::g_maxBones;
            ImGui::InputInt("Total Bones", &displayBones, 0, 0, ImGuiInputTextFlags_ReadOnly);
            ImGui::InputInt("Current Bone", &ImGui::curmikupos);
            ImGui::Checkbox("Override Params", &ImGui::mikuoverride);
            ImGui::Checkbox("Show all params", &ImGui::mikushowall);

            if (ImGui::mikuposptrcounter >= 0) {
                ImGui::PushItemWidth(80);
                ImGui::InputInt("a3", &ImGui::mikupos_a3[ImGui::curmikupos]); ImGui::SameLine();
                ImGui::InputInt("a4", &ImGui::mikupos_a4[ImGui::curmikupos]); ImGui::SameLine();
                ImGui::InputInt("a5", &ImGui::mikupos_a5[ImGui::curmikupos]);
                ImGui::PopItemWidth();
            }

            if (ImGui::mikuposptrcounter >= 0 && ImGui::curmikupos < 9999) {
                float* m1 = ImGui::mikupos_a1[ImGui::curmikupos];
                float* m2 = ImGui::mikupos_a2[ImGui::curmikupos];
                int start = ImGui::mikushowall ? 0 : 15;

                for (int i = start; i <= 23; i++) {
                    ImGui::PushItemWidth(100);
                    if (m1) {
                        char lbl1[32]; snprintf(lbl1, 32, "##a1_%d", i);
                        ImGui::Text("%2d", i); ImGui::SameLine(); ImGui::DragFloat(lbl1, &m1[i], 0.01f);
                    }
                    if (m2) {
                        ImGui::SameLine(180);
                        char lbl2[32]; snprintf(lbl2, 32, "##a2_%d", i);
                        ImGui::Text("-%d", i); ImGui::SameLine(); ImGui::DragFloat(lbl2, &m2[i], 0.01f);
                    }
                    ImGui::PopItemWidth();
                }
            }
        }

        // 3. SCENE SWITCHER
        StateSwitcher::Draw();

        // 4. EXTRA OVERLAYS
        if (ImGui::CollapsingHeader("Extra Overlays")) {
            ImGui::Separator();

            const char* modeNames[] = { "Disabled", "Gamepad Overlay", "Keyboard Overlay" };
            int curMode = std::clamp(InputOverlay::GetMode(), 0, 2);

            ImGui::PushItemWidth(260);
            if (ImGui::BeginCombo("Active Overlay", modeNames[curMode])) {
                ImGui::GetWindowDrawList()->PushClipRectFullScreen();

                for (int n = 0; n < 3; n++) {
                    bool is_selected = (curMode == n);
                    if (ImGui::Selectable(modeNames[n], is_selected)) {
                        InputOverlay::SetMode(n);
                    }
                    if (is_selected) ImGui::SetItemDefaultFocus();
                }

                ImGui::GetWindowDrawList()->PopClipRect();
                ImGui::EndCombo();
            }
            ImGui::PopItemWidth();

            ImGui::Spacing();
            ImGui::Separator();

            // Toggle button to open/close BSS Gaps Overlay
            const char* bssBtnLabel = MemoryScannerUi::g_showWindow ? "Close BSS Gaps Overlay" : "Open BSS Gaps Overlay";
            if (ImGui::Button(bssBtnLabel, ImVec2(240, 28))) {
                MemoryScannerUi::g_showWindow = !MemoryScannerUi::g_showWindow;
                if (MemoryScannerUi::g_showWindow) {
                    ImGui::g_imguiHasFocus = true;
                }
            }

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.6f, 0.7f, 0.7f, 1.0f), "Input Overlays: Hold L3 + R3 (2 modes)");
            ImGui::TextColored(ImVec4(0.6f, 0.7f, 0.7f, 1.0f), "BSS Gaps Overlay: Hold L3 + R3 + ZL");
            ImGui::TextColored(ImVec4(0.6f, 0.7f, 0.7f, 1.0f), "Keyboard Overlay: Press F10");
        }

        ImGui::GetWindowDrawList()->PopClipRect();
        ImGui::End();
    }

    // =====================================
    // 5. ULTIMATE MOTION PLAYER
    // =====================================
    if (ImGui::g_showUltimatePlayer) {
        ImGui::SetNextWindowBgAlpha(ImGui::g_imguiHasFocus ? 0.90f : 0.40f);
        if (ImGui::Begin("Ultimate Motion Player", &ImGui::g_showUltimatePlayer, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::GetWindowDrawList()->PushClipRectFullScreen();

            ImGui::Checkbox("Enable Global Playback", &ImGui::g_ultimateEnable);
            ImGui::SameLine(250.0f);
            if (ImGui::Button("+ Add Motion Slot")) {
                ImGui::g_slots.push_back(ImGui::DynamicSlot());
            }
            ImGui::Separator();

            for (size_t i = 0; i < ImGui::g_slots.size(); i++) {
                auto& slot = ImGui::g_slots[i];
                ImGui::PushID((int)i);

                ImGui::Checkbox("##en", &slot.enabled);
                ImGui::SameLine();
                ImGui::PushItemWidth(250);

                if (ImGui::g_allFiles.empty()) {
                    ImGui::BeginDisabled();
                    if (ImGui::BeginCombo("##file", "No dumps found")) ImGui::EndCombo();
                    ImGui::EndDisabled();
                } else {
                    const char* preview = slot.selectedIndex >= 0 ? ImGui::g_allFiles[slot.selectedIndex].fileName.c_str() : "Select File...";
                    if (ImGui::BeginCombo("##file", preview)) {
                        ImGui::GetWindowDrawList()->PushClipRectFullScreen();
                        for (size_t n = 0; n < ImGui::g_allFiles.size(); n++) {
                            bool is_selected = (slot.selectedIndex == (int)n);
                            if (ImGui::Selectable(ImGui::g_allFiles[n].fileName.c_str(), is_selected)) {
                                slot.selectedIndex = (int)n;
                                ImGui::LoadDumpToSlot(slot);
                            }
                            if (is_selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::GetWindowDrawList()->PopClipRect();
                        ImGui::EndCombo();
                    }
                }
                ImGui::PopItemWidth();

                ImGui::SameLine();
                ImGui::PushItemWidth(150);
                float mx = slot.dump.hasData ? (float)slot.dump.maxFrame : 100.0f;
                ImGui::SliderFloat("Frame##frm", &slot.currentFrame, 0.0f, mx, "%.1f");
                ImGui::PopItemWidth();

                ImGui::SameLine();
                if (ImGui::Button(" X ")) {
                    ImGui::g_slots.erase(ImGui::g_slots.begin() + i);
                    ImGui::PopID();
                    i--;
                    continue;
                }
                ImGui::PopID();
            }

            ImGui::GetWindowDrawList()->PopClipRect();
        }
        ImGui::End();
    }

    // 6. BSS GAPS OVERLAY
    MemoryScannerUi::DrawWindow();

    // 7. RESOLUTION SCALER OVERLAY
    ResScalerUi::DrawWindow();

    // Unified Overlay Call
    InputOverlay::Draw();

    ImGui::Render();

    ImGui::mikuposptrcounter = -1;
    return ImGui::GetDrawData();
}
