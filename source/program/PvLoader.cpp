#include <cstdint>
#include <cstddef>
#include <vector>
#include <algorithm>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "fs.hpp"
#include "patches.hpp"
#include "PvLoader.hpp"

#define FIX(addr)           ((addr) - 0x100)

// Working NOP patches
#define ADDR_NOP_2    FIX(0x5D1CE0)
#define ADDR_NOP_3    FIX(0x5D2B38)
#define ADDR_NOP_4    FIX(0x5D1CEC)

// 32-bit Truncation Patches
#define ADDR_PV_TRUNC_1 FIX(0x0085CA78)
#define ADDR_PV_TRUNC_2 FIX(0x0085D5F8)
#define ADDR_PV_TRUNC_3 FIX(0x0085D600)
#define ADDR_PV_TRUNC_4 FIX(0x0085D6F4)
#define ADDR_PV_TRUNC_5 FIX(0x0085D718)
#define ADDR_PV_TRUNC_6 FIX(0x0085DA1C)

// Database buffer grabber (x21=ptr, x22=size)
#define ADDR_PV_GRAB      FIX(0x4C1280)

// Loop counter control points (TaskPvDBCtrl, case 3)
#define ADDR_PV_LOOP_INIT   FIX(0x4C1290)
#define ADDR_PV_LOOP_NEXT_1 FIX(0x4C1370)
#define ADDR_PV_LOOP_NEXT_2 FIX(0x4C1440)
#define ADDR_PV_LOOP        FIX(0x4C1360)

// LIFO vector matching PC port to preserve stage load order
static std::vector<uint32_t> pvIdStack;

uint32_t pvLoaderParseStartImp(const char* data, size_t length)
{
    uint32_t lastPvId = 0;

    bool isUtf16 = (length >= 2 && (uint8_t)data[0] == 0xFF && (uint8_t)data[1] == 0xFE);

    if (isUtf16) {
        const uint16_t* data16 = reinterpret_cast<const uint16_t*>(data);
        size_t length16 = length / 2;
        size_t i = 1;

        while (i < length16)
        {
            while (i < length16 && (data16[i] == 0x0009 || data16[i] == 0x000A || data16[i] == 0x000D || data16[i] == 0x0020))
                i++;
            if (i >= length16) break;

            if (length16 - i > 3 && data16[i] == 0x0070 && data16[i + 1] == 0x0076 && data16[i + 2] == 0x005F)
            {
                i += 3;
                uint32_t pvId = 0;
                size_t digitsCount = 0;
                while(i < length16 && (data16[i] >= 0x0030 && data16[i] <= 0x0039))
                {
                    pvId = pvId * 10 + (data16[i] - 0x0030);
                    i++;
                    digitsCount++;
                }
                if (digitsCount > 0)
                {
                    if (pvId != lastPvId && pvId != 0)
                    {
                        lastPvId = pvId;
                        pvIdStack.push_back(pvId);
                    }
                }
            }
            while (i < length16 && data16[i] != 0x000A && data16[i] != 0x000D)
                i++;
        }
    } else {
        size_t i = 0;
        while (i < length)
        {
            while (i < length && (data[i] == '\t' || data[i] == '\n' || data[i] == '\r' || data[i] == ' '))
                i++;
            if (i >= length) break;

            if (length - i > 3 && data[i] == 'p' && data[i + 1] == 'v' && data[i + 2] == '_')
            {
                i += 3;
                uint32_t pvId = 0;
                size_t digitsCount = 0;
                while(i < length && (data[i] >= '0' && data[i] <= '9'))
                {
                    pvId = pvId * 10 + (data[i] - '0');
                    i++;
                    digitsCount++;
                }
                if (digitsCount > 0)
                {
                    if (pvId != lastPvId && pvId != 0)
                    {
                        lastPvId = pvId;
                        pvIdStack.push_back(pvId);
                    }
                }
            }
            while (i < length && data[i] != '\n' && data[i] != '\r')
                i++;
        }
    }
    return 0xFFFFFFFF;
}

uint32_t pvLoaderParseLoopImp()
{
    if (!pvIdStack.empty())
    {
        uint32_t pvId = pvIdStack.back();
        pvIdStack.pop_back();
        return pvId;
    }

    return 0xFFFFFFFF;
}

HOOK_DEFINE_INLINE(PvLoaderParseStart) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        const char* data = (const char*)ctx->X[21];
        size_t length = static_cast<size_t>(ctx->X[22]);
        pvLoaderParseStartImp(data, length);
    }
};

HOOK_DEFINE_INLINE(PvLoaderLoopInit) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        uint32_t pvID = pvLoaderParseLoopImp();
        if (pvID != 0xFFFFFFFF) {
            ctx->X[21] = pvID;
        } else {
            ctx->X[21] = 0xFFFFFFFF;
        }
    }
};

HOOK_DEFINE_INLINE(PvLoaderParseLoop1) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        uint32_t pvID = pvLoaderParseLoopImp();
        if (pvID != 0xFFFFFFFF) {
            ctx->X[21] = pvID;
        } else {
            ctx->X[21] = 0xFFFFFFFF;
        }
    }
};

HOOK_DEFINE_INLINE(PvLoaderParseLoop2) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        uint32_t pvID = pvLoaderParseLoopImp();
        if (pvID != 0xFFFFFFFF) {
            ctx->X[21] = pvID;
        } else {
            ctx->X[21] = 0xFFFFFFFF;
        }
    }
};

void PvLoader::init() {

    exl::patch::CodePatcher(0x4c1274).Write<uint32_t>(0x310006BF);
    exl::patch::CodePatcher(0x4c1344).Write<uint32_t>(0x310006BF);
    exl::patch::CodePatcher(0x4c1264).Write<uint32_t>(0x36000068);

    exl::patch::CodePatcher(ADDR_PV_LOOP_INIT).Write<uint32_t>(0xD503201F);
    exl::patch::CodePatcher(ADDR_PV_LOOP_NEXT_1).Write<uint32_t>(0xD503201F);
    exl::patch::CodePatcher(ADDR_PV_LOOP_NEXT_2).Write<uint32_t>(0xD503201F);

    exl::patch::CodePatcher(ADDR_NOP_2).Write<uint32_t>(0xD503201F);
    exl::patch::CodePatcher(ADDR_NOP_3).Write<uint32_t>(0xD503201F);
    exl::patch::CodePatcher(ADDR_NOP_4).Write<uint32_t>(0xD503201F);

    // ==============================================================================
    // 32-bit PV Truncation Patches (ported from PC)
    // ==============================================================================

    // 1. Initialize empty slot with 32-bit -1 (0xFFFFFFFF)
    exl::patch::CodePatcher(FIX(0x0085CA74)).Write<uint32_t>(0x12800008); // mov w8, #-1
    exl::patch::CodePatcher(FIX(0x0085CA78)).Write<uint32_t>(0xB9081808); // str w8, [x0, #0x818]

    // 2. Write song ID into structure (FUN_0085d4c0)
    exl::patch::CodePatcher(FIX(0x0085D5F8)).Write<uint32_t>(0xB9081A93); // str w19, [x20, #0x818]

    // 3. Read song ID without 16-bit truncation (FUN_0085d4c0)
    exl::patch::CodePatcher(FIX(0x0085D600)).Write<uint32_t>(0xB9481A81); // ldr w1, [x20, #0x818]

    // 4. Verify song ID (FUN_0085d5e0)
    exl::patch::CodePatcher(FIX(0x0085D6F4)).Write<uint32_t>(0xB9481808); // ldr w8, [x0, #0x818]

    // 5. Read song ID for playlist call (FUN_0085d5e0)
    exl::patch::CodePatcher(FIX(0x0085D718)).Write<uint32_t>(0xB9481A61); // ldr w1, [x19, #0x818]

    // 6. Read song ID for AET/UI (FUN_0085d870)
    exl::patch::CodePatcher(FIX(0x0085DA1C)).Write<uint32_t>(0xB9481A61); // ldr w1, [x19, #0x818]

    PvLoaderParseStart::InstallAtOffset(ADDR_PV_GRAB);
    PvLoaderLoopInit::InstallAtOffset(ADDR_PV_LOOP_INIT);
    PvLoaderParseLoop1::InstallAtOffset(0x4c1338);
    PvLoaderParseLoop2::InstallAtOffset(0x4c1260);
}
