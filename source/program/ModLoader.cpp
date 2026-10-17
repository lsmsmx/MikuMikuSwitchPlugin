#include "ModLoader.hpp"
#include "DatabaseLoader.hpp"
#include "Config.hpp"
#include "toml.hpp"
#include "lib.hpp"
#include "fs.hpp"
#include "macros.hpp"
#include "Allocator.hpp"
#include <algorithm>
#include <vector>
#include <string>
#include <cstring>

#define ADDR_INIT_ROM_DIR    FIX(0x001F1940)
#define ADDR_ROM_DIR_PATHS   0x00CDF7C0

std::vector<std::string> ModLoader::modDirectoryPaths;
static std::vector<std::string> s_modRomPaths;

static bool DirectoryExists(const std::string& path) {
    nn::fs::DirectoryHandle dh;
    if (R_SUCCEEDED(nn::fs::OpenDirectory(&dh, path.c_str(), nn::fs::OpenDirectoryMode_Directory))) {
        nn::fs::CloseDirectory(dh);
        return true;
    }
    return false;
}

static std::string ToVirtualRomPath(const std::string& physicalPath) {
    std::string virtualPath = physicalPath;
    size_t romfsPos = virtualPath.find("/romfs/");
    if (romfsPos != std::string::npos) {
        virtualPath = "rom:/" + virtualPath.substr(romfsPos + 7);
    }
    return virtualPath;
}

void ModLoader::initMod(const std::string& path) {
    std::string configPath = path + "/config.toml";
    nn::fs::FileHandle h;

    // If no mod-specific config exists, treat the mod as enabled by default
    if (R_FAILED(nn::fs::OpenFile(&h, configPath.c_str(), nn::fs::OpenMode_Read))) {
        modDirectoryPaths.push_back(path);
        return;
    }

    int64_t size = 0;
    nn::fs::GetFileSize(&size, h);
    std::string content(size, '\0');
    nn::fs::ReadFile(h, 0, content.data(), size);
    nn::fs::CloseFile(h);

    toml::parse_result result = toml::parse(content);
    if (!result) return;

    toml::table config = std::move(result).table();
    if (!config["enabled"].value_or(true)) return;

    // Handle "include" array if present
    if (auto includeArr = config["include"].as_array()) {
        for (auto& elem : *includeArr) {
            std::string sub = elem.value_or("");
            if (!sub.empty()) {
                if (sub == ".") modDirectoryPaths.push_back(path);
                else modDirectoryPaths.push_back(path + "/" + sub);
            }
        }
    } else {
        modDirectoryPaths.push_back(path);
    }
}

/**
 * Hook to inject our mod paths into the game's internal RomFS search list.
 */
HOOK_DEFINE_TRAMPOLINE(InitRomDirectoryPathsHook) {
    static void Callback() {
        Orig();
        uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
        auto romDirectoryPaths = *reinterpret_cast<libcxx_vector**>(base + ADDR_ROM_DIR_PATHS);

        // Inject mod paths at the beginning of the list for highest priority
        if (romDirectoryPaths && !s_modRomPaths.empty()) {
            romDirectoryPaths->insert_front(s_modRomPaths);
        }
    }
};

void ModLoader::init() {
    if (!Config::enableMods || Config::modsDirectoryPath.empty()) return;

    // 1. Process mods based on the synchronized priority list
    for (const auto& modName : Config::priorityPaths) {
        std::string modDirectory = Config::modsDirectoryPath + "/" + modName;
        if (DirectoryExists(modDirectory)) {
            initMod(modDirectory);
        }
    }

    // 2. Resolve Steam or Vanilla mod root paths to virtual RomFS paths
    for (auto& modDir : modDirectoryPaths) {
        bool hasSteamJp = DirectoryExists(modDir + "/rom_steam/rom");
        bool hasSteamEn = DirectoryExists(modDir + "/rom_steam_en/rom");

        std::string targetModPath = "";

        if (Config::forceJapanese) {
            // Japanese region mode: pick rom_steam, or fallback to rom_steam_en if missing
            if (hasSteamJp) {
                targetModPath = modDir + "/rom_steam";
            } else if (hasSteamEn) {
                targetModPath = modDir + "/rom_steam_en";
            }
        } else {
            // Default/English mode: pick rom_steam_en, or fallback to rom_steam if missing
            if (hasSteamEn) {
                targetModPath = modDir + "/rom_steam_en";
            } else if (hasSteamJp) {
                targetModPath = modDir + "/rom_steam";
            }
        }

        // If a Steam directory was selected, mount it directly as the mod root
        if (!targetModPath.empty()) {
            s_modRomPaths.push_back(ToVirtualRomPath(targetModPath));
        } else if (DirectoryExists(modDir + "/rom")) {
            // Fallback for standard Switch mod structure (modfolder/rom)
            s_modRomPaths.push_back(ToVirtualRomPath(modDir));
        }
    }

    // Initialize mod prefixes for the Database (MdataMgr)
    if (!s_modRomPaths.empty()) {
        DatabaseLoader::initMdataMgr(s_modRomPaths);
        InitRomDirectoryPathsHook::InstallAtOffset(ADDR_INIT_ROM_DIR);
    }
}
