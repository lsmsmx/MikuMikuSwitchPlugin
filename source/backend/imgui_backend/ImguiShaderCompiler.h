#pragma once

#include <cstdint>
#include <nvn/nvn_Cpp.h>

struct CompiledData {
    uint8_t *ptr;
    unsigned long size;
};

struct BinaryHeader {
    BinaryHeader(uint32_t* header) {
        mFragmentControlOffset = header[0];
        mVertexControlOffset   = header[1];
        mFragmentDataOffset    = header[2];
        mVertexDataOffset      = header[3];
    }

    uint32_t mVertexControlOffset;
    uint32_t mVertexDataOffset;
    uint32_t mFragmentControlOffset;
    uint32_t mFragmentDataOffset;
};

namespace ImguiShaderCompiler {
    CompiledData CompileShader(const char *shaderName);
}