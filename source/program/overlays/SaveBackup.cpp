#include "SaveBackup.hpp"
#include "imgui/imgui.h"
#include "lib.hpp"

#include <nn/fs.hpp>
#include <nn/os.hpp>
#include <string>
#include <vector>
#include <cstdio>
#include <algorithm>

#ifndef R_SUCCEEDED
#define R_SUCCEEDED(res) ((res).IsSuccess())
#endif
#ifndef R_FAILED
#define R_FAILED(res) ((res).IsFailure())
#endif

namespace SaveBackup {

static const char* kBackupBaseDir  = "ExlSD:/MikuMikuSwitchPlugin/Backups";
static const char* kSaveDirPrimary = "ExlSD:/Save";
static const char* kSaveDirPlugin  = "ExlSD:/MikuMikuSwitchPlugin/Save";

static std::vector<std::string> s_backupList;
static int  s_selectedBackupIdx = -1;
static int  s_currentPage       = 0;

static bool s_confirmCreateBackup  = false;
static bool s_confirmDeleteBackup  = false;
static bool s_confirmRestoreBackup = false;

static std::string s_statusMessage = "";
static float s_statusTimer = 0.0f;

// =========================================================
// SWITCH RTC CLOCK FOR BACKUP FOLDER NAMING
// =========================================================
struct SwitchCalendarTime {
    int16_t year;   // +0x00
    int8_t  month;  // +0x02 (1..12)
    int8_t  day;    // +0x03 (1..31)
    int8_t  hour;   // +0x04 (0..23)
    int8_t  minute; // +0x05 (0..59)
    int8_t  second; // +0x06 (0..59)
};

using TimeInit_t       = uint32_t (*)();
using TimeIsInit_t     = bool (*)();
using GetCurrentTime_t = uint32_t (*)(int64_t* outPosixTime);
using ToCalendarTime_t = uint32_t (*)(SwitchCalendarTime* outCal, void* outInfo, const int64_t* inPosixTime);

static TimeInit_t       s_TimeInit       = nullptr;
static TimeIsInit_t     s_TimeIsInit     = nullptr;
static GetCurrentTime_t s_GetCurrentTime = nullptr;
static ToCalendarTime_t s_ToCalendarTime = nullptr;

static uintptr_t FindSdkSymbol(const char* sym) {
    for (int i = static_cast<int>(exl::util::ModuleIndex::Start); i < static_cast<int>(exl::util::ModuleIndex::End); i++) {
        auto index = static_cast<exl::util::ModuleIndex>(i);
        if (exl::util::HasModule(index)) {
            Elf_Sym* symbol = exl::reloc::GetSymbol(index, sym);
            if (symbol != nullptr) {
                return exl::util::GetModuleInfo(index).m_Total.m_Start + symbol->st_value;
            }
        }
    }
    return 0;
}

static void InitSystemClockService() {
    uintptr_t fnInit    = FindSdkSymbol("_ZN2nn4time10InitializeEv");
    uintptr_t fnIsInit  = FindSdkSymbol("_ZN2nn4time13IsInitializedEv");
    uintptr_t fnGet     = FindSdkSymbol("_ZN2nn4time23StandardUserSystemClock14GetCurrentTimeEPNS0_9PosixTimeE");
    uintptr_t fnCal     = FindSdkSymbol("_ZN2nn4time14ToCalendarTimeEPNS0_12CalendarTimeEPNS0_22CalendarAdditionalInfoERKNS0_9PosixTimeE");

    if (fnInit)   s_TimeInit       = reinterpret_cast<TimeInit_t>(fnInit);
    if (fnIsInit) s_TimeIsInit     = reinterpret_cast<TimeIsInit_t>(fnIsInit);
    if (fnGet)    s_GetCurrentTime = reinterpret_cast<GetCurrentTime_t>(fnGet);
    if (fnCal)    s_ToCalendarTime = reinterpret_cast<ToCalendarTime_t>(fnCal);

    if (s_TimeIsInit && !s_TimeIsInit()) {
        if (s_TimeInit) s_TimeInit();
    } else if (s_TimeInit && !s_TimeIsInit) {
        s_TimeInit();
    }
}

static std::string GenerateBackupFolderName() {
    char buf[64] = {0};

    if (s_GetCurrentTime && s_ToCalendarTime) {
        int64_t posixTime = 0;
        if (s_GetCurrentTime(&posixTime) == 0) {
            SwitchCalendarTime cal = {};
            char dummyInfo[0x40] = {0};
            if (s_ToCalendarTime(&cal, dummyInfo, &posixTime) == 0 && cal.year >= 2020) {
                std::snprintf(buf, sizeof(buf), "Backup_%04d-%02d-%02d_%02d-%02d-%02d",
                              cal.year, cal.month, cal.day,
                              cal.hour, cal.minute, cal.second);
                return std::string(buf);
            }
        }
    }

    std::snprintf(buf, sizeof(buf), "Backup_%03d", (int)s_backupList.size() + 1);
    return std::string(buf);
}

// =========================================================
// FILE SYSTEM
// =========================================================
static void EnsureDirectory(const char* path) {
    nn::fs::DirectoryHandle dir;
    if (R_SUCCEEDED(nn::fs::OpenDirectory(&dir, path, 1))) {
        nn::fs::CloseDirectory(dir);
    } else {
        nn::fs::CreateDirectory(path);
    }
}

static void EnsureBaseDirectories() {
    EnsureDirectory("ExlSD:/MikuMikuSwitchPlugin");
    EnsureDirectory(kBackupBaseDir);
}

static std::string ResolveSaveDir() {
    nn::fs::DirectoryHandle dir;
    if (R_SUCCEEDED(nn::fs::OpenDirectory(&dir, kSaveDirPrimary, 1))) {
        nn::fs::CloseDirectory(dir);
        return kSaveDirPrimary;
    }
    if (R_SUCCEEDED(nn::fs::OpenDirectory(&dir, kSaveDirPlugin, 1))) {
        nn::fs::CloseDirectory(dir);
        return kSaveDirPlugin;
    }
    return kSaveDirPrimary;
}

static bool CopySingleFile(const std::string& srcPath, const std::string& dstPath) {
    nn::fs::FileHandle hSrc;
    if (R_FAILED(nn::fs::OpenFile(&hSrc, srcPath.c_str(), nn::fs::OpenMode_Read))) {
        return false;
    }

    int64_t fileSize = 0;
    nn::fs::GetFileSize(&fileSize, hSrc);

    nn::fs::DeleteFile(dstPath.c_str());
    nn::fs::CreateFile(dstPath.c_str(), fileSize);

    nn::fs::FileHandle hDst;
    if (R_FAILED(nn::fs::OpenFile(&hDst, dstPath.c_str(), nn::fs::OpenMode_Write))) {
        nn::fs::CloseFile(hSrc);
        return false;
    }

    constexpr int64_t kChunkSize = 64 * 1024;
    std::vector<char> chunk(kChunkSize);
    int64_t offset = 0;

    while (offset < fileSize) {
        int64_t toRead = std::min<int64_t>(kChunkSize, fileSize - offset);
        nn::fs::ReadFile(hSrc, offset, chunk.data(), toRead);
        nn::fs::WriteFile(hDst, offset, chunk.data(), toRead,
                          nn::fs::WriteOption::CreateOption(nn::fs::WriteOptionFlag_Flush));
        offset += toRead;
    }

    nn::fs::CloseFile(hDst);
    nn::fs::CloseFile(hSrc);
    return true;
}

static bool CopyDirectoryTree(const std::string& srcDir, const std::string& dstDir) {
    EnsureDirectory(dstDir.c_str());

    std::vector<std::string> files;
    {
        nn::fs::DirectoryHandle dir;
        int64_t readCount = 0;
        nn::fs::DirectoryEntry entry;
        if (R_SUCCEEDED(nn::fs::OpenDirectory(&dir, srcDir.c_str(), 2))) {
            while (R_SUCCEEDED(nn::fs::ReadDirectory(&readCount, &entry, dir, 1)) && readCount > 0) {
                files.push_back(entry.m_Name);
            }
            nn::fs::CloseDirectory(dir);
        }
    }

    for (const auto& fname : files) {
        std::string srcFile = srcDir + "/" + fname;
        std::string dstFile = dstDir + "/" + fname;
        CopySingleFile(srcFile, dstFile);
    }

    std::vector<std::string> subDirs;
    {
        nn::fs::DirectoryHandle dir;
        int64_t readCount = 0;
        nn::fs::DirectoryEntry entry;
        if (R_SUCCEEDED(nn::fs::OpenDirectory(&dir, srcDir.c_str(), 1))) {
            while (R_SUCCEEDED(nn::fs::ReadDirectory(&readCount, &entry, dir, 1)) && readCount > 0) {
                std::string dname = entry.m_Name;
                if (dname != "." && dname != "..") {
                    subDirs.push_back(dname);
                }
            }
            nn::fs::CloseDirectory(dir);
        }
    }

    for (const auto& dname : subDirs) {
        CopyDirectoryTree(srcDir + "/" + dname, dstDir + "/" + dname);
    }

    return true;
}

static void DeleteDirectoryTree(const std::string& path) {
    nn::fs::DeleteDirectoryRecursively(path.c_str());
}

static void RefreshBackupList() {
    EnsureBaseDirectories();
    s_backupList.clear();
    s_selectedBackupIdx = -1;
    s_confirmDeleteBackup = false;
    s_confirmRestoreBackup = false;

    nn::fs::DirectoryHandle dir;
    int64_t readCount = 0;
    nn::fs::DirectoryEntry entry;

    if (R_SUCCEEDED(nn::fs::OpenDirectory(&dir, kBackupBaseDir, 1))) {
        while (R_SUCCEEDED(nn::fs::ReadDirectory(&readCount, &entry, dir, 1)) && readCount > 0) {
            std::string dname = entry.m_Name;
            if (dname != "." && dname != "..") {
                s_backupList.push_back(dname);
            }
        }
        nn::fs::CloseDirectory(dir);
    }
    std::sort(s_backupList.rbegin(), s_backupList.rend());
}

static void DoCreateBackup() {
    std::string saveDir = ResolveSaveDir();

    nn::fs::DirectoryHandle checkDir;
    if (R_FAILED(nn::fs::OpenDirectory(&checkDir, saveDir.c_str(), 1))) {
        s_statusMessage = "Save folder not found!";
        s_statusTimer = 3.0f;
        return;
    }
    nn::fs::CloseDirectory(checkDir);

    EnsureBaseDirectories();

    std::string folderName = GenerateBackupFolderName();
    std::string dstPath = std::string(kBackupBaseDir) + "/" + folderName;

    if (CopyDirectoryTree(saveDir, dstPath)) {
        s_statusMessage = "Created: " + folderName;
        RefreshBackupList();
    } else {
        s_statusMessage = "Backup Failed!";
    }
    s_statusTimer = 3.0f;
}

static void DoDeleteSelected() {
    if (s_selectedBackupIdx < 0 || s_selectedBackupIdx >= (int)s_backupList.size())
        return;

    std::string target = std::string(kBackupBaseDir) + "/" + s_backupList[s_selectedBackupIdx];
    DeleteDirectoryTree(target);

    s_statusMessage = "Deleted!";
    s_statusTimer = 3.0f;
    RefreshBackupList();
}

static void DoRestoreSelected() {
    if (s_selectedBackupIdx < 0 || s_selectedBackupIdx >= (int)s_backupList.size())
        return;

    std::string srcBackup = std::string(kBackupBaseDir) + "/" + s_backupList[s_selectedBackupIdx];
    std::string saveDir = ResolveSaveDir();

    if (CopyDirectoryTree(srcBackup, saveDir)) {
        s_statusMessage = "Restored!";
    } else {
        s_statusMessage = "Restore Failed!";
    }
    s_statusTimer = 3.0f;
}

void Init() {
    InitSystemClockService();
    RefreshBackupList();
}

void Draw() {
    if (ImGui::CollapsingHeader("Savedata Backups")) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRectFullScreen();

        ImGui::Separator();

        const float blockW = 270.0f;
        const float startX = ImGui::GetCursorPosX();

        float dt = ImGui::GetIO().DeltaTime;
        if (s_statusTimer > 0.0f) {
            s_statusTimer -= dt;
            ImGui::TextColored(ImVec4(0.20f, 0.90f, 0.60f, 1.0f), "Status: %s", s_statusMessage.c_str());
        }

        if (!s_confirmCreateBackup) {
            if (ImGui::Button("Backup Save/Folder", ImVec2(blockW, 26.0f))) {
                s_confirmCreateBackup = true;
            }
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.45f, 0.10f, 0.95f));
            if (ImGui::Button("Confirm Backup?", ImVec2(195.0f, 26.0f))) {
                DoCreateBackup();
                s_confirmCreateBackup = false;
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(68.0f, 26.0f))) {
                s_confirmCreateBackup = false;
            }
        }

        ImGui::Separator();

        int total_items = (int)s_backupList.size();

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Dumps (%d)", total_items);

        ImGui::SameLine();
        ImGui::SetCursorPosX(startX + blockW - 68.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.5f));
        if (ImGui::Button("Refresh", ImVec2(68.0f, 24.0f))) {
            RefreshBackupList();
        }
        ImGui::PopStyleVar();

        if (total_items > 0) {
            constexpr int items_per_page = 8;
            int total_pages = (total_items + items_per_page - 1) / items_per_page;
            s_currentPage = std::clamp(s_currentPage, 0, total_pages - 1);

            int startIdx = s_currentPage * items_per_page;
            int endIdx   = std::min(startIdx + items_per_page, total_items);

            const float btnW = (blockW - 12.0f) * 0.25f;
            const float btnH = 26.0f;

            for (int i = startIdx; i < endIdx; i++) {
                if ((i - startIdx) % 4 != 0) ImGui::SameLine();

                // Extract date (DD.MM) from "Backup_YYYY-MM-DD..."
                char labelBuf[32];
                const std::string& fullName = s_backupList[i];
                if (fullName.length() >= 17 && fullName.rfind("Backup_", 0) == 0) {
                    std::string month = fullName.substr(12, 2);
                    std::string day   = fullName.substr(15, 2);
                    std::snprintf(labelBuf, sizeof(labelBuf), "%s.%s##bk%d", day.c_str(), month.c_str(), i);
                } else {
                    std::snprintf(labelBuf, sizeof(labelBuf), "#%02d##bk%d", i + 1, i);
                }

                bool isSel = (s_selectedBackupIdx == i);
                if (isSel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.65f, 0.55f, 0.95f));

                if (ImGui::Button(labelBuf, ImVec2(btnW, btnH))) {
                    s_selectedBackupIdx = i;
                    s_confirmDeleteBackup = false;
                    s_confirmRestoreBackup = false;
                }

                if (isSel) ImGui::PopStyleColor();
            }

            ImGui::Spacing();

            float navRowStartX = ImGui::GetCursorPosX();

            if (ImGui::Button("<##bkprev", ImVec2(34.0f, 24.0f))) {
                if (s_currentPage > 0) s_currentPage--;
            }

            char pageStr[32];
            std::snprintf(pageStr, sizeof(pageStr), "%d / %d", s_currentPage + 1, total_pages);
            ImVec2 pageSz = ImGui::CalcTextSize(pageStr);

            ImGui::SameLine();
            ImGui::SetCursorPosX(navRowStartX + (blockW - pageSz.x) * 0.5f);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(pageStr);

            ImGui::SameLine();
            ImGui::SetCursorPosX(navRowStartX + blockW - 34.0f);
            if (ImGui::Button(">##bknext", ImVec2(34.0f, 24.0f))) {
                if (s_currentPage < total_pages - 1) s_currentPage++;
            }

            if (s_selectedBackupIdx >= 0 && s_selectedBackupIdx < total_items) {
                ImGui::TextColored(ImVec4(0.30f, 0.85f, 0.80f, 1.0f), "%s", s_backupList[s_selectedBackupIdx].c_str());

                const float actionBtnW = (blockW - 6.0f) * 0.5f;
                const float actionBtnH = 28.0f;

                // Restore
                if (!s_confirmRestoreBackup) {
                    if (ImGui::Button("Restore", ImVec2(actionBtnW, actionBtnH))) {
                        s_confirmRestoreBackup = true;
                        s_confirmDeleteBackup = false;
                    }
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.20f, 0.20f, 0.95f));
                    if (ImGui::Button("Confirm?!", ImVec2(actionBtnW, actionBtnH))) {
                        DoRestoreSelected();
                        s_confirmRestoreBackup = false;
                    }
                    ImGui::PopStyleColor();
                }

                ImGui::SameLine();

                // Delete
                if (!s_confirmDeleteBackup) {
                    if (ImGui::Button("Delete", ImVec2(actionBtnW, actionBtnH))) {
                        s_confirmDeleteBackup = true;
                        s_confirmRestoreBackup = false;
                    }
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.95f, 0.15f, 0.20f, 1.0f));
                    if (ImGui::Button("DELETE?!", ImVec2(actionBtnW, actionBtnH))) {
                        DoDeleteSelected();
                        s_confirmDeleteBackup = false;
                    }
                    ImGui::PopStyleColor();
                }
            }
        }

        drawList->PopClipRect();
    }
}

} // namespace SaveBackup
