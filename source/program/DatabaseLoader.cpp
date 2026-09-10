#include "DatabaseLoader.hpp"
#include "ModLoader.hpp"
#include "lib.hpp"
#include "fs.hpp"
#include "macros.hpp"
#include "Allocator.hpp"
#include "Config.hpp"
#include "logger.hpp"
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

#define ADDR_RESOLVE_FILE_PATH FIX(0x001F4580)
#define ADDR_INIT_MDATA_MGR    FIX(0x0041AFC0)

static void* g_mdataMgrPtr = nullptr;

/**
 * Performance Cache: Prevents excessive SD card polling.
 * First check hits the SD card, subsequent checks hit RAM.
 */
static std::unordered_map<std::string, bool> g_dbCache;
static std::recursive_mutex g_cacheMtx;

bool FastFileExists(const std::string& path) {
    {
        std::lock_guard<std::recursive_mutex> lock(g_cacheMtx);
        auto it = g_dbCache.find(path);
        if (it != g_dbCache.end()) return it->second;
    }

    nn::fs::FileHandle h;
    bool exists = false;
    if (R_SUCCEEDED(nn::fs::OpenFile(&h, path.c_str(), nn::fs::OpenMode_Read))) {
        nn::fs::CloseFile(h);
        exists = true;
    }

    std::lock_guard<std::recursive_mutex> lock(g_cacheMtx);
    g_dbCache[path] = exists;
    return exists;
}

constexpr char MAGIC = 0x01;

/**
 * Resolves the path for mod-specific database files (e.g., song_db.mod.txt).
 */
bool resolveModDatabaseFilePath(const libcxx_string& filePath, std::string& destFilePath) {
    std::string pathStr = filePath.c_str();
    const size_t magicIdx0 = pathStr.find(MAGIC);
    if (magicIdx0 == std::string::npos) return false;

    const size_t magicIdx1 = pathStr.find(MAGIC, magicIdx0 + 1);
    if (magicIdx1 == std::string::npos) return false;

    const std::string left = pathStr.substr(0, magicIdx0);
    const std::string center = pathStr.substr(magicIdx0 + 1, magicIdx1 - magicIdx0 - 1);
    const std::string right = pathStr.substr(magicIdx1 + 1);

    destFilePath = center + "/" + left + "mod" + right;

    size_t pos;
    while ((pos = destFilePath.find("//")) != std::string::npos) {
        destFilePath.replace(pos, 2, "/");
    }

    return true;
}

/**
 * Hook to override the game's file resolution logic to prioritize modded files.
 */
HOOK_DEFINE_TRAMPOLINE(ResolveFilePathObserverHook) {
    static uint64_t Callback(libcxx_string* filePath, libcxx_string* destFilePath) {

        std::string destPathTmp;
        if (filePath && resolveModDatabaseFilePath(*filePath, destPathTmp)) {
            if (FastFileExists(destPathTmp)) {
                if (destFilePath) {
                    destFilePath->assign(destPathTmp.c_str(), destPathTmp.length());
                }
                return 1;
            }
            return 0;
        }

        // -- fix jp forced broken pause menu --
        // 1. Execute original path resolution
        uint64_t ret = Orig(filePath, destFilePath);
        // 2. Intercept and redirect DLC18 spr_db if forceJapanese is enabled
        if (ret && destFilePath) {
            const char* resolved = destFilePath->c_str();

            // Check if DLC18 spr_db.bin was resolved
            if (Config::forceJapanese && std::strstr(resolved, "/dlc18/") != nullptr && std::strstr(resolved, "/spr_db.bin") != nullptr) {
                // Redirect to the clean original Japanese base database
                const char* jpBasePath = "./rom_switch/./rom/2d/spr_db.bin";
                destFilePath->assign(jpBasePath, std::strlen(jpBasePath));
                return 1; // Return success with redirected path
            }
        }

        return ret;
    }
};

HOOK_DEFINE_TRAMPOLINE(InitMdataMgrHook) {
    static void Callback(void* this_ptr) {
        Orig(this_ptr);
        g_mdataMgrPtr = this_ptr;
    }
};

void DatabaseLoader::init() {
    ResolveFilePathObserverHook::InstallAtOffset(ADDR_RESOLVE_FILE_PATH);
    InitMdataMgrHook::InstallAtOffset(ADDR_INIT_MDATA_MGR);
}

void DatabaseLoader::initMdataMgr(const std::vector<std::string>& modRomDirectoryPaths) {
    if (!g_mdataMgrPtr) return;

    auto list = reinterpret_cast<libcxx_list*>((uintptr_t)g_mdataMgrPtr + 0x178);

    // Inject mod directory markers using the special MAGIC character
    for (auto it = modRomDirectoryPaths.rbegin(); it != modRomDirectoryPaths.rend(); ++it) {
        std::string path;
        path += MAGIC; path += *it; path += MAGIC; path += "_";
        list->push_back(path.c_str());
    }
}
