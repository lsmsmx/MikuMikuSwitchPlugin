#pragma once
void InstallModHooks();

namespace mod_nc {
    inline void init() {
        InstallModHooks();
    }
}
