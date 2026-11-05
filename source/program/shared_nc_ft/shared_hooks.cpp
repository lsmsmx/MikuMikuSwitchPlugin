#include "shared_hooks.hpp"
#include "../Config.hpp"
#include "../ModLoader.hpp"
#include "../ThumbnailLoader.hpp"

namespace nc { void OnPvDbRead(uint64_t task); }
namespace pvSel { void OnPvDbRead(uint64_t task); }

namespace shared_hooks {


HOOK_DEFINE_TRAMPOLINE(TaskPvDBCtrlSharedHook) {
    static bool Callback(uint64_t task) {
        bool ret = Orig(task);

        // Not gated: modded PV thumbnails are a DivaModLoader feature, independent
        // of the nc/FT switches.
        ThumbnailLoader::OnPvDbTask();

        if (Config::enableNewClassics) {
            nc::OnPvDbRead(task);
        }

        if (Config::enableFtUi) {
            pvSel::OnPvDbRead(task);
        }

        return ret;
    }
};

void init() {
    TaskPvDBCtrlSharedHook::InstallAtOffset(0x004c0ff0);
}

} // namespace shared_hooks
