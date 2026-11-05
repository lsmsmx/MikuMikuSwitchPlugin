#include <stdint.h>

#include "lib.hpp"
#include "logger.hpp"
#include "ThumbnailLoader.hpp"
#include "nc/diva_nc.hpp"   // spr::LoadSprSet / spr::CheckSprSetLoading, prj::string_view

// Port of DivaModLoader's ThumbnailLoader.cpp.
//
// The PC mod hooks the map that holds the per-PV sprite ids and, for every PV in
// it, looks up "SPR_SEL_PVTMB_%03d" (and its _EX twin) by name, resolves the
// sprite to its set, and preloads that set unless it is the vanilla thumbnail
// set. The reason it has to exist at all: the engine only ever keeps the base
// PV-thumbnail set resident, so a modded PV's thumbnail sprite resolves to a set
// that is never loaded and the tile draws nothing (or the vanilla picture).
//
// On Switch the same data is built by FUN_005d29a0 (per PV) out of FUN_005d26a0
// (whole map). Rather than walking libc++'s red-black tree (the old, disabled
// attempt in MemoryHooks.cpp), this hooks the per-PV builder and reads the
// result struct it just filled in, which already contains exactly the sprite ids
// the name lookups produced:
//
//     *(int *)((long)param_3 + 0x3c) = iVar7;   // SPR_SEL_PVTMB_%03d
//     *(int *)((long)param_3 + 0x44) = iVar7;   // SPR_SEL_PVTMB_%03d_EX
//
// so no name formatting is needed, and the ids are by construction the ones the
// PV select list will draw.
//
// Sprite id -> set id is the engine's own two-step: GetSpriteInfoById, then
// GetSpriteSetByIndex with the sprite's set index. That is literally what the
// engine does at 0x1dd120-0x1dd138:
//
//     bl  0x61da50              ; GetSpriteInfoById(spr_db, sprite_id)
//     ldrh w8, [x0, #0x1a]      ; SpriteInfo.setIndex
//     and  w1, w8, #0xff
//     ldr  x0, [x0, #0x360]     ; spr_db again  (= *(void**)0xCDF360)
//     bl  0x61d810              ; GetSpriteSetByIndex(spr_db, setIndex)
//     ldr  w0, [x0]             ; -> set id
//
// Addresses are module-relative (vaddr == module offset in main_redecompressed).

namespace ThumbnailLoader
{
namespace
{

constexpr uintptr_t ADDR_BUILD_PV_SPRITE_IDS      = 0x005D29A0; // FUN_005d29a0
constexpr uintptr_t ADDR_GET_SPRITE_INFO_BY_ID    = 0x0061DA50; // FUN_0061da50
constexpr uintptr_t ADDR_GET_SPRITE_SET_BY_INDEX  = 0x0061D810; // GetSpriteSetByIndex
constexpr uintptr_t OFFSET_SPR_DB_PTR             = 0x00CDF360; // void** -> spr db manager
constexpr uintptr_t OFFSET_SPR_DEFAULT_INFO_PTR   = 0x00CE23A8; // void** -> SpriteInfo "not found"

constexpr uint32_t SPRITE_ID_NONE       = 0xFFFFFFFF;
// Base set the game already keeps loaded for vanilla thumbnails (same id as the
// PC mod's BASE_SPR_PV_TMB_ID, 4527).
constexpr uint32_t BASE_SPR_PV_TMB_ID   = 4527;
// What FUN_005d29a0 leaves in a tmb slot when the name does not resolve (it
// pre-fills with 0xdb1 and only overwrites on a hit). Never a mod sprite.
constexpr uint32_t TMB_SPRITE_UNRESOLVED = 0x0DB1;

// SpriteInfo.setIndex -- verified against the engine's own read (0x1dd124).
// SpriteInfo keeps spriteIndex at +0x18 and setIndex at +0x1A, i.e. the packed
// id the engine draws with is (setIndex << 16) | spriteIndex.
constexpr uintptr_t SPRITE_INFO_SET_INDEX = 0x1A;

// Mask the engine itself applies to that field right after the load (0x1dd128).
// Vanilla is `and w1,w8,#0xfff`; SpriteLoader.cpp patches that exact instruction
// (ADDR_SPRITE_MASK_1 = FIX(0x001DD228) = 0x1DD128) to `and w1,w8,#0x7fff`.
// Using anything narrower truncates set indices >= 256 -- which is where modded
// spr sets live -- and resolves to the wrong set, so the set that actually holds
// the thumbnail never gets loaded.
constexpr int32_t SPRITE_SET_INDEX_MASK = 0x7FFF;

// What FUN_005d29a0 fills in (param_3). Only pv and tmb are read here.
struct PvSpriteId
{
    const void* pv;      // 0x00 -> pv db entry; *(const uint32_t*)pv is the PV id
    uint32_t    set;     // 0x08 "SPR_SEL_PV%03d" set id
    uint32_t    bg[4];   // 0x0C SPR_SEL_PV%03d_SONG_BG%03d / _EX
    uint32_t    jk[4];   // 0x1C SPR_SEL_PV%03d_SONG_JK%03d / _EX
    uint32_t    logo[4]; // 0x2C SPR_SEL_PV%03d_SONG_LOGO%03d / _EX
    uint32_t    tmb[4];  // 0x3C SPR_SEL_PVTMB_%03d / _EX   (sprite ids, not set ids)
};

// Sets queued in this pass of the PV select build. Fixed storage on purpose: no
// allocation on a path the engine drives, and entries are erased by the poll in
// OnPvDbTask. Overflow only loses the dedupe, not the load.
constexpr size_t kMaxPendingSets = 512;
uint32_t s_pendingSets[kMaxPendingSets];
size_t   s_pendingSetCount = 0;

bool IsPending(uint32_t set_id)
{
    for (size_t i = 0; i < s_pendingSetCount; i++)
        if (s_pendingSets[i] == set_id)
            return true;
    return false;
}

void AddPending(uint32_t set_id)
{
    if (s_pendingSetCount < kMaxPendingSets)
        s_pendingSets[s_pendingSetCount++] = set_id;
}

// Resolves one thumbnail sprite id to its set and queues that set for loading,
// unless it is the vanilla set or already queued. Mirrors the PC mod's body.
void LoadThumbnailSet(void* spr_db, uintptr_t base_addr, uint32_t sprite_id)
{
    if (sprite_id == SPRITE_ID_NONE || sprite_id == TMB_SPRITE_UNRESOLVED)
        return;

    auto GetSpriteInfoById =
        reinterpret_cast<void* (*)(void*, int32_t)>(base_addr + ADDR_GET_SPRITE_INFO_BY_ID);
    auto GetSpriteSetByIndex =
        reinterpret_cast<uint32_t* (*)(void*, int32_t)>(base_addr + ADDR_GET_SPRITE_SET_BY_INDEX);

    void* info = GetSpriteInfoById(spr_db, static_cast<int32_t>(sprite_id));

    // A miss returns the shared default entry, not null (same convention as the
    // AET by-name lookups), so compare against it explicitly.
    if (info == nullptr)
        return;
    if (info == *reinterpret_cast<void**>(base_addr + OFFSET_SPR_DEFAULT_INFO_PTR))
        return;

    const uint16_t set_index = *reinterpret_cast<const uint16_t*>(
        reinterpret_cast<uintptr_t>(info) + SPRITE_INFO_SET_INDEX);

    uint32_t* set = GetSpriteSetByIndex(spr_db, static_cast<int32_t>(set_index & SPRITE_SET_INDEX_MASK));
    if (set == nullptr)
        return;

    uint32_t set_id = *set;
    if (set_id == SPRITE_ID_NONE || set_id == BASE_SPR_PV_TMB_ID)
        return;

    if (IsPending(set_id))
        return;

    AddPending(set_id);

    prj::string_view out; // the engine's out-param, unused here
    spr::LoadSprSet(set_id, &out);
}

HOOK_DEFINE_TRAMPOLINE(PvSpriteIdsHook) {
    static void Callback(uint64_t a1, void* pv_entry, void* values, void* prev,
                         uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8)
    {
        Orig(a1, pv_entry, values, prev, a5, a6, a7, a8);

        if (values == nullptr)
            return;

        const PvSpriteId* ids = reinterpret_cast<const PvSpriteId*>(values);

        // Untouched unless the engine decided this PV belongs in the map (it
        // writes `*param_3 = pv_entry` only on that path); the caller clears the
        // struct beforehand, so a zero here means "skipped this PV".
        if (ids->pv == nullptr)
            return;

        uintptr_t base_addr = exl::util::GetMainModuleInfo().m_Total.m_Start;

        void* spr_db = *reinterpret_cast<void**>(base_addr + OFFSET_SPR_DB_PTR);
        if (spr_db == nullptr)
            return;

        LoadThumbnailSet(spr_db, base_addr, ids->tmb[0]); // SPR_SEL_PVTMB_%03d
        LoadThumbnailSet(spr_db, base_addr, ids->tmb[2]); // SPR_SEL_PVTMB_%03d_EX
    }
};

} // namespace

void OnPvDbTask()
{
    for (size_t i = 0; i < s_pendingSetCount; )
    {
        if (!spr::CheckSprSetLoading(s_pendingSets[i]))
            s_pendingSets[i] = s_pendingSets[--s_pendingSetCount];
        else
            i++;
    }
}

void init()
{
    PvSpriteIdsHook::InstallAtOffset(ADDR_BUILD_PV_SPRITE_IDS);
}

} // namespace ThumbnailLoader