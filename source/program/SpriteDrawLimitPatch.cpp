#include "lib.hpp"
#include "Types.hpp"
#include "macros.hpp"
#include "Allocator.hpp"
#include <cstring>
#include "SpriteDrawLimitPatch.hpp"

namespace spr {
    struct SpriteVertex {
        float pos[3];
        float uv[2];
        uint32_t color;
    };

    static_assert(sizeof(spr::SpriteVertex) == 0x18, "SpriteVertex size mismatch");

    struct SpriteVertexData {
        size_t max_count;
        spr::SpriteVertex* array;
    };
}

// 1. Dynamic global vertex buffer
static spr::SpriteVertexData sprite_vertex_data = { 0x2000, nullptr };

// 2. Game BSS vertex array counter reference
#define ADDR_SPRITE_VERTEX_ARRAY_COUNT 0x00BE3840E0

inline size_t& GetSpriteVertexArrayCount() {
    uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
    return *reinterpret_cast<size_t*>(base + ADDR_SPRITE_VERTEX_ARRAY_COUNT);
}

inline uintptr_t GetOriginalSp(exl::hook::nx64::InlineCtx* ctx) {
    return reinterpret_cast<uintptr_t>(ctx) + 0x100;
}

// ============================================================================
// MEMORY MANAGEMENT FUNCTIONS
// ============================================================================

HOOK_DEFINE_TRAMPOLINE(SprArgsSetVertexArray) {
    static void Callback(uintptr_t This, spr::SpriteVertex* vertex_array, size_t num_vertex) {
        if (!sprite_vertex_data.array) {
            sprite_vertex_data.array = (spr::SpriteVertex*)GameOperatorNew(
                sizeof(spr::SpriteVertex) * sprite_vertex_data.max_count);
        }

        size_t& count = GetSpriteVertexArrayCount();

        if (count + num_vertex >= sprite_vertex_data.max_count) {
            while (count + num_vertex >= sprite_vertex_data.max_count)
                sprite_vertex_data.max_count *= 2;

            spr::SpriteVertex* new_array = (spr::SpriteVertex*)GameOperatorNew(
                sizeof(spr::SpriteVertex) * sprite_vertex_data.max_count);
            std::memmove(new_array, sprite_vertex_data.array, sizeof(spr::SpriteVertex) * count);
            GameOperatorDelete(sprite_vertex_data.array);
            sprite_vertex_data.array = new_array;
        }

        *reinterpret_cast<size_t*>(This + 0x100) = num_vertex;
        *reinterpret_cast<size_t*>(This + 0xF8) = count; // Store vertex offset/index

        void* dst = sprite_vertex_data.array + count;
        size_t size_to_copy = sizeof(spr::SpriteVertex) * num_vertex;
        std::memmove(dst, vertex_array, size_to_copy);

        count += num_vertex;
    }
};

HOOK_DEFINE_TRAMPOLINE(SprArgsReserveVertexArray) {
    static void Callback(uintptr_t This, int32_t num_vertex) {
        if (!sprite_vertex_data.array) {
            sprite_vertex_data.array = (spr::SpriteVertex*)GameOperatorNew(
                sizeof(spr::SpriteVertex) * sprite_vertex_data.max_count);
        }

        size_t& count = GetSpriteVertexArrayCount();

        if (count + num_vertex >= sprite_vertex_data.max_count) {
            while (count + num_vertex >= sprite_vertex_data.max_count)
                sprite_vertex_data.max_count *= 2;

            spr::SpriteVertex* new_array = (spr::SpriteVertex*)GameOperatorNew(
                sizeof(spr::SpriteVertex) * sprite_vertex_data.max_count);
            std::memmove(new_array, sprite_vertex_data.array, sizeof(spr::SpriteVertex) * count);
            GameOperatorDelete(sprite_vertex_data.array);
            sprite_vertex_data.array = new_array;
        }

        *reinterpret_cast<size_t*>(This + 0xF8) = count; // Store vertex offset/index
        count += num_vertex;
    }
};

HOOK_DEFINE_TRAMPOLINE(SpriteManagerClear) {
    static void Callback(void *mgr) {
        if (sprite_vertex_data.array) {
            GameOperatorDelete(sprite_vertex_data.array);
            sprite_vertex_data.array = nullptr;
        }
        Orig(mgr);
    }
};

// ============================================================================
// MID HOOKS (Convert index to pointer)
// ============================================================================

HOOK_DEFINE_INLINE(SprArgsPutSpriteLineListMid) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        // Read index from game SP (+0x100)
        uintptr_t original_sp = GetOriginalSp(ctx);
        size_t index = *reinterpret_cast<size_t*>(original_sp + 0x100);
        ctx->X[8] = sprite_vertex_data.array ? reinterpret_cast<uintptr_t>(sprite_vertex_data.array + index) : 0;
    }
};

HOOK_DEFINE_INLINE(SprDrawSpriteScaleMid) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        size_t index = *reinterpret_cast<size_t*>(ctx->X[22] + 0xF8);
        ctx->X[9] = reinterpret_cast<uintptr_t>(sprite_vertex_data.array + index);
    }
};

HOOK_DEFINE_INLINE(SprCalcSpriteTextureParamMid1) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        size_t index = *reinterpret_cast<size_t*>(ctx->X[23] + 0xF8);
        ctx->X[10] = reinterpret_cast<uintptr_t>(sprite_vertex_data.array + index);
    }
};

HOOK_DEFINE_INLINE(SprCalcSpriteTextureParamMid2) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        size_t index = *reinterpret_cast<size_t*>(ctx->X[23] + 0xF8);
        ctx->X[10] = reinterpret_cast<uintptr_t>(sprite_vertex_data.array + index);
    }
};

HOOK_DEFINE_INLINE(SprSub1405B9550Mid1) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        size_t index = *reinterpret_cast<size_t*>(ctx->X[20] + 0xF8);
        ctx->X[12] = reinterpret_cast<uintptr_t>(sprite_vertex_data.array + index);
    }
};

HOOK_DEFINE_INLINE(SprSub1405B9550Mid2) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        size_t index = *reinterpret_cast<size_t*>(ctx->X[20] + 0xF8);
        ctx->X[10] = reinterpret_cast<uintptr_t>(sprite_vertex_data.array + index);
    }
};

HOOK_DEFINE_INLINE(SprDrawSpriteFillVertexArrayMid1) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        size_t index = *reinterpret_cast<size_t*>(ctx->X[20] + 0xF8);
        ctx->X[11] = reinterpret_cast<uintptr_t>(sprite_vertex_data.array + index);
    }
};

HOOK_DEFINE_INLINE(SprDrawSpriteFillVertexArrayMid2) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        size_t index = *reinterpret_cast<size_t*>(ctx->X[22] + 0xF8);
        ctx->X[11] = reinterpret_cast<uintptr_t>(sprite_vertex_data.array + index);
    }
};

// ============================================================================
// INITIALIZATION
// ============================================================================

void SpriteDrawLimitPatch::init() {
    sprite_vertex_data.array = nullptr;
    sprite_vertex_data.max_count = 0x2000;

    // 1. NOP original LDR instructions
    exl::patch::CodePatcher(0x006124B4).Write<uint32_t>(0xD503201F);
    exl::patch::CodePatcher(0x006155DC).Write<uint32_t>(0xD503201F);
    exl::patch::CodePatcher(0x00615788).Write<uint32_t>(0xD503201F);
    exl::patch::CodePatcher(0x006157A4).Write<uint32_t>(0xD503201F);
    exl::patch::CodePatcher(0x00619938).Write<uint32_t>(0xD503201F);
    exl::patch::CodePatcher(0x0061A0F8).Write<uint32_t>(0xD503201F);
    exl::patch::CodePatcher(0x0061A5F0).Write<uint32_t>(0xD503201F);
    exl::patch::CodePatcher(0x0061A6B8).Write<uint32_t>(0xD503201F);

    // 2. Install trampoline hooks
    SprArgsSetVertexArray::InstallAtOffset(0x006154C0);
    SprArgsReserveVertexArray::InstallAtOffset(0x00615520);
    SpriteManagerClear::InstallAtOffset(0x00617E40);

    // 3. Install inline hooks over NOP instructions
    SprArgsPutSpriteLineListMid::InstallAtOffset(0x006124B4);
    SprDrawSpriteScaleMid::InstallAtOffset(0x006155DC);
    SprCalcSpriteTextureParamMid1::InstallAtOffset(0x00615788);
    SprCalcSpriteTextureParamMid2::InstallAtOffset(0x006157A4);
    SprSub1405B9550Mid1::InstallAtOffset(0x00619938);
    SprSub1405B9550Mid2::InstallAtOffset(0x0061A0F8);
    SprDrawSpriteFillVertexArrayMid1::InstallAtOffset(0x0061A5F0);
    SprDrawSpriteFillVertexArrayMid2::InstallAtOffset(0x0061A6B8);
}
