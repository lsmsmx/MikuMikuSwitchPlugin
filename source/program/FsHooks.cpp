#include "FsHooks.hpp"
#include "DebugMode.hpp"
#include "Config.hpp"
#include <cstring>
#include <cstdio>
#include <mutex>
#include <set>
#include <lib/reloc/reloc.hpp>
#include <nn/fs.hpp>

namespace FsHooks {

static char g_AmsRomfsBase[512] = {0};
static std::set<std::string> g_FolderCache;
static std::mutex g_CacheMtx;

uintptr_t FindSymbol(const char* sym) {
    for(int i = static_cast<int>(exl::util::ModuleIndex::Start); i < static_cast<int>(exl::util::ModuleIndex::End); i++) {
        auto index = static_cast<exl::util::ModuleIndex>(i);
        if(exl::util::HasModule(index)) {
            Elf_Sym* symbol = exl::reloc::GetSymbol(index, sym);
            if (symbol != nullptr) return exl::util::GetModuleInfo(index).m_Total.m_Start + symbol->st_value;
        }
    }
    return 0;
}

void InitAmsPath() {
    if (g_AmsRomfsBase[0] != '\0') return;
    const char* tids[] = { "0100F3100DA46000", "01001CC00FA1A000", "0100BE300FF62000" };
    nn::fs::DirectoryHandle dh;
    for (const char* tid : tids) {
        char test[384];
        snprintf(test, sizeof(test), "ExlSD:/atmosphere/contents/%s/romfs", tid);
        if (R_SUCCEEDED(nn::fs::OpenDirectory(&dh, test, nn::fs::OpenDirectoryMode_Directory))) {
            nn::fs::CloseDirectory(dh);
            snprintf(g_AmsRomfsBase, sizeof(g_AmsRomfsBase), "%s/", test);
            return;
        }
    }
    snprintf(g_AmsRomfsBase, sizeof(g_AmsRomfsBase), "ExlSD:/atmosphere/contents/01001CC00FA1A000/romfs/");
}

void SafeCreateDirs(const char* fullPath) {
    char temp[768];
    strncpy(temp, fullPath, sizeof(temp));
    temp[767] = '\0';
    char* lastSlash = strrchr(temp, '/');
    if (!lastSlash) return;
    *lastSlash = '\0';
    {
        std::lock_guard<std::mutex> lock(g_CacheMtx);
        if (g_FolderCache.count(temp)) return;
        g_FolderCache.insert(temp);
    }
    char* p = temp;
    if (strncmp(p, "ExlSD:/", 7) == 0) p += 7;
    while (*p) {
        if (*p == '/') {
            *p = '\0';
            nn::fs::CreateDirectory(temp);
            *p = '/';
        }
        p++;
    }
    nn::fs::CreateDirectory(temp);
}

// Fastest path redirection
bool RedirectDebugPath(const char* origPath, char* outPath, size_t outSize, bool isWriteMode = false) {
    if (!origPath || origPath[0] == '\0') return false;
    if (strncmp(origPath, "ExlSD:/", 7) == 0) return false;

    bool isNativeSave = false;
    const char* suffix = origPath;

    if (strncmp(origPath, "savedata:/", 10) == 0) { suffix = origPath + 10; isNativeSave = true; }
    else if (strncmp(origPath, "save:/", 6) == 0) { suffix = origPath + 6; isNativeSave = true; }
    else if (strncmp(origPath, "rom:/", 5) == 0)  { suffix = origPath + 5; }
    else if (strncmp(origPath, "host:/", 6) == 0) { suffix = origPath + 6; }

    while (*suffix == '/' || *suffix == '\\' || *suffix == '.') {
        suffix++;
    }

    bool isDebugDir = (strncmp(suffix, "ram", 3) == 0 ||
                       strncmp(suffix, "dev_ram", 7) == 0 ||
                       strncmp(suffix, "osage", 5) == 0 ||
                       (isWriteMode && strstr(suffix, "light_") != nullptr));

    if (isNativeSave && !isDebugDir) return false;
    if (!isNativeSave && !isDebugDir && !isWriteMode) return false;

    InitAmsPath();
    char rawJoined[512];
    snprintf(rawJoined, sizeof(rawJoined), "%s%s", g_AmsRomfsBase, suffix);

    size_t j = 0;
    bool lastWasSlash = false;
    for (size_t i = 0; rawJoined[i] != '\0' && j < outSize - 1; i++) {
        char c = (rawJoined[i] == '\\') ? '/' : rawJoined[i];
        if (c == '/') {
            if (!lastWasSlash) { outPath[j++] = c; lastWasSlash = true; }
        } else {
            outPath[j++] = c; lastWasSlash = false;
        }
    }
    outPath[j] = '\0';
    if (j > 0 && outPath[j - 1] == '/') outPath[j - 1] = '\0';

    return true;
}

HOOK_DEFINE_TRAMPOLINE(CreateFileHook) {
    static uint32_t Callback(const char* path, int64_t size) {
        char newPath[768];
        if (RedirectDebugPath(path, newPath, sizeof(newPath))) {
            SafeCreateDirs(newPath);
            return Orig(newPath, size);
        }
        return Orig(path, size);
    }
};

HOOK_DEFINE_TRAMPOLINE(OpenFileHook) {
    static uint32_t Callback(void* outHandle, const char* path, int mode) {
        char newPath[768];
        if (RedirectDebugPath(path, newPath, sizeof(newPath))) {
            if (mode & (2 | 4)) { // 2 = Write, 4 = Append
                SafeCreateDirs(newPath);

                // Fix: Check if file exists via read mode
                nn::fs::FileHandle tmpHandle;
                if (R_FAILED(nn::fs::OpenFile(&tmpHandle, newPath, 1))) {
                    nn::fs::CreateFile(newPath, 0);
                } else {
                    nn::fs::CloseFile(tmpHandle);
                }
            }
            return Orig(outHandle, newPath, mode);
        }
        return Orig(outHandle, path, mode);
    }
};

HOOK_DEFINE_TRAMPOLINE(GetEntryTypeHook) {
    static uint32_t Callback(void* outType, const char* path) {
        if (!path) return Orig(outType, path);
        char newPath[768];
        if (RedirectDebugPath(path, newPath, sizeof(newPath))) return Orig(outType, newPath);
        return Orig(outType, path);
    }
};

HOOK_DEFINE_TRAMPOLINE(OpenDirectoryHook) {
    static uint32_t Callback(void* outHandle, const char* path, int mode) {
        char newPath[768];
        if (RedirectDebugPath(path, newPath, sizeof(newPath))) return Orig(outHandle, newPath, mode);
        return Orig(outHandle, path, mode);
    }
};

HOOK_DEFINE_TRAMPOLINE(DeleteFileHook) {
    static uint32_t Callback(const char* path) {
        char newPath[768];
        if (RedirectDebugPath(path, newPath, sizeof(newPath))) return Orig(newPath);
        return Orig(path);
    }
};

HOOK_DEFINE_TRAMPOLINE(RenameFileHook) {
    static uint32_t Callback(const char* oldPath, const char* newPath) {
        char newOldPath[768], newNewPath[768];
        bool rOld = RedirectDebugPath(oldPath, newOldPath, sizeof(newOldPath));
        bool rNew = RedirectDebugPath(newPath, newNewPath, sizeof(newNewPath));

        if (rOld && rNew) {
            SafeCreateDirs(newNewPath);
            nn::fs::DeleteFile(newNewPath);
            return Orig(newOldPath, newNewPath);
        }

        if (rOld) return Orig(newOldPath, newPath);
        if (rNew) {
            SafeCreateDirs(newNewPath);
            nn::fs::DeleteFile(newNewPath);
            return Orig(oldPath, newNewPath);
        }
        return Orig(oldPath, newPath);
    }
};

HOOK_DEFINE_TRAMPOLINE(CreateDirectoryHook) {
    static uint32_t Callback(const char* path) {
        char newPath[768];
        if (RedirectDebugPath(path, newPath, sizeof(newPath))) {
            SafeCreateDirs(newPath);
            return Orig(newPath);
        }
        return Orig(path);
    }
};

HOOK_DEFINE_TRAMPOLINE(FlushFileHook) {
    static uint32_t Callback(void* handle) {
        return Orig(handle);
    }
};

void Init() {

    if (!Config::enableDebug) return;

    uintptr_t addr;
    if ((addr = FindSymbol("_ZN2nn2fs10CreateFileEPKcl")))
        CreateFileHook::InstallAtFuncPtr(reinterpret_cast<decltype(&CreateFileHook::Callback)>(addr));
    if ((addr = FindSymbol("_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci")))
        OpenFileHook::InstallAtFuncPtr(reinterpret_cast<decltype(&OpenFileHook::Callback)>(addr));
    if ((addr = FindSymbol("_ZN2nn2fs12GetEntryTypeEPNS0_18DirectoryEntryTypeEPKc")))
        GetEntryTypeHook::InstallAtFuncPtr(reinterpret_cast<decltype(&GetEntryTypeHook::Callback)>(addr));
    if ((addr = FindSymbol("_ZN2nn2fs13OpenDirectoryEPNS0_15DirectoryHandleEPKci")))
        OpenDirectoryHook::InstallAtFuncPtr(reinterpret_cast<decltype(&OpenDirectoryHook::Callback)>(addr));
    if ((addr = FindSymbol("_ZN2nn2fs10DeleteFileEPKc")))
        DeleteFileHook::InstallAtFuncPtr(reinterpret_cast<decltype(&DeleteFileHook::Callback)>(addr));
    if ((addr = FindSymbol("_ZN2nn2fs15CreateDirectoryEPKc")))
        CreateDirectoryHook::InstallAtFuncPtr(reinterpret_cast<decltype(&CreateDirectoryHook::Callback)>(addr));
    if ((addr = FindSymbol("_ZN2nn2fs9FlushFileENS0_10FileHandleE")))
        FlushFileHook::InstallAtFuncPtr(reinterpret_cast<decltype(&FlushFileHook::Callback)>(addr));
    if ((addr = FindSymbol("_ZN2nn2fs10RenameFileEPKcS2_")))
        RenameFileHook::InstallAtFuncPtr(reinterpret_cast<decltype(&RenameFileHook::Callback)>(addr));
}

} // namespace FsHooks
