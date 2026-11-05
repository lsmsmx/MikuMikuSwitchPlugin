#pragma once

#include <cstddef>
#include <cstdint>

namespace UsmSupport {

    // Signatures of the nn::fs entry points the virtual movie files are served
    // from, matching the SDK (not the plugin's libnx headers): FileHandle is an
    // 8-byte struct passed by value in one register, ReadOption is a reference.
    //
    // FsHooks owns the trampolines -- one per address, on every module that
    // defines the symbol -- and passes its own original in as `orig`.  These
    // functions return the Result the engine expects.
    using VOpenFileFn  = uint32_t (*)(void*, const char*, int);
    using VEntryTypeFn = uint32_t (*)(void*, const char*);
    using VGetSizeFn   = uint32_t (*)(int64_t*, uint64_t);
    using VReadFn5     = uint32_t (*)(uint64_t, int64_t, void*, size_t, const void*);
    using VReadFn6     = uint32_t (*)(size_t*, uint64_t, int64_t, void*, size_t);
    using VCloseFn     = void     (*)(uint64_t);
    using VFlushFn     = uint32_t (*)(uint64_t);

    // The plugin's own file access: the originals FsHooks saved for the SDK
    // functions it hooks.  FsHooks hands them over once, right after it installs
    // them; everything in UsmSupport that opens or measures a file goes through
    // these and never back through a hook.  The note on SetRawFs' definition
    // says why that is not just a matter of speed.
    struct RawFs {
        VOpenFileFn open;
        VGetSizeFn  getSize;
        VCloseFn    close;
    };
    void SetRawFs(RawFs const& fs);

    uint32_t vfileOpenFileImpl(VOpenFileFn orig, void* outHandle,
                               const char* path, int mode);
    uint32_t vfileGetEntryTypeImpl(VEntryTypeFn orig, void* outType,
                                   const char* path);
    uint32_t vfileGetFileSizeImpl(VGetSizeFn orig, int64_t* out, uint64_t handle);
    uint32_t vfileReadImpl(VReadFn5 orig, uint64_t handle, int64_t pos,
                           void* buf, size_t size, const void* opt);
    uint32_t vfileReadImpl6(VReadFn6 orig, size_t* bytesRead, uint64_t handle,
                            int64_t pos, void* buf, size_t size);
    void     vfileCloseImpl(VCloseFn orig, uint64_t handle);
    uint32_t vfileFlushImpl(VFlushFn orig, uint64_t handle);

    // Resolver, movie-gate and multimedia hooks.  Call from nnMain callback.
    void init();

} // namespace UsmSupport
