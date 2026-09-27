#pragma once

#include "ImguiShaderCompiler.h"
#include "imgui/imgui.h"
#include "nvn/nvn_Cpp.h"
#include "nvn/nvn_CppMethods.h"
#include "MemoryBuffer.h"

namespace ImguiNvnBackend {

    struct NvnBackendInitInfo {
        nvn::Device *device;
        nvn::Queue *queue;
    };

    struct NvnBackendData {
        nvn::Device *device;
        nvn::Queue *queue;

        static constexpr int FramesInFlight = 3;
        int frame_index = 0;

        // Dedicated independent command buffers for ImGui (Prevents game buffer overflow)
        nvn::CommandBuffer cmdBuf[FramesInFlight];
        nvn::MemoryPool cmdMemPool[FramesInFlight];
        void* controlMemory[FramesInFlight];

        nvn::BufferBuilder bufferBuilder;
        nvn::MemoryPoolBuilder memPoolBuilder;

        nvn::Program shaderProgram;
        MemoryBuffer *shaderMemory;

        MemoryBuffer *uniformMemory[FramesInFlight];
        nvn::ShaderData shaderDatas[2];

        nvn::VertexStreamState streamState;
        nvn::VertexAttribState attribStates[3];

        nvn::Texture fontTexture;
        nvn::Sampler fontSampler;
        nvn::MemoryPool fontTexMemPool;
        nvn::MemoryPool sampTexMemPool;
        nvn::TexturePool texPool;
        nvn::SamplerPool samplerPool;
        nvn::TextureHandle fontTexHandle;

        MemoryBuffer *vtxBuffer[FramesInFlight];
        MemoryBuffer *idxBuffer[FramesInFlight];
        ImVec2 viewportSize;

        int64_t lastTick;
        bool isInitialized;
        bool isDisableInput = true;

        CompiledData imguiShaderBinary;
    };

    bool createShaders();
    bool setupShaders(uint8_t *shaderBinary, unsigned long binarySize);
    bool createFontTexture();

    void InitBackend(const NvnBackendInitInfo &initInfo);
    void ShutdownBackend();
    void newFrame();
    void setRenderStates(nvn::CommandBuffer* cmdBuf);
    void renderDrawData(ImDrawData *drawData);

    void SetRenderTarget(const nvn::Texture *colorTarget);
    NvnBackendData *getBackendData();
}
