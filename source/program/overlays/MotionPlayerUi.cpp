#include "MotionPlayerUi.hpp"
#include "VirtualKeyboard.hpp"
#include "macros.hpp"
#include "imgui/imgui.h"
#include "lib.hpp"

#include <nn/fs.hpp>
#include <vector>
#include <string>
#include <set>
#include <cmath>
#include <cstring>
#include <algorithm>

#define ADDR_UPDATE_BONES FIX(0x00252A70)

namespace ImGui {
    extern bool g_imguiHasFocus;
}

namespace MotionPlayerUi {

bool g_showUltimatePlayer = false;
bool g_showMotionControlWindow = false;
static bool g_ultimateEnable = false;
static bool s_showBoneHelp = false;

static float* mikupos_a1[9999];
static float* mikupos_a2[9999];
static int mikuposptrcounter = 0;
static int curmikupos = 0;
static bool mikuoverride = false;
static bool mikushowall = false;
static int mikupos_a3[9999], mikupos_a4[9999], mikupos_a5[9999];

static int g_maxBones = 0;
static int s_currentFrameBones = 0;

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

// 0=IDLE, 1=ARMED, 2=REC, 3=PENDING
static int g_recordState = 0;
static int g_recordFrameCounter = 0;
static int g_silenceCounter = 0;
static char s_motionFileName[64] = "motion_dump_1.txt";

static AnimDumpMemory g_recordBuffer;
static std::set<FloatSig> g_activeFloats;

static float g_baseSnapshot_a1[1000][24];
static float g_baseSnapshot_a2[1000][24];
static float g_prevSnapshot_a1[1000][24];
static float g_prevSnapshot_a2[1000][24];
static bool g_hasBaseSnapshot = false;

struct DynamicSlot {
    bool enabled = true;
    int selectedIndex = -1;
    float currentFrame = 0.0f;
    AnimDumpMemory dump;
};

static std::vector<DynamicSlot> g_slots;
static std::vector<DumpFileInfo> g_allFiles;

static void EnsureDirectories() {
    nn::fs::DirectoryHandle dir;
    if (R_SUCCEEDED(nn::fs::OpenDirectory(&dir, "ExlSD:/MikuMikuSwitchPlugin/Dumps", 1))) {
        nn::fs::CloseDirectory(dir);
    } else {
        nn::fs::CreateDirectory("ExlSD:/MikuMikuSwitchPlugin/Dumps");
    }
}

static void RefreshFiles() {
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

static std::string GenerateDumpFilename() {
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

static void SaveDumpToSD(const char* custom_name = nullptr) {
    EnsureDirectories();
    std::string filename = "";
    if (custom_name && custom_name[0] != '\0') {
        filename = custom_name;
        if (filename.find(".txt") == std::string::npos) filename += ".txt";
    } else {
        filename = GenerateDumpFilename();
        strncpy(s_motionFileName, filename.c_str(), sizeof(s_motionFileName));
    }

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

static void LoadDumpToSlot(DynamicSlot& slot) {
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

static void ApplySlotPlaybackLerp(DynamicSlot& slot, int boneId, float* out_a1, float* out_a2) {
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

HOOK_DEFINE_TRAMPOLINE(BonesUpdateHook) {
    static uint64_t Callback(float* a1, float* a2, uint32_t a3, uint32_t a4, uint32_t a5) {

        if (mikuposptrcounter == -1) {
            if (s_currentFrameBones > 0) {
                g_maxBones = s_currentFrameBones;
            }
            s_currentFrameBones = 0;
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
                        g_recordState = 3;
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

        s_currentFrameBones++;
        mikuposptrcounter++;

        if (mikuposptrcounter >= 9999) mikuposptrcounter = 0;
        return output;
    }
};

void ResetPtrCounter() {
    mikuposptrcounter = -1;
}

void Init() {
    RefreshFiles();
    BonesUpdateHook::InstallAtOffset(ADDR_UPDATE_BONES);
}

struct BoneGuideItem {
    int bone;
    const char* range;
    const char* desc;
};

static const BoneGuideItem s_boneGuideList[] = {
    { 0,   "0-3",     "Body and pelvis rotations" },
    { 3,   "3-6",     "Neck and head rotation" },
    { 6,   "6-91",    "Face stuff, just use facemot" },
    { 92,  "92-97",   "Left shoulder" },
    { 96,  "96-98",   "Left wrist" },
    { 98,  "98-101",  "Left hand index finger" },
    { 102, "102-105", "Left hand smallest finger" },
    { 105, "105-109", "Left hand 4th finger" },
    { 109, "109-113", "Left hand middle finger" },
    { 113, "113-117", "Left hand thumb" },
    { 124, "124-129", "Right shoulder" },
    { 128, "128-130", "Right wrist" },
    { 130, "130-133", "Right hand index finger" },
    { 134, "134-137", "Right hand smallest finger" },
    { 137, "137-140", "Right hand 4th finger" },
    { 141, "141-145", "Right hand middle finger" },
    { 144, "144-148", "Right hand thumb" },
    { 155, "155-157", "Left arm" },
    { 157, "157-159", "Right arm" },
    { 160, "160-162", "Waist rotation" },
    { 162, "162-164", "Left leg" },
    { 164, "164-166", "Left foot" },
    { 166, "166-168", "Right leg" },
    { 168, "168-170", "Right foot" },
    { 178, "178-181", "XYZ dimensions" }
};

static int s_bonePage = 0; // 0 = Page 1, 1 = Page 2

static void DrawBonesGuideOverlay(const ImVec2& parentPos, const ImVec2& parentSize) {
    const float helpW = 240.0f;
    const float helpH = std::max(parentSize.y, 380.0f);

    float helpX = parentPos.x + parentSize.x + 6.0f;
    float helpY = parentPos.y;

    if (helpX + helpW > 1272.0f) {
        helpX = parentPos.x - helpW - 6.0f;
    }
    if (helpX < 8.0f) helpX = 8.0f;

    ImGui::SetNextWindowPos(ImVec2(helpX, helpY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(helpW, helpH), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.94f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse |
                             ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));

    if (ImGui::Begin("Bones Guide###MotionBonesGuideWindow", &s_showBoneHelp, flags)) {
        ImGui::GetWindowDrawList()->PushClipRectFullScreen();

        ImGui::TextColored(ImVec4(0.28f, 0.88f, 0.83f, 1.0f), "Motion ImGUI");
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "* Note: not 100%% accurate!");
        ImGui::Separator();
        ImGui::Spacing();

        // Split 25 items across 2 pages: Page 1 = 0..12, Page 2 = 13..24
        int startIdx = (s_bonePage == 0) ? 0 : 13;
        int endIdx   = (s_bonePage == 0) ? 13 : 25;

        const float colBtnW = 60.0f;
        const float textAvailW = helpW - 16.0f - colBtnW - 8.0f;

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 2.5f));

        for (int i = startIdx; i < endIdx; i++) {
            const auto& item = s_boneGuideList[i];
            ImGui::PushID(item.range);

            if (ImGui::Button(item.range, ImVec2(colBtnW, 18.0f))) {
                curmikupos = item.bone;
            }

            ImGui::SameLine(0.0f, 6.0f);

            ImGui::BeginGroup();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textAvailW);
            ImGui::TextUnformatted(item.desc);
            ImGui::PopTextWrapPos();
            ImGui::EndGroup();

            ImGui::PopID();
        }

        ImGui::PopStyleVar(2);

        ImGui::SetCursorPosY(helpH - 30.0f);
        ImGui::Separator();

        const float navBtnW = 26.0f;
        if (ImGui::Button("<##PrevBonePage", ImVec2(navBtnW, 19.0f))) {
            if (s_bonePage > 0) s_bonePage--;
        }

        ImGui::SameLine();
        ImGui::SetCursorPosX((helpW - 65.0f) * 0.5f);
        ImGui::Text("Page %d / 2", s_bonePage + 1);

        ImGui::SameLine(helpW - navBtnW - 8.0f);
        if (ImGui::Button(">##NextBonePage", ImVec2(navBtnW, 19.0f))) {
            if (s_bonePage < 1) s_bonePage++;
        }

        ImGui::GetWindowDrawList()->PopClipRect();
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void DrawRecorderHeader() {
    ImGui::Separator();

    if (g_recordState == 0) {
        if (ImGui::Button("Record", ImVec2(75.0f, 24.0f))) {
            g_recordState = 1;
            g_hasBaseSnapshot = false;
        }
        ImGui::SameLine();
        ImGui::Text("IDLE");
    } else if (g_recordState == 1) {
        if (ImGui::Button("Cancel", ImVec2(75.0f, 24.0f))) g_recordState = 0;
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "ARMED");
    } else if (g_recordState == 2) {
        if (ImGui::Button("Stop", ImVec2(75.0f, 24.0f))) {
            g_recordState = 3;
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "REC (%d)", g_recordFrameCounter);
    } else if (g_recordState == 3) {
        if (ImGui::Button("Drop", ImVec2(50.0f, 24.0f))) g_recordState = 0;
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "PENDING");

        ImGui::SameLine();
        ImGui::PushItemWidth(125.0f);

        if (VirtualKeyboard::ShouldFocusTarget()) {
            ImGui::SetKeyboardFocusHere(0);
        }

        ImGui::InputText(
            "##mot_name", s_motionFileName, sizeof(s_motionFileName),
            ImGuiInputTextFlags_CallbackAlways, VirtualKeyboard::InputTextCallback
        );
        if (ImGui::IsItemClicked()) {
            VirtualKeyboard::Open(s_motionFileName, sizeof(s_motionFileName), "SAVE MOTION", []() {
                SaveDumpToSD(s_motionFileName);
                g_recordState = 0;
                RefreshFiles();
            });
        }
        ImGui::PopItemWidth();
    }

    ImGui::Spacing();
    if (g_recordState == 3) {
        if (ImGui::Button("Save Motion", ImVec2(90.0f, 24.0f))) {
            SaveDumpToSD(s_motionFileName);
            g_recordState = 0;
            RefreshFiles();
        }
    } else {
        ImGui::BeginDisabled();
        ImGui::Button("Save Motion", ImVec2(90.0f, 24.0f));
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (ImGui::Button("Motion Player", ImVec2(100.0f, 24.0f))) {
        g_showUltimatePlayer = true;
    }

    ImGui::SameLine();
    if (ImGui::Button("Refresh", ImVec2(60.0f, 24.0f))) {
        RefreshFiles();
    }
}

void DrawMotionControlWindow() {
    if (!g_showMotionControlWindow) return;

    ImGui::SetNextWindowBgAlpha(0.85f);
    std::string title = "Motion Control" + std::string(ImGui::g_imguiHasFocus ? "" : " [GAME HAS FOCUS]") + "###MotionControlWindow";

    ImVec2 parentPos;
    ImVec2 parentSize;

    if (ImGui::Begin(title.c_str(), &g_showMotionControlWindow, ImGuiWindowFlags_AlwaysAutoResize)) {
        parentPos = ImGui::GetWindowPos();
        parentSize = ImGui::GetWindowSize();
        ImGui::GetWindowDrawList()->PushClipRectFullScreen();

        int displayBones = g_maxBones;
        if (g_maxBones > 0 && curmikupos >= g_maxBones) {
            curmikupos = g_maxBones - 1;
        }

        ImGui::PushItemWidth(140.0f);
        ImGui::InputInt("Total Bones", &displayBones, 0, 0, ImGuiInputTextFlags_ReadOnly);
        ImGui::InputInt("Current Bone", &curmikupos);
        ImGui::PopItemWidth();

        ImGui::Checkbox("Override Params", &mikuoverride);
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - 76.0f);
        if (ImGui::Button(s_showBoneHelp ? "Help [X]" : "Help (?)", ImVec2(76.0f, 0.0f))) {
            s_showBoneHelp = !s_showBoneHelp;
        }

        ImGui::Checkbox("Show all params", &mikushowall);

        if (mikuposptrcounter >= 0) {
            ImGui::PushItemWidth(68.0f);
            ImGui::InputInt("a3", &mikupos_a3[curmikupos]); ImGui::SameLine();
            ImGui::InputInt("a4", &mikupos_a4[curmikupos]); ImGui::SameLine();
            ImGui::InputInt("a5", &mikupos_a5[curmikupos]);
            ImGui::PopItemWidth();
        }

        if (mikuposptrcounter >= 0 && curmikupos < 9999) {
            float* m1 = mikupos_a1[curmikupos];
            float* m2 = mikupos_a2[curmikupos];
            int start = mikushowall ? 0 : 15;

            for (int i = start; i <= 23; i++) {
                ImGui::PushItemWidth(125.0f);

                if (m1) {
                    char lbl1[32]; snprintf(lbl1, 32, "##a1_%d", i);
                    ImGui::Text("%2d", i);
                    ImGui::SameLine(0.0f, 4.0f);
                    ImGui::DragFloat(lbl1, &m1[i], 0.01f);
                }

                if (m2) {
                    ImGui::SameLine(164.0f);
                    char lbl2[32]; snprintf(lbl2, 32, "##a2_%d", i);
                    ImGui::Text("-%d", i);
                    ImGui::SameLine(0.0f, 4.0f);
                    ImGui::DragFloat(lbl2, &m2[i], 0.01f);
                }

                ImGui::PopItemWidth();
            }
        }

        ImGui::GetWindowDrawList()->PopClipRect();
    }
    ImGui::End();

    if (s_showBoneHelp) {
        DrawBonesGuideOverlay(parentPos, parentSize);
    }
}

void DrawUltimatePlayerWindow() {
    if (!g_showUltimatePlayer) return;

    ImGui::SetNextWindowBgAlpha(0.85f);
    std::string title = "Ultimate Motion Player" + std::string(ImGui::g_imguiHasFocus ? "" : " [GAME HAS FOCUS]") + "###UltimateMotionPlayerWindow";

    if (ImGui::Begin(title.c_str(), &g_showUltimatePlayer, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::GetWindowDrawList()->PushClipRectFullScreen();

        ImGui::Checkbox("Enable Playback", &g_ultimateEnable);
        ImGui::SameLine(170.0f);
        if (ImGui::Button("+ Add Slot")) {
            g_slots.push_back(DynamicSlot());
        }
        ImGui::Separator();

        for (size_t i = 0; i < g_slots.size(); i++) {
            auto& slot = g_slots[i];
            ImGui::PushID((int)i);

            ImGui::Checkbox("##en", &slot.enabled);
            ImGui::SameLine();

            ImGui::PushItemWidth(180.0f);
            if (g_allFiles.empty()) {
                ImGui::BeginDisabled();
                if (ImGui::BeginCombo("##file", "No dumps")) ImGui::EndCombo();
                ImGui::EndDisabled();
            } else {
                const char* preview = slot.selectedIndex >= 0 ? g_allFiles[slot.selectedIndex].fileName.c_str() : "Select File...";

                if (ImGui::BeginCombo("##file", preview)) {
                    ImGui::GetWindowDrawList()->PushClipRectFullScreen();
                    for (size_t n = 0; n < g_allFiles.size(); n++) {
                        bool is_selected = (slot.selectedIndex == (int)n);
                        if (ImGui::Selectable(g_allFiles[n].fileName.c_str(), is_selected)) {
                            slot.selectedIndex = (int)n;
                            LoadDumpToSlot(slot);
                        }
                        if (is_selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::GetWindowDrawList()->PopClipRect();
                    ImGui::EndCombo();
                }
            }
            ImGui::PopItemWidth();

            ImGui::SameLine();

            ImGui::PushItemWidth(150.0f);
            float maxSliderFrame = (slot.dump.hasData && slot.dump.maxFrame > 0) ? (float)slot.dump.maxFrame : 100.0f;
            ImGui::SliderFloat("Frame##frm", &slot.currentFrame, 0.0f, maxSliderFrame, "%.1f");
            ImGui::PopItemWidth();

            ImGui::SameLine();
            if (ImGui::Button("X")) {
                g_slots.erase(g_slots.begin() + i);
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

} // namespace MotionPlayerUi
