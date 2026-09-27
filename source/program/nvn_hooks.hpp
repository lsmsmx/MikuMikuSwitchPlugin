#pragma once

#include <nvn/nvn.h>

namespace nvn {
    class Texture;
}

namespace nvn_hooks {
    using PresentCallback = void (*)(NVNqueue *queue, NVNwindow *window, int texture_index);

    auto InstallHooks(PresentCallback on_present) -> bool;
    auto LoadCppProcsOnce() -> void;

    auto GetDevice() -> NVNdevice *;
    auto GetQueue() -> NVNqueue *;
    auto GetCommandBuffer() -> NVNcommandBuffer *;
    auto GetDeviceGetProcAddress() -> PFNNVNDEVICEGETPROCADDRESSPROC;

    auto GetCropWidth() -> int;
    auto GetCropHeight() -> int;

    auto GetSwapchainTextureCount() -> int;
    auto GetSwapchainTextureForBackend(int index) -> const nvn::Texture *;
    auto GetSwapchainTextureSize(int index, int *out_w, int *out_h) -> bool;
}