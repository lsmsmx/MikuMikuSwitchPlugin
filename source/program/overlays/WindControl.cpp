#include "WindControl.hpp"
#include "VirtualKeyboard.hpp"
#include "../macros.hpp"
#include "imgui/imgui.h"
#include "lib.hpp"

#include <nn/fs.hpp>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>

#define ADDR_WIND_CTRL 0x00523140

namespace ImGui {
    extern bool g_imguiHasFocus;
}

namespace WindControl {

bool g_showWindow = false;
bool g_overrideWind = false;

static Wind* s_currentEngineWind = nullptr;

static int s_controlMode = 0;
static bool s_showHelp = false;

static float s_scale = 0.08f;
static float s_cycle = 0.01f;
static float s_yrot = 90.0f;
static float s_zrot = 80.0f;
static float s_bias = 0.39f;
static float s_strength = 1.0f;

static WindSpectrum s_spectrum[16] = {};
static float s_customVec[3] = { 0.0f, 0.0f, 0.0f };

static char s_fileNameBuf[64] = "wind_tst.txt";
static std::string s_lastDumpMsg = "";
static std::vector<std::string> s_savedFiles;
static int s_fileCurrentPage = 0;

static void EnsureDirectory() {
    nn::fs::DirectoryHandle dir;
    if (R_SUCCEEDED(nn::fs::OpenDirectory(&dir, "ExlSD:/MikuMikuSwitchPlugin/Wind", 1))) {
        nn::fs::CloseDirectory(dir);
    } else {
        nn::fs::CreateDirectory("ExlSD:/MikuMikuSwitchPlugin/Wind");
    }
}

static void RefreshFiles() {
    EnsureDirectory();
    s_savedFiles.clear();

    nn::fs::DirectoryHandle dir;
    int64_t readCount;
    nn::fs::DirectoryEntry entry;

    if (R_SUCCEEDED(nn::fs::OpenDirectory(&dir, "ExlSD:/MikuMikuSwitchPlugin/Wind", 2))) {
        while (R_SUCCEEDED(nn::fs::ReadDirectory(&readCount, &entry, dir, 1)) && readCount > 0) {
            std::string fname = entry.m_Name;
            if (fname.find(".txt") != std::string::npos) {
                s_savedFiles.push_back(fname);
            }
        }
        nn::fs::CloseDirectory(dir);
    }
}

static std::string GenerateDumpFilename() {
    EnsureDirectory();
    for (int i = 1; i <= 999; i++) {
        char fname[64];
        snprintf(fname, sizeof(fname), "wind_dump_%d.txt", i);

        char full[128];
        snprintf(full, sizeof(full), "ExlSD:/MikuMikuSwitchPlugin/Wind/%s", fname);

        nn::fs::FileHandle h;
        if (R_FAILED(nn::fs::OpenFile(&h, full, nn::fs::OpenMode_Read))) {
            return std::string(fname);
        }
        nn::fs::CloseFile(h);
    }
    return "wind_dump_999.txt";
}

bool LoadFromSegaTxt(const char* full_path) {
    nn::fs::FileHandle h;
    if (R_FAILED(nn::fs::OpenFile(&h, full_path, nn::fs::OpenMode_Read))) {
        return false;
    }

    int64_t sz = 0;
    nn::fs::GetFileSize(&sz, h);
    std::vector<char> buf(sz + 1);
    nn::fs::ReadFile(h, 0, buf.data(), sz);
    nn::fs::CloseFile(h);
    buf[sz] = '\0';

    std::memset(s_spectrum, 0, sizeof(s_spectrum));

    char* line = strtok(buf.data(), "\r\n");
    while (line != nullptr) {
        if (strncmp(line, "scale", 5) == 0) {
            sscanf(line + 5, "%f", &s_scale);
        } else if (strncmp(line, "cycle", 5) == 0) {
            sscanf(line + 5, "%f", &s_cycle);
        } else if (strncmp(line, "rot", 3) == 0) {
            sscanf(line + 3, "%f %f", &s_yrot, &s_zrot);
        } else if (strncmp(line, "bias", 4) == 0) {
            sscanf(line + 4, "%f", &s_bias);
        } else if (strncmp(line, "spc", 3) == 0) {
            int idx = 0;
            float cos_v = 0.0f, sin_v = 0.0f;
            int count = sscanf(line + 3, "%d %f %f", &idx, &cos_v, &sin_v);
            if (count >= 2 && idx >= 0 && idx < 16) {
                s_spectrum[idx].cos = cos_v;
                s_spectrum[idx].sin = (count == 3) ? sin_v : 0.0f;
            }
        }
        line = strtok(nullptr, "\r\n");
    }
    return true;
}

static bool WriteWindDataToTxt(const char* full_path, float scale, float cycle, float yrot, float zrot, float bias, const WindSpectrum* spec) {
    EnsureDirectory();
    std::string out = "";
    char line[128];

    snprintf(line, sizeof(line), "scale %g\n", scale); out += line;
    snprintf(line, sizeof(line), "cycle %g\n", cycle); out += line;
    snprintf(line, sizeof(line), "rot %g %g\n", yrot, zrot); out += line;
    snprintf(line, sizeof(line), "bias %g\n", bias); out += line;

    for (int i = 0; i < 16; i++) {
        float c = spec ? spec[i].cos : 0.0f;
        float s = spec ? spec[i].sin : 0.0f;
        snprintf(line, sizeof(line), "spc %d %g %g\n", i, c, s);
        out += line;
    }

    nn::fs::DeleteFile(full_path);
    nn::fs::CreateFile(full_path, out.length());

    nn::fs::FileHandle h;
    if (R_SUCCEEDED(nn::fs::OpenFile(&h, full_path, nn::fs::OpenMode_Write))) {
        nn::fs::WriteFile(h, 0, out.c_str(), out.length(), nn::fs::WriteOption::CreateOption(nn::fs::WriteOptionFlag_Flush));
        nn::fs::CloseFile(h);
        return true;
    }
    return false;
}

static bool SaveCurrentWindFile(const char* custom_name) {
    char full[128];
    if (custom_name && custom_name[0] != '\0') {
        std::string fname = custom_name;
        if (fname.find(".txt") == std::string::npos) fname += ".txt";
        snprintf(full, sizeof(full), "ExlSD:/MikuMikuSwitchPlugin/Wind/%s", fname.c_str());
    } else {
        std::string fname = GenerateDumpFilename();
        snprintf(full, sizeof(full), "ExlSD:/MikuMikuSwitchPlugin/Wind/%s", fname.c_str());
        strncpy(s_fileNameBuf, fname.c_str(), sizeof(s_fileNameBuf));
    }

    bool ok = WriteWindDataToTxt(full, s_scale, s_cycle, s_yrot, s_zrot, s_bias, s_spectrum);
    if (ok) {
        s_lastDumpMsg = "Saved!";
        RefreshFiles();
    }
    return ok;
}

HOOK_DEFINE_TRAMPOLINE(WindCtrlHook) {
    static void Callback(Wind* wind) {
        s_currentEngineWind = wind;

        if (!g_overrideWind) {
            Orig(wind);
            s_scale = wind->scale;
            s_cycle = wind->cycle;
            s_yrot = wind->yrot;
            s_zrot = wind->zrot;
            s_bias = wind->bias;
            s_strength = wind->strength;
            s_customVec[0] = wind->val[0];
            s_customVec[1] = wind->val[1];
            s_customVec[2] = wind->val[2];
            for (int i = 0; i < 16; i++) {
                s_spectrum[i] = wind->spectrum[i];
            }
        } else {
            if (s_controlMode == 0) {
                wind->scale = s_scale;
                wind->cycle = s_cycle;
                wind->yrot = s_yrot;
                wind->zrot = s_zrot;
                wind->bias = s_bias;
                for (int i = 0; i < 16; i++) {
                    wind->spectrum[i] = s_spectrum[i];
                }
                Orig(wind);
                wind->strength = s_strength;
            } else {
                wind->val[0] = s_customVec[0];
                wind->val[1] = s_customVec[1];
                wind->val[2] = s_customVec[2];
                wind->strength = s_strength;
            }
        }
    }
};

void Init() {
    RefreshFiles();
    s_spectrum[2].cos = 0.03f;
    s_spectrum[3].cos = 0.09f;
    WindCtrlHook::InstallAtOffset(ADDR_WIND_CTRL);
}

void DrawWindow() {
    if (!g_showWindow) return;

    ImGui::SetNextWindowBgAlpha(0.85f);
    std::string title = "Wind Control" + std::string(ImGui::g_imguiHasFocus ? "" : " [GAME HAS FOCUS]") + "###WindControlWindow";

    ImGui::SetNextWindowSizeConstraints(ImVec2(290.0f, -1.0f), ImVec2(310.0f, -1.0f));

    if (ImGui::Begin(title.c_str(), &g_showWindow, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::GetWindowDrawList()->PushClipRectFullScreen();

        ImGui::Checkbox("Override", &g_overrideWind);
        ImGui::SameLine(90.0f);
        ImGui::PushItemWidth(120.0f);

        if (VirtualKeyboard::ShouldFocusTarget()) {
            ImGui::SetKeyboardFocusHere(0);
        }

        ImGui::InputText(
            "##wname", s_fileNameBuf, sizeof(s_fileNameBuf),
            ImGuiInputTextFlags_CallbackAlways, VirtualKeyboard::InputTextCallback
        );
        if (ImGui::IsItemClicked()) {
            VirtualKeyboard::Open(s_fileNameBuf, sizeof(s_fileNameBuf), "SAVE CONFIG", []() {
                SaveCurrentWindFile(s_fileNameBuf);
            });
        }
        ImGui::PopItemWidth();

        ImGui::SameLine();
        if (ImGui::Button("Save", ImVec2(55.0f, 0.0f))) {
            SaveCurrentWindFile(s_fileNameBuf);
        }

        if (!s_lastDumpMsg.empty()) {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "%s", s_lastDumpMsg.c_str());
        }

        ImGui::Separator();

        ImGui::Text("Presets:");
        if (ImGui::Button("Upward (Lift)", ImVec2(135.0f, 0.0f))) {
            g_overrideWind = true; s_controlMode = 0;
            s_scale = 0.08f; s_cycle = 0.01f; s_yrot = 90.0f; s_zrot = 80.0f; s_bias = 0.39f; s_strength = 1.0f;
            std::memset(s_spectrum, 0, sizeof(s_spectrum));
            s_spectrum[2].cos = 0.03f; s_spectrum[3].cos = 0.09f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Faint Lift", ImVec2(135.0f, 0.0f))) {
            g_overrideWind = true; s_controlMode = 0;
            s_scale = 0.1f; s_cycle = 0.01f; s_yrot = 30.0f; s_zrot = 90.0f; s_bias = 0.1f; s_strength = 1.0f;
            std::memset(s_spectrum, 0, sizeof(s_spectrum));
            s_spectrum[2].cos = 0.05f; s_spectrum[3].cos = 0.1f; s_spectrum[9].cos = 0.05f;
        }

        if (ImGui::Button("Backward", ImVec2(135.0f, 0.0f))) {
            g_overrideWind = true; s_controlMode = 0;
            s_scale = 0.3f; s_cycle = 0.02f; s_yrot = 90.0f; s_zrot = 0.0f; s_bias = 0.5f; s_strength = 1.0f;
            std::memset(s_spectrum, 0, sizeof(s_spectrum));
            s_spectrum[2].cos = 0.04f; s_spectrum[3].cos = 0.10f;
            s_spectrum[9].sin = 0.02f; s_spectrum[10].sin = 0.08f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Right", ImVec2(135.0f, 0.0f))) {
            g_overrideWind = true; s_controlMode = 0;
            s_scale = 0.3f; s_cycle = 0.02f; s_yrot = 0.0f; s_zrot = 0.0f; s_bias = 0.5f; s_strength = 1.0f;
            std::memset(s_spectrum, 0, sizeof(s_spectrum));
            s_spectrum[2].cos = 0.04f; s_spectrum[3].cos = 0.10f;
            s_spectrum[9].sin = 0.02f; s_spectrum[10].sin = 0.08f;
        }

        if (ImGui::Button("Zero-G Float", ImVec2(135.0f, 0.0f))) {
            g_overrideWind = true; s_controlMode = 0;
            s_scale = 0.04f; s_cycle = 0.005f; s_yrot = 0.0f; s_zrot = 85.0f; s_bias = 0.15f; s_strength = 1.0f;
            std::memset(s_spectrum, 0, sizeof(s_spectrum));
            s_spectrum[1].cos = 0.02f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Headwind", ImVec2(135.0f, 0.0f))) {
            g_overrideWind = true; s_controlMode = 0;
            s_scale = 0.25f; s_cycle = 0.03f; s_yrot = 180.0f; s_zrot = 0.0f; s_bias = 0.4f; s_strength = 1.0f;
            std::memset(s_spectrum, 0, sizeof(s_spectrum));
            s_spectrum[2].cos = 0.05f;
        }

        if (ImGui::Button("Subtle Gust", ImVec2(135.0f, 0.0f))) {
            g_overrideWind = true; s_controlMode = 0;
            s_scale = 0.008f; s_cycle = 0.01f; s_yrot = 300.0f; s_zrot = 80.0f; s_bias = 0.20f; s_strength = 1.0f;
            std::memset(s_spectrum, 0, sizeof(s_spectrum));
            s_spectrum[2].cos = 0.03f; s_spectrum[3].cos = 0.09f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Zero (Off)", ImVec2(135.0f, 0.0f))) {
            g_overrideWind = true;
            s_scale = 0.0f; s_cycle = 0.0f; s_yrot = 0.0f; s_zrot = 0.0f; s_bias = 0.0f;
            std::memset(s_spectrum, 0, sizeof(s_spectrum));
            s_customVec[0] = s_customVec[1] = s_customVec[2] = 0.0f;
        }

        if (ImGui::Button(s_showHelp ? "Hide Help" : "Help (? Info)", ImVec2(275.0f, 0.0f))) {
            s_showHelp = !s_showHelp;
        }

        if (s_showHelp) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "Parameters Guide:");
            ImGui::TextWrapped("Scale: Wind force multiplier.");
            ImGui::TextWrapped("Cycle: Wave speed (lower = smoother float).");
            ImGui::TextWrapped("Rot Y: Yaw angle (-180 to 360 deg).");
            ImGui::TextWrapped("Rot Z: Pitch tilt (80-90 deg = upward lift).");
            ImGui::TextWrapped("Bias: Constant base lift (holds skirts up).");
            ImGui::TextWrapped("spc 0: Main slow rolling wave.");
            ImGui::TextWrapped("spc 1-3: Fabric ripple and natural flutter.");
            ImGui::TextWrapped("spc 4-15: Fast micro-vibrations (keep at 0).");
            ImGui::Separator();
        }

        ImGui::Separator();

        ImGui::RadioButton("Rotation + Spectrum", &s_controlMode, 0);
        ImGui::RadioButton("Direct 3D Vector", &s_controlMode, 1);

        ImGui::Spacing();

        if (s_controlMode == 0) {
            ImGui::PushItemWidth(165.0f);
            ImGui::DragFloat("Scale", &s_scale, 0.0005f, 0.0000f, 1.0000f, "%.4f");
            ImGui::DragFloat("Cycle", &s_cycle, 0.001f, 0.000f, 2.000f, "%.3f");
            ImGui::SliderFloat("Rot Y", &s_yrot, -180.0f, 360.0f, "%.0f deg");
            ImGui::SliderFloat("Rot Z", &s_zrot, -180.0f, 180.0f, "%.0f deg");
            ImGui::DragFloat("Bias", &s_bias, 0.005f, -5.0f, 5.0f, "%.3f");
            ImGui::SliderFloat("Strength", &s_strength, 0.0f, 1.0f, "%.2f");
            ImGui::PopItemWidth();

            ImGui::Spacing();

            if (ImGui::CollapsingHeader("Harmonic Spectrum (spc 0..15)")) {
                if (ImGui::Button("Clear All (0)", ImVec2(275.0f, 0.0f))) {
                    std::memset(s_spectrum, 0, sizeof(s_spectrum));
                }

                for (int i = 0; i < 16; i++) {
                    ImGui::PushID(i);
                    ImGui::Text("%2d:", i);
                    ImGui::SameLine();
                    ImGui::PushItemWidth(100.0f);
                    ImGui::DragFloat("cos", &s_spectrum[i].cos, 0.002f, -2.0f, 2.0f, "%.3f");
                    ImGui::SameLine();
                    ImGui::DragFloat("sin", &s_spectrum[i].sin, 0.002f, -2.0f, 2.0f, "%.3f");
                    ImGui::PopItemWidth();
                    ImGui::PopID();
                }
            }
        } else {
            ImGui::PushItemWidth(165.0f);
            ImGui::DragFloat("Force X", &s_customVec[0], 0.001f, -0.200f, 0.200f, "%.4f");
            ImGui::DragFloat("Force Y", &s_customVec[1], 0.001f, -0.200f, 0.200f, "%.4f");
            ImGui::DragFloat("Force Z", &s_customVec[2], 0.001f, -0.200f, 0.200f, "%.4f");
            ImGui::SliderFloat("Strength", &s_strength, 0.0f, 1.0f, "%.2f");
            ImGui::PopItemWidth();

            ImGui::Text("Quick Direction:");
            if (ImGui::Button("Up", ImVec2(65.0f, 0.0f)))    { s_customVec[0] =  0.0f;   s_customVec[1] =  0.035f; s_customVec[2] = 0.0f; }
            ImGui::SameLine();
            if (ImGui::Button("Down", ImVec2(65.0f, 0.0f)))  { s_customVec[0] =  0.0f;   s_customVec[1] = -0.035f; s_customVec[2] = 0.0f; }
            ImGui::SameLine();
            if (ImGui::Button("Left", ImVec2(65.0f, 0.0f)))  { s_customVec[0] = -0.025f; s_customVec[1] =  0.0f;   s_customVec[2] = 0.0f; }
            ImGui::SameLine();
            if (ImGui::Button("Right", ImVec2(65.0f, 0.0f))) { s_customVec[0] =  0.025f; s_customVec[1] =  0.0f;   s_customVec[2] = 0.0f; }

            if (ImGui::Button("Stop (0)", ImVec2(275.0f, 0.0f))) {
                s_customVec[0] = s_customVec[1] = s_customVec[2] = 0.0f;
            }
        }

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Custom TXT Files (SD)")) {
            if (ImGui::Button("Refresh List", ImVec2(275.0f, 0.0f))) {
                RefreshFiles();
            }

            if (!s_savedFiles.empty()) {
                constexpr int items_per_page = 4;
                int total_pages = ((int)s_savedFiles.size() + items_per_page - 1) / items_per_page;
                s_fileCurrentPage = std::clamp(s_fileCurrentPage, 0, std::max(0, total_pages - 1));

                int startIdx = s_fileCurrentPage * items_per_page;
                int endIdx   = std::min(startIdx + items_per_page, (int)s_savedFiles.size());

                for (int i = startIdx; i < endIdx; i++) {
                    if ((i - startIdx) % 2 != 0) {
                        ImGui::SameLine();
                    }
                    if (ImGui::Button(s_savedFiles[i].c_str(), ImVec2(135.0f, 24.0f))) {
                        char full[128];
                        snprintf(full, sizeof(full), "ExlSD:/MikuMikuSwitchPlugin/Wind/%s", s_savedFiles[i].c_str());
                        if (LoadFromSegaTxt(full)) {
                            g_overrideWind = true;
                            s_controlMode = 0;
                            strncpy(s_fileNameBuf, s_savedFiles[i].c_str(), sizeof(s_fileNameBuf));
                        }
                    }
                }

                if (total_pages > 1) {
                    ImGui::Spacing();
                    if (ImGui::Button("<##wprev", ImVec2(34.0f, 22.0f))) {
                        if (s_fileCurrentPage > 0) s_fileCurrentPage--;
                    }
                    ImGui::SameLine();
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 45.0f);
                    ImGui::Text("Page %d/%d", s_fileCurrentPage + 1, total_pages);
                    ImGui::SameLine(240.0f);
                    if (ImGui::Button(">##wnext", ImVec2(34.0f, 22.0f))) {
                        if (s_fileCurrentPage < total_pages - 1) s_fileCurrentPage++;
                    }
                }
            }
        }

        if (s_currentEngineWind) {
            ImGui::Separator();
            ImGui::Text("Vector: X:%.3f Y:%.3f Z:%.3f",
                s_currentEngineWind->val[0], s_currentEngineWind->val[1], s_currentEngineWind->val[2]);
        }

        ImGui::GetWindowDrawList()->PopClipRect();
    }
    ImGui::End();
}

} // namespace WindControl
