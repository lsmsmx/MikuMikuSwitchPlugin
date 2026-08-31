#pragma once
#include <cstdint>
#include <cstddef>

namespace PvLoader {
    void init();
    uint32_t pvLoaderParseStartImp(const char* data, size_t length);
}
