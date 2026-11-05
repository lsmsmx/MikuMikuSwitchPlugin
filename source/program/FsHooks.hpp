#pragma once

#include <cstdint>

namespace FsHooks {
    void Init();

    // Address of `sym` as any loaded module defines it, or 0 when nothing does.
    // Callers that want a function the plugin does not otherwise import look it
    // up through this instead of calling it directly: an import no module
    // exports keeps the whole plugin from loading.
    uintptr_t FindSymbol(const char* sym);
}