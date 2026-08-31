#pragma once

void InstallDSCHooks();

namespace dsc {
    inline void init() {
         InstallDSCHooks();
    }
}
