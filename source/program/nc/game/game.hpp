#pragma once

void InstallGameHooks();

namespace game {
     inline void init() {
          InstallGameHooks();
     }
}
