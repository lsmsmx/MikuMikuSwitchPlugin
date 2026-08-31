#pragma once

#include <stdint.h>

void InstallTargetHooks();

namespace target {
    inline void init() {
        InstallTargetHooks();
    }
}
