#pragma once

#include <cstddef>
#include <nvn/nvn_Cpp.h>

namespace MemoryPoolMaker {
    bool createPool(nvn::MemoryPool *result, size_t size,
                    const nvn::MemoryPoolFlags &flags = nvn::MemoryPoolFlags::CPU_UNCACHED |
                                                        nvn::MemoryPoolFlags::GPU_CACHED);
    bool createPoolAligned(nvn::MemoryPool *result, size_t size, size_t min_alignment,
                           const nvn::MemoryPoolFlags &flags = nvn::MemoryPoolFlags::CPU_UNCACHED |
                                                               nvn::MemoryPoolFlags::GPU_CACHED);
    void destroyPool(nvn::MemoryPool *pool);
}