#include "OpdMakerFix.hpp"
#include "lib.hpp"
#include "fs.hpp"
#include <lib/reloc/reloc.hpp>

#include <stdint.h>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>

// The debug-path mapping lives in FsHooks.  It is what turns
// `savedata:/ram/osage_play_data_tmp` into the directory on the SD card the
// generator actually writes into, so the cleanup below deletes the same tree the
// game would have listed.
namespace FsHooks {
    bool RedirectDebugPath(const char* origPath, char* outPath, size_t outSize,
                           bool isWriteMode);
}

// ---------------------------------------------------------------------------
// OPD bake: record frame 0 from a settled pose
//
// What the game's generator does (OpdMakeManager -> job executor FUN_0046e990),
// per motion id, in both of its paths:
//
//   1. settle pass : create a player, then play the motion for `iterations`
//                    frames (chara+7468, default 60), wrapping with
//                    FUN_00524610 (osg_mhd init_data) when the motion ends;
//   2. harvest     : FUN_00480040 -> FUN_0047ebe0 sizes the recorder buffers to
//                    the motion's frame count and resets only the *recorder*
//                    frame index -- it does not touch the osage physics;
//   3. record pass : for n = 1..frame_count-1 { set_frame(n); ctrl(); add_frame(); }
//
// Step 3 therefore starts from whatever pose step 1 happened to end on.  For a
// motion shorter than `iterations` that is roughly the motion's start, but for
// a normal song-length motion the settle pass runs one whole playthrough and
// stops at the *end* of the motion, so the recorded frame 0 is the end pose
// carried into the beginning.  On a loop that shows up as hair/cloth snapped to
// the wrong side for the whole chart -- the "F2nd 180 degree flip".
//
// koren's PC fix (OPDPlayGen, rob.cpp rob_chara_opd_make_data) keys on
// MOTHEAD_DATA_OSAGE_RESET at frame 0 of the motion's mothead: when present it
// runs 600 extra osg_mhd.ctrl() to calm the physics, captures that state and
// forces the record pass to start from it, and marks the OPD no_loop.
//
// The trigger mechanism exists here, and the type numbering is the same as on
// PC: the bake player's stream setup walks the motion's mothead array for types
// 0x3E, 0x4B, 0x43, 0x4A and 0x4F -- the same five types PC's rob_osage_mothead
// subscribes to -- and OSAGE_RESET is 0x40 on both.  The lookup itself is sound:
// FUN_0051f9f0 takes the raw motion id in w0, searches a red-black tree keyed by
// the u32 at node+0x20, and answers a miss with a .bss sentinel whose
// mothead_data pointer (mot+0x20) is null; records are 16 bytes of
// {type, frame, data, pad} terminated by a negative type.
//
// A real 496-motion bake showed that gate never matching: every motion came back
// "no", for a reason that was never established (a lookup miss and a record set
// without 0x40 were both still open).  The settle therefore runs unconditionally
// here.  Either way the *behaviour* under test is koren's: record the motion
// from a settled frame 0 instead of from whatever pose the settle pass happened
// to stop on.
//
// The no_loop half of the PC fix is deliberately not implemented, because on
// this build it has nowhere to live and nothing that reads it.  Switch's
// opd_blend_data is the same 0x18-byte record as PC's, built from the live
// motion by FUN_00501680: {motion_id = mot+0x490, frame = mot+0x4a0,
// frame_count-1 = (float)mot+0x45a - 1, use_blend, type, blend}.  Its readers
// (FUN_005eb100 and the mothead handler at 0x5ef93c) touch motion_id,
// use_blend and type only; +0x0D and +0x0F are stored from uninitialized stack
// slots there, i.e. they are padding.  koren's no_loop sits in that padding and
// is read by his own OPDPlayGen CtrlOsagePlayData, not by retail code.
// His player advances a frame of its own and therefore has to clamp the wrap;
// the Switch one does not: entry.frame *is* the motion player's frame, and the
// per-frame update FUN_00506a10 clamps it into [0, last] (FUN_0042ba90 /
// FUN_0042baa0 / FUN_0042b810) before the entries are rebuilt.  The wrap
// no_loop suppresses cannot occur here.  Same story one level down: the writer
// FUN_0047f6a0 never sets the OPD header's +0x12 and the loader FUN_0047e450
// copies only +0x00/+0x08/+0x10, so the file flag is dropped as well.
//
// Where the settle is injected: the first ctrl() of the record pass.  The job
// (FUN_0046e990) does, per motion:
//
//   FUN_00524610(player)                        reset the player's streams
//   FUN_00480040(chara, motion)                 harvest: size the recorder from
//                                               mot+0x1a00, slot[2] = 0
//   loop f = 0,1,2,... : set_frame(f); ctrl(); FUN_00480060(chara)
//
// so the first sample written into the OPD -- index 0 -- is the pose that ctrl()
// reaches from wherever the settle pass parked the physics, and every later
// sample chains off that one.  A wrong frame 0 therefore poisons the whole
// recorded motion, not just one sample.  The harvest call is the only thing that
// runs between "the settle pass is over" and "frames are being recorded", and it
// receives the chara id and the motion id, so it sets a per-chara pending flag
// that the ctrl hook consumes once: rewind the player to frame 0, run the
// physics steps, restore the frame the caller set, and let the original call
// record the frame as usual.
//
// Cost: kSettleSteps extra ctrl() per motion.  One ctrl() is one frame of
// animation+physics without rendering, so this is the same work the settle pass
// already does 60x per motion -- the bake is dominated by the farc writes, not
// by this.
//
// ---------------------------------------------------------------------------
// Why the bake aborts (fixed below, in SanitizeRecorderFrames)
//
// The recorder is sized from a *different* frame count than the one the job
// records with:
//
//   * FUN_0047ebe0 (reached as FUN_00480040 -> b 0x47ebe0) reads the motion's
//     frame count as a float at mot+0x1a00 (`ldr s0,[x0,#0x1a00]`, 0x47ec1c)
//     and converts it with fcvtzu -- unsigned, 32-bit, unchecked.  It then
//     sizes three float vectors per osage node to that count.
//   * the job that drives the record pass reads the count as a *u16* at
//     mot+0x45a (FUN_0042baa0, `ucvtf` of the halfword) and loops the record
//     pass count-1 times, one frame each, after the harvest records frame 0.
//
// For a healthy motion the two agree.  When +0x1a00 does not hold that number
// -- a motion the manager resolved but never filled in, a ported motion with a
// different header -- fcvtzu turns whatever is there into up to 4.29e9, and
// the recorder asks operator new for 17 GB of vectors from a job thread.  That
// throws bad_alloc, nothing in the job catches it, std::terminate calls
// nn::diag::Abort.  Crash report 01793924761_01001cc00fa1a000.log is exactly
// that: User Break, nnSdk AbortImpl with a null Result, and the innermost
// diva_nx frame is nss+0x46eb54 -- the return address of the `bl 0x480040` in
// the bake job at 0x46eb50, i.e. the abort is inside FUN_0047ebe0.
//
// The trace of a full bake (496 motions, chara 0) settled this: every harvest
// resolves its motion through slot[0], and in a real bake slot[0] is 0 -- the
// job refills that one entry per motion.  In that object the float at +0x1a00
// is the only frame count that exists: the halfword at +0x45a is 0 on every
// one of the 496, so it is a field of the *player's* motion, not the
// recorder's.  The float itself was sane everywhere (22..2000), so the sizer
// gets a usable number for every motion of a normal pack.
//
// That makes the sizer's count unreconcilable with anything else at this
// point, and the only failure it has is fcvtzu on a value that is not a frame
// count: garbage (a memset-0 field gives 0, a leftover pointer gives 1e9+)
// turns into up to 4.29e9 elements and the three vectors per osage node per
// part ask operator new for more than the heap has.  So: leave a plausible
// count alone, and replace an implausible one with the largest count seen so
// far this run -- the frame counts of one pack's motions are all the same
// order of magnitude, and any count at or above the true one only wastes
// memory, it never truncates the recording.
//
// The record pass cannot overrun either way: FUN_0047f4f0 writes frame
// param_1[2] only while param_1[2] < param_1[3], and the sizer sets
// param_1[3] from the same float it sized with.
//
// Safety notes:
//   * FUN_524bf0 has exactly five call sites: three in the baker and two in the
//     PV player FUN_004de490.  The pending flag is only ever set by the baker's
//     harvest call and is cleared both on use and when the job closes
//     (FUN_004800c0), so gameplay never takes the slow path.
//   * The recursion is broken by calling Orig() directly -- our own hook is not
//     re-entered -- so no TLS/thread-local is involved (this module has none).
//   * Nothing here writes a log or touches the fs: the diagnosis tracing this
//     file used to carry is gone, so the plugin stays silent on the SD card.
// ---------------------------------------------------------------------------

namespace OpdMakerFix {
namespace {

constexpr uintptr_t ADDR_OPD_HARVEST = 0x00480040; // FUN_00480040(charaId, motionId)
constexpr uintptr_t ADDR_OPD_CLOSE   = 0x004800C0; // FUN_004800C0(charaId)
constexpr uintptr_t ADDR_OPD_FINALIZE = 0x00480080; // FUN_00480080(charaId), after the record loop
constexpr uintptr_t ADDR_OPD_ADD_FRAME = 0x00480060; // FUN_00480060(charaId)
constexpr uintptr_t ADDR_PLAYER_CTRL = 0x00524BF0; // FUN_00524BF0(player)
constexpr uintptr_t ADDR_SET_FRAME   = 0x00524C50; // FUN_00524C50(float frame, player)

// The recorder, as FUN_00480040 addresses it: six 0x2a0-byte slots behind the
// pointer stored at 0xce1a78, each holding the motion id it is recording at +0.
// Same slot the harvest sizes, so the same motion object is looked up below.
constexpr uintptr_t RECORDER_SLOT_PTR = 0x00CE1A78; // void** -> slots
constexpr uintptr_t RECORDER_STRIDE   = 0x2A0;
constexpr uintptr_t SLOT_MOTION_ID    = 0x00;       // int32

// The motion table FUN_00519ef0 indexes with a raw id (stride 0x8160, no
// bounds check of its own) and the liveness flags it tests first (stride 0xcc,
// -1 = no such motion).  Both are pointers stored in the module's data.
constexpr uintptr_t ADDR_MOTION_GET    = 0x00519EF0; // FUN_00519EF0(mgr_unused, id)
constexpr uintptr_t MOTION_FLAGS_PTR   = 0x00CE1D58; // int32** -> flags
constexpr uintptr_t MOTION_FLAGS_STRIDE = 0xCC;
constexpr int32_t   MOTION_NONE        = -1;

// The frame count the recorder sizes from (FUN_0047ebe0 reads it as a float and
// converts it with fcvtzu; FUN_0042baa0 and the blend entries FUN_00501680
// builds read the same number as the halfword at +0x45a on a healthy motion).
constexpr uintptr_t MOTION_FRAMES_F32 = 0x1A00;

// The recorder's close (FUN_004800c0 -> FUN_0047fe40): for each part 0xf..0x1e
// it asks the motion's osage data for that part (FUN_00500ae0), and when the
// part's "has data" word at +0x1c0 is non-zero it frees the matching pointer at
// slot+0x1a0.  Sixteen pointers, sixteen flags.
constexpr uintptr_t ADDR_PART_GET   = 0x00500AE0; // FUN_00500ae0(osage, index)
constexpr uintptr_t MOTION_OSAGE    = 0x30;       // void* -> osage data
constexpr uintptr_t PART_HAS_DATA   = 0x1C0;      // u64 per part
constexpr uintptr_t CLOSE_PARTS     = 0x1A0;      // 16 pointers the close frees
constexpr int32_t   kFirstPart      = 0xF;        // 0xf..0x1e inclusive
constexpr int32_t   kPartCount      = 0x10;

// What each of those pointers is: FUN_001f0a80 loads it and tail-calls
// FUN_001eef50(p), which frees the buffer at p+0x70, destroys the object at
// p+0x58, then runs the big reset FUN_001ee2f0(p): zero ~0xa50 bytes, walk the
// 0x28-byte element array [p+0x40, p+0x48) freeing flagged elements, and drop
// the reference on the shared block at p+0xa48 (vtable calls when it reaches
// zero).  FUN_0047fe40 itself contains nothing else -- no file work -- so a
// death in there is inside one of these two objects and nowhere else.  The
// element walk is what GuardElements below guards.
constexpr uintptr_t ADDR_PART_DESTROY = 0x001EEF50; // FUN_001eef50(p)
constexpr uintptr_t PART_ELEMS        = 0x40;       // begin of the 0x28-byte array
constexpr uintptr_t PART_ELEMS_END    = 0x48;       // end

// Player object (the bake's stack local passed to every ctrl()).
constexpr uintptr_t PLAYER_CHARA    = 0x00; // void*  -> chara; chara id is a signed byte at +0
constexpr uintptr_t PLAYER_FRAME    = 0x0C; // float  current motion frame (FUN_00524C50 writes this)
constexpr uintptr_t PLAYER_MAXFRAME = 0x10; // float  frame_count - 1; set_frame clamps against it

constexpr int32_t kMaxChara = 6;         // FUN_00480040/FUN_004800c0 clamp to [0, 6)

// Same count koren's OPDPlayGen runs ("Should calm phys").  One step is one
// frame of animation+physics; 600 is ten seconds of settling at 60 fps.
constexpr int32_t kSettleSteps = 600;

// Set by the harvest call of the motion about to be recorded, consumed by the
// first ctrl() after it.  One slot per chara: each bake worker owns one chara,
// so the four workers never touch the same slot.
volatile bool s_pending[kMaxChara];

// The player our ctrl hook saw last, per chara.  The record pass keeps one
// player object for the whole motion and frees it right after the recorder
// finalize, so this is still valid there -- that is where the missing last
// frame is filled in (see FillRecordedTail).  It has to be per chara: the bake
// runs several of them on several worker threads, and a single shared slot is
// overwritten by whichever thread ctrl'd last, which made the fill skip
// motions at random.
volatile uintptr_t s_lastPlayer[kMaxChara] = {};

// What the harvest is about to size, resolved the same way FUN_0047ebe0
// resolves it: the recorder slot for this chara, the motion id it holds at +0,
// and the motion object behind that id.  FUN_00519ef0 already answers null for
// an id with no motion (its -1 flags test), so a null `mot` means the original
// will not size anything either.
struct RecorderMotion {
    int32_t* slot;    // null when the recorder is not up yet
    int32_t motionId; // slot[0] -- the id the sizer looks up
    uint8_t* mot;     // null when there is no motion behind the id
};

RecorderMotion ResolveRecorderMotion(uint32_t charaId) {
    RecorderMotion out{nullptr, -1, nullptr};
    if (charaId >= static_cast<uint32_t>(kMaxChara))
        return out; // FUN_00480040 clamps to [0, 6) and sizes slot 0 for the rest

    uint8_t* const base =
        reinterpret_cast<uint8_t*>(exl::util::GetMainModuleInfo().m_Total.m_Start);

    int32_t* const* slotPtr =
        reinterpret_cast<int32_t* const*>(base + RECORDER_SLOT_PTR);
    if (*slotPtr == nullptr)
        return out;

    out.slot = reinterpret_cast<int32_t*>(
        reinterpret_cast<uint8_t*>(*slotPtr) + charaId * RECORDER_STRIDE);
    out.motionId = *reinterpret_cast<int32_t*>(
        reinterpret_cast<uint8_t*>(out.slot) + SLOT_MOTION_ID);

    // The harvest's own gate, asked the same way, so an id with no motion
    // behind it is left to the original (which returns without touching
    // anything) instead of being written through.
    int32_t* const* flagsPtr =
        reinterpret_cast<int32_t* const*>(base + MOTION_FLAGS_PTR);
    if (*flagsPtr == nullptr)
        return out;
    if (*reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(*flagsPtr) +
                                    static_cast<int64_t>(out.motionId) * MOTION_FLAGS_STRIDE)
        == MOTION_NONE)
        return out;

    using MotionGet = void* (*)(uint64_t, int32_t);
    out.mot = static_cast<uint8_t*>(
        reinterpret_cast<MotionGet>(base + ADDR_MOTION_GET)(0, out.motionId));
    return out;
}

// Put the recorder's frame count back in step with the rest of the engine
// before FUN_0047ebe0 sizes anything from it.  Called with the chara id the
// harvest was called with, so the slot -- and through it the motion id and the
// motion object -- is the exact one the harvest is about to size.
void SanitizeRecorderFrames(uint32_t charaId) {
    const RecorderMotion rm = ResolveRecorderMotion(charaId);
    if (rm.mot == nullptr)
        return;

    // A count above this is not a motion of any PV on this game; the longest
    // song motions are well under 20000 frames.
    constexpr float kPlausibleMax = 20000.0f;

    // Largest sane count seen this run, used as the replacement for a garbage
    // one.  Written from several bake workers; a torn float cannot happen at
    // 4-byte alignment, and a slightly stale maximum is still a good bound.
    static float s_maxFrames = 0.0f;

    float* const frames = reinterpret_cast<float*>(rm.mot + MOTION_FRAMES_F32);
    const float count = *frames;

    // NaN fails both comparisons, so it lands in the repair branch.
    if (count >= 1.0f && count <= kPlausibleMax) {
        if (count > s_maxFrames)
            s_maxFrames = count;
        return; // sane motion: the vanilla bake is left exactly as it is
    }

    *frames = s_maxFrames >= 1.0f ? s_maxFrames : 600.0f;
}

// The sizer (FUN_0047ebe0) allocates from two counts: the frame count and the
// number of osage/cloth nodes per block.  The frame count is clamped by
// SanitizeRecorderFrames; these are the node lists it also multiplies by.
// Node ranges are [block+0x98, block+0xa0) in 0x1d0-byte nodes and
// [block+0x78, block+0x80) in 0xf0-byte nodes; the blocks are reached through
// [part+0x158, part+0x160) and [part+0x1a0, part+0x1a8).
constexpr uintptr_t PART_OSAGE_BLOCKS = 0x158;
constexpr uintptr_t PART_CLOTH_BLOCKS = 0x1A0;
constexpr uintptr_t OSAGE_NODE_BEGIN  = 0x98;
constexpr uintptr_t OSAGE_NODE_END    = 0xA0;
constexpr uintptr_t CLOTH_NODE_BEGIN  = 0x78;
constexpr uintptr_t CLOTH_NODE_END    = 0x80;
constexpr uint64_t  OSAGE_NODE_STRIDE = 0x1D0;
constexpr uint64_t  CLOTH_NODE_STRIDE = 0xF0;
constexpr uint64_t  kMaxBlocks        = 256;

// A backwards or ragged node range is never a legitimate state; the count the
// sizer derives from it is what turns into the giant allocation.  Collapse it
// to empty, which records no nodes for that block instead of dying.
void FixNodeRange(uint8_t* block, uintptr_t beginOff, uintptr_t endOff, uint64_t stride) {
    uint64_t* const begin = reinterpret_cast<uint64_t*>(block + beginOff);
    uint64_t* const end   = reinterpret_cast<uint64_t*>(block + endOff);
    if (*end >= *begin && (*end - *begin) % stride == 0)
        return;
    *end = *begin;
}

void FixBlockList(uint8_t* part, uintptr_t listBeginOff, uintptr_t listEndOff,
                  uintptr_t nodeBeginOff, uintptr_t nodeEndOff, uint64_t stride) {
    uint64_t* const listBegin = reinterpret_cast<uint64_t*>(part + listBeginOff);
    uint64_t* const listEnd   = reinterpret_cast<uint64_t*>(part + listEndOff);
    if (*listEnd < *listBegin || (*listEnd - *listBegin) % 8u != 0) {
        *listEnd = *listBegin;
        return;
    }
    const uint64_t count = (*listEnd - *listBegin) / 8u;
    if (count == 0 || count > kMaxBlocks)
        return;
    for (uint64_t i = 0; i < count; i++) {
        uint8_t* const block = reinterpret_cast<uint8_t*>(listBegin[i]);
        if (block != nullptr)
            FixNodeRange(block, nodeBeginOff, nodeEndOff, stride);
    }
}

// Called from the harvest before the sizer runs, for every part that still
// reports data -- the same sixteen the sizer walks.
void GuardRecorderSizing(uint32_t charaId) {
    const RecorderMotion rm = ResolveRecorderMotion(charaId);
    if (rm.mot == nullptr)
        return;
    void* const osage = *reinterpret_cast<void**>(rm.mot + MOTION_OSAGE);
    if (osage == nullptr)
        return;

    uint8_t* const base =
        reinterpret_cast<uint8_t*>(exl::util::GetMainModuleInfo().m_Total.m_Start);
    using PartGet = void* (*)(void*, int32_t);
    for (int32_t i = 0; i < kPartCount; i++) {
        uint8_t* const part = static_cast<uint8_t*>(
            reinterpret_cast<PartGet>(base + ADDR_PART_GET)(osage, kFirstPart + i));
        if (part == nullptr)
            continue;
        if (*reinterpret_cast<const uint64_t*>(part + PART_HAS_DATA) == 0)
            continue;
        FixBlockList(part, PART_OSAGE_BLOCKS, PART_OSAGE_BLOCKS + 8,
                     OSAGE_NODE_BEGIN, OSAGE_NODE_END, OSAGE_NODE_STRIDE);
        FixBlockList(part, PART_CLOTH_BLOCKS, PART_CLOTH_BLOCKS + 8,
                     CLOTH_NODE_BEGIN, CLOTH_NODE_END, CLOTH_NODE_STRIDE);
    }
}

// The pointers the close is about to destroy.  The destruction hook below only
// speaks while one of these is on the stack, so no other user of that (very
// generic) function is affected.
uintptr_t s_watch[kPartCount];
volatile bool s_watching = false;

bool IsWatched(const void* p) {
    if (!s_watching || p == nullptr)
        return false;
    const uintptr_t v = reinterpret_cast<uintptr_t>(p);
    for (int32_t i = 0; i < kPartCount; i++) {
        if (s_watch[i] == v)
            return true;
    }
    return false;
}

// Arms the destruction guard below with exactly the pointers the recorder's
// close is about to free: the sixteen at +0x1a0, each one only when the part it
// belongs to still reports data.
void ArmCloseGuards(uint32_t charaId) {
    const RecorderMotion rm = ResolveRecorderMotion(charaId);
    if (rm.slot == nullptr || rm.mot == nullptr)
        return;

    void* const osage = *reinterpret_cast<void**>(rm.mot + MOTION_OSAGE);
    if (osage == nullptr)
        return;

    uint8_t* const base =
        reinterpret_cast<uint8_t*>(exl::util::GetMainModuleInfo().m_Total.m_Start);
    using PartGet = void* (*)(void*, int32_t);

    const uintptr_t* const ptrs = reinterpret_cast<const uintptr_t*>(
        reinterpret_cast<uint8_t*>(rm.slot) + CLOSE_PARTS);
    for (int32_t i = 0; i < kPartCount; i++) {
        void* const part =
            reinterpret_cast<PartGet>(base + ADDR_PART_GET)(osage, kFirstPart + i);
        const bool has = part != nullptr &&
                         *reinterpret_cast<const uint64_t*>(
                             static_cast<uint8_t*>(part) + PART_HAS_DATA) != 0;
        s_watch[i] = has ? ptrs[i] : 0;
    }
    s_watching = true; // armed until CloseHook's Orig returns
}

HOOK_DEFINE_TRAMPOLINE(HarvestHook) {
    static void Callback(uint64_t charaId, uint64_t motionId) {
        const uint32_t c = static_cast<uint32_t>(charaId);
        GuardRecorderSizing(c);
        SanitizeRecorderFrames(c);

        // One spare sample for the last frame.
        //
        // The player samples the OPD at the entry's frame and interpolates
        // towards the next sample using the frame's fractional part.  At the
        // last frame that second index is one past the end of the recorded
        // data, so it reads whatever sits behind the component's array -- the
        // next component's first sample -- and the pose moves a little on the
        // final frame.  koren's fix removes the delta-frame call in the four
        // block update paths so the sample is used directly; this does the
        // same thing from the data side: the recorder is sized for one more
        // frame, the tail fill below writes the last pose into it, and the
        // out-of-range read then lands on a copy of the pose it is blending
        // to, so the delta contributes nothing.
        float* frames = nullptr;
        float savedFrames = 0.0f;
        {
            const RecorderMotion rm = ResolveRecorderMotion(c);
            if (rm.mot != nullptr) {
                float* const f = reinterpret_cast<float*>(rm.mot + MOTION_FRAMES_F32);
                const float count = *f;
                if (count >= 1.0f && count <= 20000.0f) {
                    frames = f;
                    savedFrames = count;
                    // Two spare frames, not one: whether this float is the
                    // frame count or the last index is not settled, and with
                    // one spare the sampler's index+1 can still land outside
                    // the data in the second case.  Two covers both, and the
                    // fill below writes the last pose into both slots.
                    *f = count + 2.0f; // only the sizer sees this
                }
            }
        }

        Orig(charaId, motionId);

        if (frames != nullptr)
            *frames = savedFrames;

        // koren's gate answered "no" for every motion of a real bake, so it is
        // not usable as it stands: the settle runs for every harvested chara.
        if (c < static_cast<uint32_t>(kMaxChara))
            s_pending[c] = true;
    }
};

HOOK_DEFINE_TRAMPOLINE(CloseHook) {
    static void Callback(uint64_t charaId) {
        const uint32_t id = static_cast<uint32_t>(charaId);
        ArmCloseGuards(id);
        if (id < static_cast<uint32_t>(kMaxChara))
            s_pending[id] = false;
        Orig(charaId);
        s_watching = false;
    }
};

// The reset body walks [p+0x40, p+0x48) downwards in 0x28-byte steps and never
// stops until it reaches begin, so bounds that are backwards or that are not a
// whole number of elements make it walk off the allocation.  Neither can ever
// be a legitimate state, and the recorder has already written everything it
// needed by the time the close runs, so the walk is taken away instead of
// trusted: an element leaks at worst, the abort does not happen at all.
void GuardElements(uint8_t* q) {
    uint64_t* const begin = reinterpret_cast<uint64_t*>(q + PART_ELEMS);
    uint64_t* const end   = reinterpret_cast<uint64_t*>(q + PART_ELEMS_END);
    const bool bad = *end < *begin || (*end - *begin) % 0x28 != 0;
    if (!bad)
        return;
    *end = *begin;
}

// The destruction of a part the close is holding.  Only the pointers the close
// really armed are touched, so no other user of this (very generic) function is
// affected.
HOOK_DEFINE_TRAMPOLINE(PartDestroyHook) {
    static void Callback(void* p) {
        if (IsWatched(p))
            GuardElements(static_cast<uint8_t*>(p));
        Orig(p);
    }
};

// ---------------------------------------------------------------------------
// The generate-time directory handling.  This is a different code path from the
// bake, and it is the one that dies the moment "generate" is clicked while a
// previous run's `ram/osage_play_data_tmp/<chara>` is still there.
//
// At generate time the manager (around 0x46fc00) asks whether the *final*
// directory `ram/osage_play_data` is missing (0x33300 -> 0x379d0) and, when it
// is not, removes it recursively (0x333a0).  It then asks the same question
// about `ram/osage_play_data_tmp` and creates it only when it is missing
// (0x33730 -> 0x335b0).  The tmp directory is therefore never cleaned: only the
// per-chara subdirectory the writer made survives, and the writer lists that
// subdirectory with 0x33b20 before writing into it.
//
// The wipe below is written but not armed: it runs in the same window as the
// still-unexplained crash, so it waits until the crash itself is gone.
// ---------------------------------------------------------------------------
constexpr const char* kOpdTmpRoot = "savedata:/ram/osage_play_data_tmp";
constexpr int32_t kTmpMaxDepth = 3;
constexpr int64_t kTmpBatch = 24;

[[maybe_unused]] int32_t WipeTmpTree(const char* dir, int32_t depth) {
    if (depth > kTmpMaxDepth)
        return 0;

    nn::fs::DirectoryHandle handle = {};
    if (nn::fs::OpenDirectory(&handle, dir, 3) != 0)
        return 0;

    // One entry is 0x310 bytes and the bake runs on worker threads, so the
    // buffer is static rather than on the stack of whichever thread got here.
    static nn::fs::DirectoryEntry s_entries[kTmpBatch];

    int32_t removed = 0;
    for (;;) {
        int64_t read = 0;
        if (nn::fs::ReadDirectory(&read, s_entries, handle, kTmpBatch) != 0 || read <= 0)
            break;

        for (int64_t i = 0; i < read; i++) {
            char child[512];
            const int n = snprintf(child, sizeof(child), "%s/%s", dir, s_entries[i].m_Name);
            if (n <= 0 || n >= static_cast<int>(sizeof(child)))
                continue;

            if (static_cast<int32_t>(s_entries[i].m_Type) ==
                static_cast<int32_t>(nn::fs::DirectoryEntryType_Directory)) {
                removed += WipeTmpTree(child, depth + 1);
            } else if (R_SUCCEEDED(nn::fs::DeleteFile(child))) {
                removed++;
            }
        }
        if (read < kTmpBatch)
            break;
    }
    nn::fs::CloseDirectory(handle);
    return removed;
}

[[maybe_unused]] void CleanOpdTmp() {
    char sd[512];
    if (!FsHooks::RedirectDebugPath(kOpdTmpRoot, sd, sizeof(sd), true))
        return;
    WipeTmpTree(sd, 0);
}

// The engine's own directory iterator, 0x37480.  It hands the game one entry per
// call, and on this build its listing of `_tmp/<chara>` comes back empty even
// though the files are demonstrably in the directory, which is what makes the
// install step skip every copy.  The iterator is therefore driven from the
// directory object the game itself opened: walk its entries by index and emit
// them through the same slot the original writes.
constexpr uintptr_t ADDR_FS_LIST_ITER = 0x00037480;
constexpr uint32_t kDirEntryBytes = 0x310;

HOOK_DEFINE_TRAMPOLINE(ListIterHook) {
    static void Callback(uint8_t* dir, uint64_t* slot) {
        if (dir == nullptr || slot == nullptr ||
            *slot != reinterpret_cast<uint64_t>(dir)) {
            Orig(dir, slot);
            return;
        }

        struct Cursor {
            uintptr_t dir;
            uint32_t index;
            bool done;
        };
        static Cursor s_cursor[4] = {};

        Cursor* cursor = nullptr;
        for (Cursor& c : s_cursor) {
            if (c.dir == reinterpret_cast<uintptr_t>(dir)) {
                cursor = &c;
                break;
            }
        }
        if (cursor == nullptr) {
            for (Cursor& c : s_cursor) {
                if (c.dir == 0) {
                    cursor = &c;
                    break;
                }
            }
            if (cursor == nullptr)
                cursor = &s_cursor[0];
            cursor->dir = reinterpret_cast<uintptr_t>(dir);
            cursor->index = 0;
            cursor->done = false;
        } else if (cursor->done) {
            cursor->index = 0;
            cursor->done = false;
        }

        const uint8_t* const entries = *reinterpret_cast<const uint8_t* const*>(dir + 0x18);
        const int64_t count = *reinterpret_cast<const int64_t*>(dir + 0x30);
        if (entries == nullptr || count <= 0 ||
            static_cast<int64_t>(cursor->index) >= count) {
            *slot = 0;
            cursor->done = true;
            return;
        }

        uint8_t* const emit = reinterpret_cast<uint8_t*>(slot) + 8;
        memcpy(emit, entries + static_cast<size_t>(cursor->index) * kDirEntryBytes,
               kDirEntryBytes);
        cursor->index++;

    }
};

HOOK_DEFINE_TRAMPOLINE(CtrlHook) {
    static void Callback(void* player) {
        if (player != nullptr) {
            uint8_t* p = static_cast<uint8_t*>(player);
            // FUN_00524bf0 returns immediately when the player has no chara
            // bound, so there is nothing to settle in that case either.
            void* chara = *reinterpret_cast<void**>(p + PLAYER_CHARA);
            const int32_t id = chara != nullptr
                ? static_cast<int32_t>(*reinterpret_cast<const int8_t*>(chara))
                : -1;

            if (static_cast<uint32_t>(id) < static_cast<uint32_t>(kMaxChara))
                s_lastPlayer[id] = reinterpret_cast<uintptr_t>(player);

            if (static_cast<uint32_t>(id) < static_cast<uint32_t>(kMaxChara) &&
                s_pending[id]) {
                s_pending[id] = false;

                float* frame    = reinterpret_cast<float*>(p + PLAYER_FRAME);
                const float max = *reinterpret_cast<float*>(p + PLAYER_MAXFRAME);

                // set_frame() would refuse frame 0 for a motion this short, and
                // such a motion cannot have a settle problem worth chasing.
                if (0.0f < max) {
                    const float saved = *frame;
                    *frame = 0.0f;

                    for (int32_t i = 0; i < kSettleSteps; i++)
                        Orig(player);

                    *frame = saved;
                }
            }
        }
        Orig(player);
    }
};

// ---------------------------------------------------------------------------
// The record pass stops short of the recorder's capacity.
//
// The harvest (FUN_0047ebe0) sizes the recorder from the motion's float frame
// count (`mot+0x1a00`) and sets index 0 / capacity N.  The record loop in
// FUN_0046e990 counts frames instead with the u16 at the player's motion +0x45a
// and runs `while (frame < count - 1)`, i.e. it writes count-1 samples into a
// buffer N long.  When the two counts agree that is exactly one frame short;
// when a ported (F2nd) motion reports a different float than u16, the shortfall
// is larger and the tail is whatever the allocation held.  The commit in
// FUN_0047f6a0 then refuses to run because `index == capacity` is false, and the
// generated OPD ends on unwritten osage data -- the corrupted last frame.
//
// koren's generator records `while (frame <= frame_count)`, one frame further
// than the game does.  We cannot extend the game's loop, but the recorder is
// finalize (FUN_00480080) called right after it and before the player is freed,
// so the remaining frames can be recorded here, with the same three game calls
// the loop uses: set_frame(f), ctrl(), add_frame(chara).
// ---------------------------------------------------------------------------
constexpr int32_t kMaxFillFrames = 600; // a plausible shortfall; anything larger is not this bug

void FillRecordedTail(uint32_t charaId) {
    if (charaId >= static_cast<uint32_t>(kMaxChara))
        return;

    const uintptr_t player = s_lastPlayer[charaId];
    if (player == 0)
        return;
    void* const chara = *reinterpret_cast<void* const*>(player + PLAYER_CHARA);
    if (chara == nullptr ||
        *reinterpret_cast<const int8_t*>(chara) != static_cast<int8_t>(charaId))
        return;

    uint8_t* const base =
        reinterpret_cast<uint8_t*>(exl::util::GetMainModuleInfo().m_Total.m_Start);
    int32_t* const* const slotsPtr =
        reinterpret_cast<int32_t* const*>(base + RECORDER_SLOT_PTR);
    if (*slotsPtr == nullptr)
        return;

    uint8_t* const slot =
        reinterpret_cast<uint8_t*>(*slotsPtr) + static_cast<size_t>(charaId) * RECORDER_STRIDE;
    const bool armed = (*reinterpret_cast<const uint8_t*>(slot + 0x04) & 1u) != 0;
    const uint32_t index = *reinterpret_cast<const uint32_t*>(slot + 0x08);
    const uint32_t capacity = *reinterpret_cast<const uint32_t*>(slot + 0x0C);
    if (!armed || index >= capacity)
        return;
    if (capacity > 20000u || capacity - index > static_cast<uint32_t>(kMaxFillFrames))
        return;

    using SetFrameFn = void (*)(float, void*);
    using CtrlFn     = void (*)(void*);
    using AddFrameFn = void (*)(uint32_t);

    float* const frame = reinterpret_cast<float*>(player + PLAYER_FRAME);
    const float saved = *frame;

    const auto setFrame = reinterpret_cast<SetFrameFn>(base + ADDR_SET_FRAME);
    const auto ctrl     = reinterpret_cast<CtrlFn>(base + ADDR_PLAYER_CTRL);
    const auto addFrame = reinterpret_cast<AddFrameFn>(base + ADDR_OPD_ADD_FRAME);
    void* const p = reinterpret_cast<void*>(player);

    for (uint32_t f = index; f < capacity; f++) {
        setFrame(static_cast<float>(f), p);
        ctrl(p); // goes through CtrlHook; the settle flag is already consumed
        addFrame(charaId);
        if (*reinterpret_cast<const uint32_t*>(slot + 0x08) >= capacity)
            break;
    }

    *frame = saved;
}

HOOK_DEFINE_TRAMPOLINE(FinalizeHook) {
    static void Callback(uint32_t charaId) {
        FillRecordedTail(charaId);
        Orig(charaId);
    }
};

// ---------------------------------------------------------------------------
// EXPERIMENT: let the movie MIME gate see an MPEG-2 track as H.264.
//
// The movie path compares each track's MIME against a fixed list through the
// nnSdk string-compare import at 0x9f9860.  Only audio/mp4a-latm, audio/vorbis,
// video/avc, video/x-vnd.on2.vp8 and video/x-vnd.on2.vp9 are mapped to an
// internal codec code, so a USM whose video is MPEG-2 ("video/mpeg2") matches
// nothing and the load screen waits for a decoder that was never requested.
//
// Rather than patch the thunk (its first instruction is `adrp`, and relocating
// a PC-relative import stub is where hooks break things), the pointer the thunk
// branches through is swapped, so every caller reaches this probe and
// everything else goes straight to the original.
//
// This is a probe, not a fix: MPEG-2 bytes handed to the H.264 decoder should
// fail or render garbage.  The answer it gives is whether that MIME gate is the
// only barrier between the pipeline and a decoder.
// ---------------------------------------------------------------------------
constexpr uintptr_t OFF_MIME_COMPARE_GOT = 0x00CDE1F0;

using CompareFn = int (*)(const void*, const void*, uint64_t);
static CompareFn s_origCompare = nullptr;

static int ProbeVideoMime(const void* a, const void* b, uint64_t n) {
    if (n == 9 && a != nullptr && b != nullptr) {
        const char* const pa = static_cast<const char*>(a);
        const char* const pb = static_cast<const char*>(b);
        if (pa[0] == 'v' && std::memcmp(pa, "video/avc", 9) == 0 &&
            std::memcmp(pb, "video/mpeg2", 9) == 0)
            return 0; // "equal": the track takes the H.264 code
    }
    return s_origCompare(a, b, n);
}

static void InstallMimeProbe() {
    const uintptr_t base =
        reinterpret_cast<uintptr_t>(exl::util::GetMainModuleInfo().m_Total.m_Start);
    void** const slot = reinterpret_cast<void**>(base + OFF_MIME_COMPARE_GOT);
    if (s_origCompare != nullptr || *slot == nullptr)
        return;
    s_origCompare = reinterpret_cast<CompareFn>(*slot);
    *slot = reinterpret_cast<void*>(&ProbeVideoMime);
}

} // namespace

void init() {
    HarvestHook::InstallAtOffset(ADDR_OPD_HARVEST);
    CloseHook::InstallAtOffset(ADDR_OPD_CLOSE);
    FinalizeHook::InstallAtOffset(ADDR_OPD_FINALIZE);
    CtrlHook::InstallAtOffset(ADDR_PLAYER_CTRL);
    PartDestroyHook::InstallAtOffset(ADDR_PART_DESTROY);
    ListIterHook::InstallAtOffset(ADDR_FS_LIST_ITER);
    // The movie-MIME probe is not installed: swapping the import's GOT pointer
    // stopped the game from booting, so that slot is not the string-compare
    // this was meant to intercept (or it is used before init runs).  Left for
    // the record; see the comment on InstallMimeProbe.
    // InstallMimeProbe();
}

} // namespace OpdMakerFix
