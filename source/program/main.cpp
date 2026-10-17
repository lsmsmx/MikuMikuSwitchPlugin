#include "lib.hpp"

#include "SaveDataSystem.hpp"

#include "SpriteLoader.hpp"
#include "Config.hpp"
#include "ModLoader.hpp"
#include "DatabaseLoader.hpp"
#include "DebugMode.hpp"
#include "freecam.hpp"
#include "FsHooks.hpp"
#include "FTRestoration.hpp"
#include "lib.hpp"
#include "ImGui.hpp"
#include "nvn_hooks.hpp"
#include "PvLoader.hpp"
#include "patches.hpp"
#include "fs.hpp"
#include "StrArray.hpp"
#include "AetDB.hpp"
#include "SpriteDrawLimitPatch.hpp"
#include "keyboard.hpp"
#include "AutoPlay.hpp"
#include "Sound.hpp"
#include "Mode2DNoMV.hpp"
#include "SprBackFix.hpp"
#include "Dimming.hpp"
#include "TheaterRestoration.hpp"
#include "overlays/SongQuickJump.hpp"
#include "overlays/VolumeOverlay.hpp"
#include "overlays/SaveBackup.hpp"

//#include "ft_ui/ft_ui.hpp"
#include "nc/nc.hpp"
#include "nc/save_data.hpp"
#include "shared_nc_ft/shared_hooks.hpp"

#include "MemoryHooks.hpp"

#include "logger.hpp"

#define MOUNT_NAME          "ExlSD"

#include <stdint.h>
#include <string.h>

// nvn function from ImGUI
void MyOnPresent(NVNqueue *queue, NVNwindow *window, int texture_index);

// =========================================================
// INITIALIZATION
// =========================================================

extern "C" void nnMain();
HOOK_DEFINE_TRAMPOLINE(MainHook) {
    static void Callback() {
        nn::fs::MountSdCardForDebug(MOUNT_NAME);

        Config::init();
        ModLoader::init();
        DebugMode::Init();
        ApplyCustomPatches();
        FTRestoration::init();
        Dimming::init();
        InitFreeCam();

        nvn_hooks::InstallHooks(&MyOnPresent);
        ImGui::Init();

        nc::init();
        shared_hooks::init();

        keyboard::init();

        FsHooks::Init();
        SustainAudio::Init();
        SaveBackup::Init();
        Mode2DNoMV::init();
        VolumeOverlay::Init();
        MemoryHooks::Init();

        Orig();
    }
};


extern "C" void exl_main(void* x0, void* x1) {

    exl::hook::Initialize();

    nn::hid::Initialize();

    MainHook::InstallAtFuncPtr(nnMain);

    SaveDataSystem::init();

    DatabaseLoader::init();
    StrArray::init();
    SpriteLoader::init();
    PvLoader::init();
    AetDB::init();
    SpriteDrawLimitPatch::init();
    AutoPlay::init();
    BackgroundScaleFix::init();
    TheaterRestoration::init();

};
