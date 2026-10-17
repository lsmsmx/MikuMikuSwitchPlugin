#pragma once
#include <cstdint>
#include <cstddef>

namespace PvLoader {
    void init();
    bool IsMoviePv(uint32_t pvId);
    uint32_t pvLoaderParseStartImp(const char* data, size_t length);
}
