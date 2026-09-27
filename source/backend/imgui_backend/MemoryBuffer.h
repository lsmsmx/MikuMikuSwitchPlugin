#pragma once

#include <cstddef>
#include <cstdint>
#include <nvn/nvn_Cpp.h>

class MemoryBuffer {
public:
    explicit MemoryBuffer(size_t size);
    explicit MemoryBuffer(size_t size, nvn::MemoryPoolFlags flags);
    explicit MemoryBuffer(size_t size, void *buffer, nvn::MemoryPoolFlags flags);

    void Finalize();

    size_t GetPoolSize() const { return pool.GetSize(); }
    nvn::BufferAddress GetBufferAddress() const { return buffer.GetAddress(); };
    uint8_t *GetMemPtr() const { return (uint8_t *) pool.Map(); }
    bool IsBufferReady() { return mIsReady; }

    void ClearBuffer();
    void FlushRange(size_t offset, size_t size) const;
    void InvalidateRange(size_t offset, size_t size) const;

    operator nvn::BufferAddress() const { return buffer.GetAddress(); }

private:
    nvn::MemoryPool pool;
    nvn::Buffer buffer;

    void *memBuffer = nullptr;
    void *rawBuffer = nullptr;
    bool ownsBuffer = false;
    bool mIsReady = false;
};