#pragma once
#include <cstdint>
#include <vector>

namespace MemoryScannerUi {

    struct MemoryGap {
        uintptr_t startOffset;
        uintptr_t endOffset;
        size_t sizeBytes;
        float sizeMB;
    };

    extern bool g_showWindow;

    // Start background scanner thread
    void Init();

    // Render transparent frameless window in top-right
    void DrawWindow();

} // namespace MemoryScannerUi
