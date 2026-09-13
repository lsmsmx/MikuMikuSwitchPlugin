#include "Config.hpp"
#include "toml.hpp"
#include "lib.hpp"
#include "fs.hpp"
#include "hid.hpp"
#include <cstring>
#include <algorithm>
#include <set>
#include <string>
#include <vector>


bool Config::enableMods = true;
bool Config::enableDebug = false;
std::string Config::modsDirectoryPath = "";
std::vector<std::string> Config::priorityPaths;

// Feature Master Toggles
bool Config::enableFtUi = false;
bool Config::enableNewClassics = false;

// Gameplay
std::string Config::challengeTimeMode = "default";
bool Config::removeWatermarks = true;
bool Config::disableHandScaling = false;
bool Config::disableLyrics = false;
bool Config::forceJapanese = false;
bool Config::forceFtUI = false;
bool Config::ExPatch = true;
bool Config::enableTouch = false;
bool Config::enableKeyboard = false;

// Graphics
bool Config::disableAdp = false;
bool Config::enableSss = false;
bool Config::cstmMenuFtStyle = false;
bool Config::extraFtGraphics = false;
std::string Config::antiAliasing = "fxaa";
std::string Config::magFilterMode = "default";
std::string Config::ssaaMode = "off";
float Config::shadowIntensity = 1.0f;
float Config::reflectionQuality = 1.0f;
float Config::exposure = -1.0f;
float Config::gamma = -1.0f;
float Config::fxaaQualitySubpix = -1.0f;
float Config::fxaaQualityEdgeThreshold = -1.0f;
float Config::fxaaQualityEdgeThresholdMin = -1.0f;

// Force Disables
bool Config::disableReflections = false;
bool Config::disableShadows = false;
bool Config::disableSelfShadow = false;
bool Config::disableDOF = false;

// Preset Toggles
bool Config::ftShadows = false;
bool Config::ftReflect = false;
bool Config::ftRefract = false;
bool Config::advancedGraphics = false;

// Optimisations
bool Config::force30fps = false;
float Config::resScaler = 1.0f;

// Advanced Resolutions
uint32_t Config::shadowMap1W = 2048;
uint32_t Config::shadowMap1H = 2048;
uint32_t Config::shadowViewportW = 2048;
uint32_t Config::shadowViewportH = 512;
uint32_t Config::shadowTex1W = 2048;
uint32_t Config::shadowTex1H = 512;
uint32_t Config::shadowTex2W = 1280;
uint32_t Config::shadowTex2H = 720;
uint32_t Config::shadowMap3W = 2048;
uint32_t Config::shadowMap3H = 2048;
uint32_t Config::shadowMap4W = 2048;
uint32_t Config::shadowMap4H = 2048;
uint32_t Config::shadowMap5W = 512;
uint32_t Config::shadowMap5H = 512;
uint32_t Config::shadowMap6W = 512;
uint32_t Config::shadowMap6H = 512;
uint32_t Config::reflectW = 1024;
uint32_t Config::reflectH = 512;
uint32_t Config::refractW = 1024;
uint32_t Config::refractH = 512;

static void SaveConfig(const std::string& path) {
    std::string tomlContent = "# =========================================================\n";
    tomlContent += "# MikuMikuSwitchPlugin - Global Configuration\n";
    tomlContent += "# Author: lsmsmx\n";
    tomlContent += "# =========================================================\n\n";
    tomlContent += "mods = " + std::string(Config::enableMods ? "true" : "false") +"\n";
    tomlContent += "debug = " + std::string(Config::enableDebug ? "true" : "false") + "\n\n";
    tomlContent += "# Priority list (Top is highest priority).\n";
    tomlContent += "# New mods found on SD are automatically appended here.\n";
    tomlContent += "priority = [\n";

    for (const auto& name : Config::priorityPaths) {
        tomlContent += "    \"" + name + "\",\n";
    }
    tomlContent += "]\n\n";

    tomlContent += "[gameplay]\n";
    tomlContent += "# Master toggle for New Classics mod features\n";
    tomlContent += "new_classics = " + std::string(Config::enableNewClassics ? "true" : "false") + "\n";
    tomlContent += "# Challenge Time mode: \"enabled\" (force all difficulties), \"disabled\" (completely off), \"default\" (vanilla)\n";
    tomlContent += "challenge_time = \"" + Config::challengeTimeMode + "\"\n";
    tomlContent += "# Removes Copyright & PV watermark text during playback\n";
    tomlContent += "remove_watermarks = " + std::string(Config::removeWatermarks ? "true" : "false") + "\n";
    tomlContent += "# Disables hand model scaling in PVs\n";
    tomlContent += "disable_hand_scaling = " + std::string(Config::disableHandScaling ? "true" : "false") + "\n";
    tomlContent += "# Disables PV lyrics display\n";
    tomlContent += "disable_lyrics = " + std::string(Config::disableLyrics ? "true" : "false") + "\n";
    tomlContent += "# Forces Japanese region/language mode\n";
    tomlContent += "force_japanese = " + std::string(Config::forceJapanese ? "true" : "false") + "\n";
    tomlContent += "# PS4 FTUI forced leftovers\n";
    tomlContent += "force_ft_ui = " + std::string(Config::forceFtUI ? "true" : "false") + "\n";
    tomlContent += "# ExPatch (unlocks Extreme charts by default)\n";
    tomlContent += "ExPatch = " + std::string(Config::ExPatch ? "true" : "false") + "\n";
    tomlContent += "# Touch-to-Sliders SUPPORT (two sticks all directions imitation with touchscreen)\n";
    tomlContent += "enable_touch = " + std::string(Config::enableTouch ? "true" : "false") + "\n";
    tomlContent += "# USB KEYBOARD SUPPORT\n";
    tomlContent += "# Notes / D-Pad: W, A, S, D / Arrows\n";
    tomlContent += "# Arcade Buttons: I, J, K, L\n";
    tomlContent += "# Left Stick: Q, E (Left / Right)\n";
    tomlContent += "# Right Stick: U, O (Left / Right)\n";
    tomlContent += "# Triggers: LeftShift/X (L), RightShift/M (R), LeftCtrl/Z/CapsLock (ZL), RightCtrl/,/;/Spacebar (ZR)\n";
    tomlContent += "# Menus: Esc (B), Enter/P (+), Tab (-)\n";
    tomlContent += "enable_keyboard = " + std::string(Config::enableKeyboard ? "true" : "false") + "\n\n";

    tomlContent += "[graphics]\n";
    tomlContent += "# NOTE: for best looking graphics use dock/fakedock mode using ReverseNX + -no_npr in args.txt\n";
    tomlContent += "# Disables ADP (Adaptive Performance) system completely\n";
    tomlContent += "disable_adp = " + std::string(Config::disableAdp ? "true" : "false") + "\n";
    tomlContent += "# Enable Subsurface Scattering (SSS) for Future Tone style graphics\n";
    tomlContent += "enable_sss = " + std::string(Config::enableSss ? "true" : "false") + "\n";
    tomlContent += "# Customization menu graphics in AFT/FT style (removes cell-shading in menu)\n";
    tomlContent += "cstm_menu_ft_style = " + std::string(Config::cstmMenuFtStyle ? "true" : "false") + "\n";
    tomlContent += "# Anti-Aliasing mode: \"mlaa\", \"fxaa\", \"off\"\n";
    tomlContent += "anti_aliasing = \"" + Config::antiAliasing + "\"\n";
    tomlContent += "# Texture Magnification Filter: \"bilinear\", \"nearest\", \"sharpen_5tap\", \"sharpen_4tap\", \"cone_4tap\", \"cone_2tap\", \"default\"\n";
    tomlContent += "mag_filter = \"" + Config::magFilterMode + "\"\n";
    tomlContent += "# SSAA (Super Sampling) mode: \"on\", \"off\"\n";
    tomlContent += "ssaa_mode = \"" + Config::ssaaMode + "\"\n";
    tomlContent += "# Force Disables (true = Disabled, false = Vanilla Enabled)\n";
    tomlContent += "force_disable_reflections = " + std::string(Config::disableReflections ? "true" : "false") + "\n";
    tomlContent += "force_disable_shadows = " + std::string(Config::disableShadows ? "true" : "false") + "\n";
    tomlContent += "force_disable_self_shadow = " + std::string(Config::disableSelfShadow ? "true" : "false") + "\n";
    tomlContent += "force_disable_DOF = " + std::string(Config::disableDOF ? "true" : "false") + "\n";
    tomlContent += "# Forces Future Tone Mode extra patches to get maximumly close to MM+ \n";
    tomlContent += "extraFtGraphics = " + std::string(Config::extraFtGraphics ? "true" : "false") + "\n";

    tomlContent += "# Exposure value multiplier: 0.0 to 4.0 (Default -1.0)\n";
    tomlContent += "exposure = " + std::to_string(Config::exposure) + "\n";
    tomlContent += "# Gamma correction: 0.0 to 1.0 (Set to -1.0 for game default)\n";
    tomlContent += "gamma = " + std::to_string(Config::gamma) + "\n";

    tomlContent += "# FXAA Settings: 0.0 to 1.0 (Set to -1.0 for game default)\n";
    tomlContent += "fxaa_subpix = " + std::to_string(Config::fxaaQualitySubpix) + "\n";
    tomlContent += "fxaa_edge_threshold = " + std::to_string(Config::fxaaQualityEdgeThreshold) + "\n";
    tomlContent += "fxaa_edge_threshold_min = " + std::to_string(Config::fxaaQualityEdgeThresholdMin) + "\n\n";

    tomlContent += "# Preset resolution toggles using predefined FT/AFT resolution patches\n";
    tomlContent += "ft_shadows = " + std::string(Config::ftShadows ? "true" : "false") + "\n";
    tomlContent += "ft_reflect = " + std::string(Config::ftReflect ? "true" : "false") + "\n";
    tomlContent += "ft_refract = " + std::string(Config::ftRefract ? "true" : "false") + "\n";
    tomlContent += "# Enables advanced mode: overrides FT presets with custom resolutions from advanced\n";
    tomlContent += "advanced_graphics = " + std::string(Config::advancedGraphics ? "true" : "false") + "\n\n";

    tomlContent += "[graphics.optimisations]\n";
    tomlContent += "# Enables 30 FPS rendering limits\n";
    tomlContent += "force_30fps_rendering = " + std::string(Config::force30fps ? "true" : "false") + "\n";
    tomlContent += "# Resolution scaling factor (Stub for scale getter): 1.0, 0.9, 0.8, 0.675, 0.6, 0.5\n";
    tomlContent += "res_scaler = " + std::to_string(Config::resScaler) + "\n";
    tomlContent += "# Reflection quality multiplier: 0.0 to 1.0 (Default 1.0)\n";
    tomlContent += "reflection_quality = " + std::to_string(Config::reflectionQuality) + "\n";
    tomlContent += "# Shadow intensity/opacity multiplier: 0.0 to 1.4\n";
    tomlContent += "# WARNING: Setting shadow_intensity higher than 1.0 may break shadow rendering!\n";
    tomlContent += "shadow_intensity = " + std::to_string(Config::shadowIntensity) + "\n\n";

    tomlContent += "[graphics.advanced]\n";
    tomlContent += "# Individual custom resolutions (Active ONLY when advanced_graphics = true)\n";
    tomlContent += "shadow_map_1_w = " + std::to_string(Config::shadowMap1W) + "\n";
    tomlContent += "shadow_map_1_h = " + std::to_string(Config::shadowMap1H) + "\n";
    tomlContent += "shadow_viewport_w = " + std::to_string(Config::shadowViewportW) + "\n";
    tomlContent += "shadow_viewport_h = " + std::to_string(Config::shadowViewportH) + "\n";
    tomlContent += "shadow_tex_1_w = " + std::to_string(Config::shadowTex1W) + "\n";
    tomlContent += "shadow_tex_1_h = " + std::to_string(Config::shadowTex1H) + "\n";
    tomlContent += "shadow_tex_2_w = " + std::to_string(Config::shadowTex2W) + "\n";
    tomlContent += "shadow_tex_2_h = " + std::to_string(Config::shadowTex2H) + "\n";
    tomlContent += "shadow_map_3_w = " + std::to_string(Config::shadowMap3W) + "\n";
    tomlContent += "shadow_map_3_h = " + std::to_string(Config::shadowMap3H) + "\n";
    tomlContent += "shadow_map_4_w = " + std::to_string(Config::shadowMap4W) + "\n";
    tomlContent += "shadow_map_4_h = " + std::to_string(Config::shadowMap4H) + "\n";
    tomlContent += "shadow_map_5_w = " + std::to_string(Config::shadowMap5W) + "\n";
    tomlContent += "shadow_map_5_h = " + std::to_string(Config::shadowMap5H) + "\n";
    tomlContent += "shadow_map_6_w = " + std::to_string(Config::shadowMap6W) + "\n";
    tomlContent += "shadow_map_6_h = " + std::to_string(Config::shadowMap6H) + "\n";
    tomlContent += "reflect_w = " + std::to_string(Config::reflectW) + "\n";
    tomlContent += "reflect_h = " + std::to_string(Config::reflectH) + "\n";
    tomlContent += "refract_w = " + std::to_string(Config::refractW) + "\n";
    tomlContent += "refract_h = " + std::to_string(Config::refractH) + "\n\n";

    tomlContent += "#                    HOTKEYS SHEET                  \n";
    tomlContent += "# [ Touchscreen->Mouse ]       : One/Two fingers\n";
    tomlContent += "# [ IMGUI ]\n";
    tomlContent += "#   - Hold Plus + Minus (0.8s) : Toggle Debug Menu\n";
    tomlContent += "#   - Hold ZL + ZR (0.5s)      : Toggle Focus (Game <-> Menu)\n";
    tomlContent += "#   - LStick (+ Hold Y)        : Cursor move (+ Turbo speed)\n";
    tomlContent += "#   - ZL / ZR                  : Left Click / Right Click\n";
    tomlContent += "#   - Hold L3 + R3 (0.8s)      : Cycle Input Overlay (Off/Gamepad/Keyboard)\n";
    tomlContent += "#   - Hold L3 + R3 + ZL (0.8s) : Toggle BSS RAM Overlay\n";
    tomlContent += "#   - Hold L3 + R3 + ZR (0.8s) : Toggle Resolution Scaler Overlay\n";
    tomlContent += "#   - L3 (in RAM Overlay)      : Rescan RAM\n";
    tomlContent += "#   - F10                      : Toggle Keyboard Overlay\n";
    tomlContent += "#   - ZL in cstm menu          : Toggle New Classics Options window\n";
    tomlContent += "# [ FREECAM ]\n";
    tomlContent += "#   - L + R + Minus            : Toggle Freecam\n";
    tomlContent += "#   - D-Pad                    : Move Forward / Back / Left / Right\n";
    tomlContent += "#   - RStick                   : Rotate Camera (Yaw / Pitch)\n";
    tomlContent += "#   - Click R3                 : Turbo move speed\n";
    tomlContent += "#   - X / B                    : Up / Down (Elevation)\n";
    tomlContent += "#   - L / R                    : Zoom FOV\n";
    tomlContent += "#   - Hold Y + (L / R)         : Roll Camera (Tilt)\n";
    tomlContent += "# [ DEBUG MODE ]\n";
    tomlContent += "#   - L + R + Plus             : Toggle Debug Mode\n";
    tomlContent += "#   - L + R + D-Pad Down       : State -> DATA_TEST\n";
    tomlContent += "#   - L + R + D-Pad Up         : State -> TEST_MODE\n";
    tomlContent += "#   - L + R + D-Pad Right      : State -> MENU_SWITCH\n";
    tomlContent += "#   - L + R + D-Pad Left       : State -> CS_MENU\n";
    tomlContent += "#   - Click L3                 : Toggle Engine Mouse (LStick+Y: Move, ZL/ZR: Clicks)\n";
    tomlContent += "#   - Hold R3 + D-Pad Up/Down  : Select Debug Scene (Press A to load)\n\n";

    tomlContent += "[leftover]\n";
    tomlContent += "# Master toggle for FT UI (its dead so dont enable it)\n";
    tomlContent += "ft_ui = " + std::string(Config::enableFtUi ? "true" : "false") + "\n";

    nn::fs::DeleteFile(path.c_str());
    nn::fs::CreateFile(path.c_str(), tomlContent.length());

    nn::fs::FileHandle h;
    if (R_SUCCEEDED(nn::fs::OpenFile(&h, path.c_str(), nn::fs::OpenMode_Write))) {
        nn::fs::WriteFile(h, 0, tomlContent.c_str(), tomlContent.length(), nn::fs::WriteOption::CreateOption(nn::fs::WriteOptionFlag_Flush));
        nn::fs::CloseFile(h);
    }
}

bool Config::init() {
    const char* possible_tids[] = {
        "0100F3100DA46000", // JP
        "01001CC00FA1A000", // EN
        "0100BE300FF62000"  // KR
    };

    std::string atmoPath = "";
    nn::fs::DirectoryHandle dh;
    for (const char* tid : possible_tids) {
        std::string testPath = "ExlSD:/atmosphere/contents/" + std::string(tid);
        if (R_SUCCEEDED(nn::fs::OpenDirectory(&dh, testPath.c_str(), nn::fs::OpenDirectoryMode_Directory))) {
            nn::fs::CloseDirectory(dh);
            atmoPath = testPath;
            break;
        }
    }

    if (atmoPath.empty()) return false;

    modsDirectoryPath = atmoPath + "/romfs/mods";
    nn::fs::CreateDirectory(modsDirectoryPath.c_str());
    nn::fs::CreateDirectory("ExlSD:/MikuMikuSwitchPlugin");

    std::string configPath = "ExlSD:/MikuMikuSwitchPlugin/config.toml";
    nn::fs::FileHandle h;
    bool configExists = R_SUCCEEDED(nn::fs::OpenFile(&h, configPath.c_str(), nn::fs::OpenMode_Read));

    if (configExists) {
        int64_t size = 0;
        nn::fs::GetFileSize(&size, h);
        std::string content(size, '\0');
        nn::fs::ReadFile(h, 0, content.data(), size);
        nn::fs::CloseFile(h);

        auto result = toml::parse(content);
        if (result) {
            auto config = std::move(result).table();
            enableMods = config["mods"].value_or(true);
            enableDebug = config["debug"].value_or(false);

            if (enableMods) {
                if (auto pArr = config["priority"].as_array()) {
                    for (auto&& el : *pArr) {
                        if (auto val = el.value<std::string>()) {
                            if (!val->empty()) priorityPaths.push_back(*val);
                        }
                    }
                }
            }

            // Gameplay Master Toggles
            enableNewClassics = config["gameplay"]["new_classics"].value_or(false);

            challengeTimeMode = config["gameplay"]["challenge_time"].value_or("enabled");
            removeWatermarks = config["gameplay"]["remove_watermarks"].value_or(true);
            disableHandScaling = config["gameplay"]["disable_hand_scaling"].value_or(false);
            disableLyrics = config["gameplay"]["disable_lyrics"].value_or(false);
            forceJapanese = config["gameplay"]["force_japanese"].value_or(false);
            forceFtUI = config["gameplay"]["force_ft_ui"].value_or(false);
            ExPatch = config["gameplay"]["ExPatch"].value_or(true);
            enableTouch = config["gameplay"]["enable_touch"].value_or(false);
            enableKeyboard = config["gameplay"]["enable_keyboard"].value_or(false);

            nn::hid::g_keyboardModeEnabled = enableKeyboard;

            // Graphics
            disableAdp = config["graphics"]["disable_adp"].value_or(false);
            antiAliasing = config["graphics"]["anti_aliasing"].value_or("fxaa");
            enableSss = config["graphics"]["enable_sss"].value_or(false);
            cstmMenuFtStyle = config["graphics"]["cstm_menu_ft_style"].value_or(false);
            ssaaMode = config["graphics"]["ssaa_mode"].value_or("off");
            exposure = (float)config["graphics"]["exposure"].value_or(-1.0f);
            gamma = (float)config["graphics"]["gamma"].value_or(-1.0f);
            fxaaQualitySubpix = (float)config["graphics"]["fxaa_subpix"].value_or(-1.0f);
            fxaaQualityEdgeThreshold = (float)config["graphics"]["fxaa_edge_threshold"].value_or(-1.0f);
            fxaaQualityEdgeThresholdMin = (float)config["graphics"]["fxaa_edge_threshold_min"].value_or(-1.0f);
            disableReflections = config["graphics"]["force_disable_reflections"].value_or(false);
            disableShadows = config["graphics"]["force_disable_shadows"].value_or(false);
            disableSelfShadow = config["graphics"]["force_disable_self_shadow"].value_or(false);
            disableDOF = config["graphics"]["force_disable_DOF"].value_or(false);
            extraFtGraphics = config["graphics"]["extraFtGraphics"].value_or(false);
            magFilterMode = config["graphics"]["mag_filter"].value_or("default");

            ftShadows = config["graphics"]["ft_shadows"].value_or(false);
            ftReflect = config["graphics"]["ft_reflect"].value_or(false);
            ftRefract = config["graphics"]["ft_refract"].value_or(false);
            advancedGraphics = config["graphics"]["advanced_graphics"].value_or(false);

            // Graphics -> Optimisations
            force30fps = config["graphics"]["optimisations"]["force_30fps_rendering"].value_or(false);
            resScaler = (float)config["graphics"]["optimisations"]["res_scaler"].value_or(1.0f);
            reflectionQuality = (float)config["graphics"]["optimisations"]["reflection_quality"].value_or(1.0f);
            shadowIntensity = (float)config["graphics"]["optimisations"]["shadow_intensity"].value_or(1.0f);

            // Graphics -> Advanced
            shadowMap1W = (uint32_t)config["graphics"]["advanced"]["shadow_map_1_w"].value_or(2048);
            shadowMap1H = (uint32_t)config["graphics"]["advanced"]["shadow_map_1_h"].value_or(2048);
            shadowViewportW = (uint32_t)config["graphics"]["advanced"]["shadow_viewport_w"].value_or(2048);
            shadowViewportH = (uint32_t)config["graphics"]["advanced"]["shadow_viewport_h"].value_or(512);
            shadowTex1W = (uint32_t)config["graphics"]["advanced"]["shadow_tex_1_w"].value_or(2048);
            shadowTex1H = (uint32_t)config["graphics"]["advanced"]["shadow_tex_1_h"].value_or(512);
            shadowTex2W = (uint32_t)config["graphics"]["advanced"]["shadow_tex_2_w"].value_or(1280);
            shadowTex2H = (uint32_t)config["graphics"]["advanced"]["shadow_tex_2_h"].value_or(720);
            shadowMap3W = (uint32_t)config["graphics"]["advanced"]["shadow_map_3_w"].value_or(2048);
            shadowMap3H = (uint32_t)config["graphics"]["advanced"]["shadow_map_3_h"].value_or(2048);
            shadowMap4W = (uint32_t)config["graphics"]["advanced"]["shadow_map_4_w"].value_or(2048);
            shadowMap4H = (uint32_t)config["graphics"]["advanced"]["shadow_map_4_h"].value_or(2048);
            shadowMap5W = (uint32_t)config["graphics"]["advanced"]["shadow_map_5_w"].value_or(512);
            shadowMap5H = (uint32_t)config["graphics"]["advanced"]["shadow_map_5_h"].value_or(512);
            shadowMap6W = (uint32_t)config["graphics"]["advanced"]["shadow_map_6_w"].value_or(512);
            shadowMap6H = (uint32_t)config["graphics"]["advanced"]["shadow_map_6_h"].value_or(512);
            reflectW = (uint32_t)config["graphics"]["advanced"]["reflect_w"].value_or(1024);
            reflectH = (uint32_t)config["graphics"]["advanced"]["reflect_h"].value_or(512);
            refractW = (uint32_t)config["graphics"]["advanced"]["refract_w"].value_or(1024);
            refractH = (uint32_t)config["graphics"]["advanced"]["refract_h"].value_or(512);

            enableFtUi = config["leftover"]["ft_ui"].value_or(false);
        }
    }

    bool configChanged = !configExists;

    if(enableMods) {
        // Scan SD card for mods
        std::vector<std::string> modsOnDisk;
        if (R_SUCCEEDED(nn::fs::OpenDirectory(&dh, modsDirectoryPath.c_str(), nn::fs::OpenDirectoryMode_Directory))) {
            int64_t count = 0;
            nn::fs::DirectoryEntry entry;
            while (R_SUCCEEDED(nn::fs::ReadDirectory(&count, &entry, dh, 1)) && count > 0) {

                // 1. Hardware Switch and Yuzu check (offset 0x301)
                bool isDir = ((int)entry.m_Type == (int)nn::fs::DirectoryEntryType_Directory);

                // 2. Ryujinx check (offset 0x304)
                uint8_t* rawBytes = reinterpret_cast<uint8_t*>(&entry);
                if (!isDir && rawBytes[0x304] == (uint8_t)nn::fs::DirectoryEntryType_Directory) {
                    isDir = true;
                }

                // If it is a directory on any supported platform, append to list
                if (isDir) {
                    modsOnDisk.push_back(entry.m_Name);
                }
            }
            nn::fs::CloseDirectory(dh);
        }
        std::sort(modsOnDisk.begin(), modsOnDisk.end());

        std::set<std::string> currentDiskSet(modsOnDisk.begin(), modsOnDisk.end());
        auto it = priorityPaths.begin();
        while (it != priorityPaths.end()) {
            if (currentDiskSet.find(*it) == currentDiskSet.end()) {
                it = priorityPaths.erase(it);
                configChanged = true;
            } else {
                ++it;
            }
        }

        std::set<std::string> currentPrioritySet(priorityPaths.begin(), priorityPaths.end());
        for (const auto& modName : modsOnDisk) {
            if (currentPrioritySet.find(modName) == currentPrioritySet.end()) {
                priorityPaths.push_back(modName);
                configChanged = true;
            }
        }
    }
    if (configChanged) {
        SaveConfig(configPath);
    }

    return true;

}
