#include "MemoryPoolMaker.h"
#include "imgui_impl_nvn.hpp"
#include <vector>

#define ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((a) - 1))

namespace {
    size_t GetDevicePageSizeOrFallback(const nvn::Device *device) {
        int page_size = 0;
        if (device != nullptr) {
            device->GetInteger(nvn::DeviceInfo::MEMORY_POOL_PAGE_SIZE, &page_size);
        }
        const size_t page_size_u = page_size > 0 ? static_cast<size_t>(page_size) : 0;
        const bool is_power_of_two = page_size_u != 0 && ((page_size_u & (page_size_u - 1)) == 0);
        return is_power_of_two ? page_size_u : 0x1000;
    }

    size_t GetShaderCodePaddingOrZero(const nvn::Device *device) {
        int pad = 0;
        if (device != nullptr) {
            device->GetInteger(nvn::DeviceInfo::SHADER_CODE_MEMORY_POOL_PADDING_SIZE, &pad);
        }
        return pad > 0 ? static_cast<size_t>(pad) : 0;
    }

    struct PoolAllocation {
        nvn::MemoryPool *pool = nullptr;
        void *raw_ptr = nullptr;
    };

    std::vector<PoolAllocation> g_pool_allocations;

    void RegisterPoolAllocation(nvn::MemoryPool *pool, void *raw_ptr) {
        g_pool_allocations.push_back({pool, raw_ptr});
    }

    void *UnregisterPoolAllocation(nvn::MemoryPool *pool) {
        for (size_t i = 0; i < g_pool_allocations.size(); ++i) {
            if (g_pool_allocations[i].pool == pool) {
                void *raw_ptr = g_pool_allocations[i].raw_ptr;
                g_pool_allocations[i] = g_pool_allocations.back();
                g_pool_allocations.pop_back();
                return raw_ptr;
            }
        }
        return nullptr;
    }

    size_t ChoosePoolAlignment(const nvn::Device *device, size_t min_alignment) {
        const size_t page_alignment = GetDevicePageSizeOrFallback(device);
        size_t alignment = page_alignment;
        if (min_alignment > alignment) {
            alignment = min_alignment;
        }
        return alignment;
    }
}

bool MemoryPoolMaker::createPool(nvn::MemoryPool *result, size_t size,
                                 const nvn::MemoryPoolFlags &flags) {
    return createPoolAligned(result, size, 0, flags);
}

bool MemoryPoolMaker::createPoolAligned(nvn::MemoryPool *result, size_t size, size_t min_alignment,
                                        const nvn::MemoryPoolFlags &flags) {

    auto bd = ImguiNvnBackend::getBackendData();

    const size_t pool_alignment = ChoosePoolAlignment(bd->device, min_alignment);
    const bool is_shader_code = (int(flags) & int(nvn::MemoryPoolFlags::SHADER_CODE)) != 0;
    const size_t shader_pad = is_shader_code ? GetShaderCodePaddingOrZero(bd->device) : 0;

    const size_t aligned_size = ALIGN_UP(size + shader_pad, pool_alignment);
    void *rawPtr = IM_ALLOC(aligned_size + (pool_alignment - 1));
    if (rawPtr == nullptr) {
        return false;
    }

    void *poolPtr = reinterpret_cast<void *>(ALIGN_UP(reinterpret_cast<uintptr_t>(rawPtr), pool_alignment));

    nvn::MemoryPoolBuilder memPoolBuilder{};
    memPoolBuilder.SetDefaults().SetDevice(bd->device).SetFlags(flags).SetStorage(poolPtr, aligned_size);

    if (!result->Initialize(&memPoolBuilder)) {
        IM_FREE(rawPtr);
        return false;
    }

    RegisterPoolAllocation(result, rawPtr);
    return true;
}

void MemoryPoolMaker::destroyPool(nvn::MemoryPool *pool) {
    if (pool == nullptr) {
        return;
    }
    pool->Finalize();
    void *raw_ptr = UnregisterPoolAllocation(pool);
    if (raw_ptr != nullptr) {
        IM_FREE(raw_ptr);
    }
}