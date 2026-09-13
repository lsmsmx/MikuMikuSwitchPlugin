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
#include "PvLoader.hpp"
#include "patches.hpp"
#include "fs.hpp"
#include "StrArray.hpp"
#include "AetDB.hpp"
#include "SpriteDrawLimitPatch.hpp"
#include "keyboard_sliders.hpp"

//#include "ft_ui/ft_ui.hpp"
#include "nc/nc.hpp"
#include "nc/save_data.hpp"
#include "shared_nc_ft/shared_hooks.hpp"

#include "logger.hpp"

#define MOUNT_NAME          "ExlSD"

#include <stdint.h>
#include <string.h>



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
        InitFreeCam();
        ImGui::Init();

        nc::init();
        shared_hooks::init();

        keyboard_sliders::init();

        FsHooks::Init();

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


};
