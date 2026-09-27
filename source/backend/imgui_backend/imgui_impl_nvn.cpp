#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>

#include "imgui_impl_nvn.hpp"
#include "MemoryPoolMaker.h"
#include "imgui_shader_bin.h"
#include "lib/diag/assert.hpp"

#define UBOSIZE 0x1000
#define ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((a) - 1))

typedef float Matrix44f[4][4];

static void BuildOrthoMatrix(Matrix44f out, const ImVec2 &display_pos, const ImVec2 &display_size) {
    const float L = display_pos.x;
    const float R = display_pos.x + display_size.x;
    const float T = display_pos.y;
    const float B = display_pos.y + display_size.y;

    out[0][0] = 2.0f / (R - L);
    out[0][1] = 0.0f;
    out[0][2] = 0.0f;
    out[0][3] = 0.0f;

    out[1][0] = 0.0f;
    out[1][1] = 2.0f / (T - B);
    out[1][2] = 0.0f;
    out[1][3] = 0.0f;

    out[2][0] = 0.0f;
    out[2][1] = 0.0f;
    out[2][2] = -0.5f;
    out[2][3] = 0.0f;

    out[3][0] = (R + L) / (L - R);
    out[3][1] = (T + B) / (B - T);
    out[3][2] = 0.5f;
    out[3][3] = 1.0f;
}

namespace ImguiNvnBackend {

    // Generous command memory bounds: 1 MB Command Memory + 256 KB Control Memory
    // Prevents GPU buffer exhaustion even with hundreds of active ImGui widgets
    constexpr size_t kCmdMemSize  = 1024u * 1024u; // 1 MB
    constexpr size_t kCtrlMemSize = 256u * 1024u;  // 256 KB

    namespace {
        const nvn::Texture *g_frame_color_target = nullptr;
    }

    void SetRenderTarget(const nvn::Texture *colorTarget) {
        g_frame_color_target = colorTarget;
    }

    NvnBackendData *getBackendData() {
        NvnBackendData *result = ImGui::GetCurrentContext() ? (NvnBackendData *) ImGui::GetIO().BackendRendererUserData
                                                            : nullptr;
        EXL_ASSERT(result, "Backend has not been initialized!");
        return result;
    }

    bool createShaders() {
        auto bd = getBackendData();
        bd->imguiShaderBinary.size = static_cast<unsigned long>(imgui_bin_len);
        bd->imguiShaderBinary.ptr  = const_cast<uint8_t*>(imgui_bin);
        return true;
    }

    bool setupShaders(uint8_t *shaderBinary, unsigned long binarySize) {
        auto bd = getBackendData();

        if (!bd->shaderProgram.Initialize(bd->device)) return false;

        bd->shaderMemory = IM_NEW(MemoryBuffer)(binarySize, nvn::MemoryPoolFlags::CPU_UNCACHED |
                                                              nvn::MemoryPoolFlags::GPU_CACHED |
                                                              nvn::MemoryPoolFlags::SHADER_CODE);

        if (!bd->shaderMemory->IsBufferReady()) return false;

        memcpy(bd->shaderMemory->GetMemPtr(), shaderBinary, binarySize);
        BinaryHeader offsetData = BinaryHeader((uint32_t *) shaderBinary);

        nvn::BufferAddress addr = bd->shaderMemory->GetBufferAddress();

        nvn::ShaderData &vertShaderData = bd->shaderDatas[0];
        vertShaderData.data = addr + offsetData.mVertexDataOffset;
        vertShaderData.control = shaderBinary + offsetData.mVertexControlOffset;

        nvn::ShaderData &fragShaderData = bd->shaderDatas[1];
        fragShaderData.data = addr + offsetData.mFragmentDataOffset;
        fragShaderData.control = shaderBinary + offsetData.mFragmentControlOffset;

        if (!bd->shaderProgram.SetShaders(2, bd->shaderDatas)) return false;

        bd->shaderProgram.SetDebugLabel("ImGuiShader");

        for (int i = 0; i < NvnBackendData::FramesInFlight; ++i) {
            bd->uniformMemory[i] = IM_NEW(MemoryBuffer)(UBOSIZE);
            if (!bd->uniformMemory[i] || !bd->uniformMemory[i]->IsBufferReady()) return false;
        }

        bd->attribStates[0].SetDefaults().SetFormat(nvn::Format::RG32F, offsetof(ImDrawVert, pos));
        bd->attribStates[1].SetDefaults().SetFormat(nvn::Format::RG32F, offsetof(ImDrawVert, uv));
        bd->attribStates[2].SetDefaults().SetFormat(nvn::Format::RGBA8, offsetof(ImDrawVert, col));

        bd->streamState.SetDefaults().SetStride(sizeof(ImDrawVert));

        return true;
    }

    bool createFontTexture() {
        auto bd = getBackendData();
        ImGuiIO &io = ImGui::GetIO();

        unsigned char *pixels = nullptr;
        int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

        int sampler_desc_size = 0, texture_desc_size = 0;
        bd->device->GetInteger(nvn::DeviceInfo::SAMPLER_DESCRIPTOR_SIZE, &sampler_desc_size);
        bd->device->GetInteger(nvn::DeviceInfo::TEXTURE_DESCRIPTOR_SIZE, &texture_desc_size);

        size_t total_pool_size = ALIGN_UP(sampler_desc_size * 16 + texture_desc_size * 16, 0x1000);
        if (!MemoryPoolMaker::createPool(&bd->sampTexMemPool, total_pool_size)) return false;

        if (!bd->samplerPool.Initialize(&bd->sampTexMemPool, 0, 16)) return false;
        if (!bd->texPool.Initialize(&bd->sampTexMemPool, sampler_desc_size * 16, 16)) return false;

        nvn::TextureBuilder tex_builder;
        tex_builder.SetDefaults()
            .SetDevice(bd->device)
            .SetTarget(nvn::TextureTarget::TARGET_2D)
            .SetFormat(nvn::Format::RGBA8)
            .SetSize2D(width, height);

        size_t storage_size = tex_builder.GetStorageSize();
        size_t storage_align = tex_builder.GetStorageAlignment();

        if (!MemoryPoolMaker::createPoolAligned(&bd->fontTexMemPool, storage_size, storage_align,
                                                nvn::MemoryPoolFlags::CPU_UNCACHED | nvn::MemoryPoolFlags::GPU_CACHED)) {
            return false;
        }

        tex_builder.SetStorage(&bd->fontTexMemPool, 0);
        if (!bd->fontTexture.Initialize(&tex_builder)) return false;

        nvn::CopyRegion region = { 0, 0, 0, width, height, 1 };
        bd->fontTexture.WriteTexelsStrided(nullptr, &region, pixels, width * 4, 0);
        bd->fontTexture.FlushTexels(nullptr, &region);

        nvn::SamplerBuilder sampler_builder;
        sampler_builder.SetDefaults()
            .SetDevice(bd->device)
            .SetMinMagFilter(nvn::MinFilter::LINEAR, nvn::MagFilter::LINEAR)
            .SetWrapMode(nvn::WrapMode::CLAMP, nvn::WrapMode::CLAMP, nvn::WrapMode::CLAMP);

        if (!bd->fontSampler.Initialize(&sampler_builder)) return false;

        bd->texPool.RegisterTexture(1, &bd->fontTexture, nullptr);
        bd->samplerPool.RegisterSampler(1, &bd->fontSampler);

        bd->fontTexHandle = bd->device->GetTextureHandle(1, 1);
        io.Fonts->SetTexID((ImTextureID)(uintptr_t)bd->fontTexHandle);

        return true;
    }

    void InitBackend(const NvnBackendInitInfo &initInfo) {
        ImGuiIO &io = ImGui::GetIO();
        EXL_ASSERT(!io.BackendRendererUserData, "Already Initialized Imgui Backend!");

        io.BackendPlatformName = "Switch";
        io.BackendRendererName = "imgui_impl_nvn";
        io.IniFilename = nullptr;
        io.MouseDrawCursor = true;
        io.DisplaySize = ImVec2(1280, 720);

        auto *bd = IM_NEW(NvnBackendData)();
        EXL_ASSERT(bd, "Backend was not Created!");

        io.BackendRendererUserData = (void *) bd;

        bd->device = initInfo.device;
        bd->queue = initInfo.queue;
        bd->viewportSize = io.DisplaySize;
        bd->isInitialized = false;

        for (int i = 0; i < NvnBackendData::FramesInFlight; ++i) {
            MemoryPoolMaker::createPool(&bd->cmdMemPool[i], kCmdMemSize,
                                        nvn::MemoryPoolFlags::CPU_UNCACHED | nvn::MemoryPoolFlags::GPU_CACHED);
            bd->cmdBuf[i].Initialize(bd->device);
            bd->cmdBuf[i].AddCommandMemory(&bd->cmdMemPool[i], 0, kCmdMemSize);

            bd->controlMemory[i] = std::malloc(kCtrlMemSize);
            bd->cmdBuf[i].AddControlMemory(bd->controlMemory[i], kCtrlMemSize);
        }

        constexpr size_t kInitialVtxSize = 1024u * 1024u;
        constexpr size_t kInitialIdxSize = 512u * 1024u;

        for (int i = 0; i < NvnBackendData::FramesInFlight; ++i) {
            bd->vtxBuffer[i] = IM_NEW(MemoryBuffer)(kInitialVtxSize);
            bd->idxBuffer[i] = IM_NEW(MemoryBuffer)(kInitialIdxSize);
        }

        if (!createShaders()) return;
        if (!setupShaders(bd->imguiShaderBinary.ptr, bd->imguiShaderBinary.size)) return;
        if (!createFontTexture()) return;

        bd->isInitialized = true;
    }

    void ShutdownBackend() {
        auto bd = getBackendData();
        if (bd) {
            for (int i = 0; i < NvnBackendData::FramesInFlight; ++i) {
                bd->cmdBuf[i].Finalize();
                MemoryPoolMaker::destroyPool(&bd->cmdMemPool[i]);
                if (bd->controlMemory[i]) std::free(bd->controlMemory[i]);
            }
            bd->samplerPool.Finalize();
            bd->texPool.Finalize();
            MemoryPoolMaker::destroyPool(&bd->sampTexMemPool);
            MemoryPoolMaker::destroyPool(&bd->fontTexMemPool);
            bd->isInitialized = false;
        }
    }

    void newFrame() {
        ImGuiIO &io = ImGui::GetIO();
        auto bd = getBackendData();

        io.DisplaySize = bd->viewportSize;
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
        io.DeltaTime = 1.0f / 60.0f;
    }

    void setRenderStates(nvn::CommandBuffer* cmdBuf) {
        auto bd = getBackendData();

        nvn::PolygonState polyState;
        polyState.SetDefaults();
        polyState.SetPolygonMode(nvn::PolygonMode::FILL);
        polyState.SetCullFace(nvn::Face::NONE);
        polyState.SetFrontFace(nvn::FrontFace::CCW);
        cmdBuf->BindPolygonState(&polyState);

        nvn::ColorState colorState;
        colorState.SetDefaults();
        colorState.SetLogicOp(nvn::LogicOp::COPY);
        colorState.SetAlphaTest(nvn::AlphaFunc::ALWAYS);
        for (int i = 0; i < 8; ++i) {
            colorState.SetBlendEnable(i, true);
        }
        cmdBuf->BindColorState(&colorState);

        nvn::BlendState blendState;
        blendState.SetDefaults();
        blendState.SetBlendFunc(nvn::BlendFunc::SRC_ALPHA, nvn::BlendFunc::ONE_MINUS_SRC_ALPHA, nvn::BlendFunc::ONE,
                                nvn::BlendFunc::ZERO);
        blendState.SetBlendEquation(nvn::BlendEquation::ADD, nvn::BlendEquation::ADD);
        cmdBuf->BindBlendState(&blendState);

        nvn::DepthStencilState depthStencil;
        depthStencil.SetDefaults();
        depthStencil.SetDepthTestEnable(0);
        depthStencil.SetDepthWriteEnable(0);
        depthStencil.SetStencilTestEnable(0);
        cmdBuf->BindDepthStencilState(&depthStencil);

        cmdBuf->BindVertexAttribState(3, bd->attribStates);
        cmdBuf->BindVertexStreamState(1, &bd->streamState);

        cmdBuf->SetTexturePool(&bd->texPool);
        cmdBuf->SetSamplerPool(&bd->samplerPool);
    }

    void renderDrawData(ImDrawData *drawData) {
        if (!drawData->Valid || drawData->CmdListsCount == 0) return;

        auto bd = getBackendData();
        if (!bd || !bd->isInitialized) return;

        const int frame_index = bd->frame_index % NvnBackendData::FramesInFlight;
        bd->frame_index++;

        nvn::CommandBuffer *cmdBuf = &bd->cmdBuf[frame_index];
        MemoryBuffer *vtx_buffer   = bd->vtxBuffer[frame_index];
        MemoryBuffer *idx_buffer   = bd->idxBuffer[frame_index];
        MemoryBuffer *ubo_buffer   = bd->uniformMemory[frame_index];

        if (!vtx_buffer || !vtx_buffer->IsBufferReady() ||
            !idx_buffer || !idx_buffer->IsBufferReady() ||
            !ubo_buffer || !ubo_buffer->IsBufferReady()) {
            return;
        }

        size_t totalVtxSize = drawData->TotalVtxCount * sizeof(ImDrawVert);
        size_t totalIdxSize = drawData->TotalIdxCount * sizeof(ImDrawIdx);

        if (totalVtxSize > vtx_buffer->GetPoolSize() || totalIdxSize > idx_buffer->GetPoolSize()) {
            return;
        }

        // Reset command and control memory offsets so memory NEVER runs out after 10 seconds
        cmdBuf->AddCommandMemory(&bd->cmdMemPool[frame_index], 0, kCmdMemSize);
        cmdBuf->AddControlMemory(bd->controlMemory[frame_index], kCtrlMemSize);

        cmdBuf->BeginRecording();
        cmdBuf->Barrier(nvn::BarrierBits::ORDER_FRAGMENTS | nvn::BarrierBits::INVALIDATE_TEXTURE);

        if (g_frame_color_target) {
            const nvn::Texture *colors[1] = {g_frame_color_target};
            cmdBuf->SetRenderTargets(1, colors, nullptr, nullptr, nullptr);
        }

        cmdBuf->BindProgram(&bd->shaderProgram, nvn::ShaderStageBits::VERTEX | nvn::ShaderStageBits::FRAGMENT);

        cmdBuf->BindUniformBuffer(nvn::ShaderStage::VERTEX, 0, *ubo_buffer, UBOSIZE);
        Matrix44f proj_matrix{};
        BuildOrthoMatrix(proj_matrix, drawData->DisplayPos, drawData->DisplaySize);
        cmdBuf->UpdateUniformBuffer(*ubo_buffer, UBOSIZE, 0, sizeof(proj_matrix), &proj_matrix);

        setRenderStates(cmdBuf);

        size_t vtxOffset = 0, idxOffset = 0;
        nvn::TextureHandle boundTextureHandle = 0;
        bool has_bound_texture = false;

        const ImVec2 clip_off = drawData->DisplayPos;
        const ImVec2 clip_scale = drawData->FramebufferScale;
        const ImVec2 vp = bd->viewportSize;
        cmdBuf->SetViewport(0, 0, vp.x, vp.y);
        cmdBuf->SetScissor(0, 0, vp.x, vp.y);

        for (int i = 0; i < drawData->CmdListsCount; i++) {
            auto cmdList = drawData->CmdLists[i];

            size_t vtxSize = cmdList->VtxBuffer.Size * sizeof(ImDrawVert);
            size_t idxSize = cmdList->IdxBuffer.Size * sizeof(ImDrawIdx);

            cmdBuf->BindVertexBuffer(0, (*vtx_buffer) + vtxOffset, vtxSize);

            memcpy(vtx_buffer->GetMemPtr() + vtxOffset, cmdList->VtxBuffer.Data, vtxSize);
            memcpy(idx_buffer->GetMemPtr() + idxOffset, cmdList->IdxBuffer.Data, idxSize);
            vtx_buffer->FlushRange(vtxOffset, vtxSize);
            idx_buffer->FlushRange(idxOffset, idxSize);

            for (const ImDrawCmd &cmd: cmdList->CmdBuffer) {
                if (cmd.UserCallback != nullptr) {
                    if (cmd.UserCallback == ImDrawCallback_ResetRenderState) {
                        setRenderStates(cmdBuf);
                    } else {
                        cmd.UserCallback(cmdList, const_cast<ImDrawCmd *>(&cmd));
                    }
                    continue;
                }

                ImVec4 clip_rect;
                clip_rect.x = (cmd.ClipRect.x - clip_off.x) * clip_scale.x;
                clip_rect.y = (cmd.ClipRect.y - clip_off.y) * clip_scale.y;
                clip_rect.z = (cmd.ClipRect.z - clip_off.x) * clip_scale.x;
                clip_rect.w = (cmd.ClipRect.w - clip_off.y) * clip_scale.y;

                if (clip_rect.x >= vp.x || clip_rect.y >= vp.y || clip_rect.z <= 0.0f || clip_rect.w <= 0.0f) {
                    continue;
                }

                int scissor_x = std::max(0, static_cast<int>(std::floor(clip_rect.x)));
                int scissor_y = std::max(0, static_cast<int>(std::floor(clip_rect.y)));
                int scissor_z = std::min(static_cast<int>(vp.x), static_cast<int>(std::ceil(clip_rect.z)));
                int scissor_w = std::min(static_cast<int>(vp.y), static_cast<int>(std::ceil(clip_rect.w)));

                int scissor_w_px = std::max(0, scissor_z - scissor_x);
                int scissor_h_px = std::max(0, scissor_w - scissor_y);

                if (scissor_x + scissor_w_px > (int)vp.x) scissor_w_px = (int)vp.x - scissor_x;
                if (scissor_y + scissor_h_px > (int)vp.y) scissor_h_px = (int)vp.y - scissor_y;

                // Skip empty draw calls to prevent GPU assembler hangs
                if (cmd.ElemCount == 0) continue;

                if (scissor_w_px <= 0 || scissor_h_px <= 0) continue;

                cmdBuf->SetScissor(scissor_x, scissor_y, scissor_w_px, scissor_h_px);

                const nvn::TextureHandle tex_handle = (cmd.TextureId != nullptr)
                    ? (nvn::TextureHandle)(uintptr_t)cmd.TextureId
                    : bd->fontTexHandle;

                if (!has_bound_texture || boundTextureHandle != tex_handle) {
                    boundTextureHandle = tex_handle;
                    has_bound_texture = true;
                    cmdBuf->BindTexture(nvn::ShaderStage::FRAGMENT, 0, tex_handle);
                }

                cmdBuf->DrawElementsBaseVertex(nvn::DrawPrimitive::TRIANGLES,
                                               nvn::IndexType::UNSIGNED_SHORT, cmd.ElemCount,
                                               (*idx_buffer) + (cmd.IdxOffset * sizeof(ImDrawIdx)) + idxOffset,
                                               cmd.VtxOffset);
            }

            vtxOffset += vtxSize;
            idxOffset += idxSize;
        }

        cmdBuf->SetScissor(0, 0, vp.x, vp.y);

        auto handle = cmdBuf->EndRecording();

        // CRITICAL FIX: Never submit a NULL command handle to the GPU queue (Prevents FPS: 0.0 Freeze)
        if (handle != 0) {
            bd->queue->SubmitCommands(1, &handle);
        }
    }
}
