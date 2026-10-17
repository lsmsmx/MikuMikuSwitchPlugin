#pragma once
#include <cstdint>
#include <cstddef>

namespace SongQuickJump {
    void Draw();
    void ParsePvDbBuffer(const char* data, size_t length);
    void SetPvSelActive(bool active, void* sel = nullptr);
    bool IsVisible();
}
