#include "shared_hooks.hpp"
#include "../Config.hpp"
#include "../ModLoader.hpp"


namespace nc { void OnPvDbRead(uint64_t task); }
namespace pvSel { void OnPvDbRead(uint64_t task); }

namespace shared_hooks {

HOOK_DEFINE_TRAMPOLINE(TaskPvDBCtrlSharedHook) {
    static bool Callback(uint64_t task) {
        bool ret = Orig(task);


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

    if (Config::enableNewClassics || Config::enableFtUi) {
        TaskPvDBCtrlSharedHook::InstallAtOffset(0x004c0ff0);
    }
}

} // namespace shared_hooks
