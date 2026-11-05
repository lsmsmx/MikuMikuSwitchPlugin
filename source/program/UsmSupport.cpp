#include "UsmSupport.hpp"
#include "Mode2DNoMV.hpp"
#include "Config.hpp"
#include "lib.hpp"
#include "logger.hpp"
#include <nn/fs.hpp>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <utility>
#include <atomic>
#include <new>

namespace UsmSupport {

// SHIPPING BUILD: the movie path does no logging at all.
//
// This stub hides ::WriteLog for every call in this file.  It lives in
// namespace UsmSupport, so unqualified lookup finds it before the global one
// declared in logger.hpp and the real logger is never reached from here.  The
// rest of the plugin keeps logging to ExlSD:/hook_log.txt as before; only the
// movie/decoder path goes quiet, and nothing the game reads is affected.
//
// Why it is needed: WriteLog does nn::fs file I/O -- delete, create and open on
// its first call, then a write per line -- and this file is the only place the
// plugin logs from the movie/decoder threads.  Earlier hardware builds died in
// those threads while writing the log.  The same reasoning applies to the
// emulator now: on the latest Eden nightly the title does not boot at all with
// the plugin enabled, and the logger is the plugin's one filesystem writer that
// early (WriteLog is reachable from module init).
//
// To get the diagnosing build back, delete the next line and rebuild.  Nothing
// else in this file has to change.  This is a build for the console, so the
// stub is in place and the movie path stays quiet; put it back the same way
// after any diagnosing build.
static void WriteLog(const char*, ...) {}

// ---------------------------------------------------------------------------
// Path handling
//
// The engine does NOT pass a plain "char*" to TaskMovie::OpenFile.  It passes
// its own length-tagged string object -- the same one ResolveFilePath
// (0x001f4480) reads:
//     byte0 bit0 == 0 : short form -> length = byte0 >> 1, chars at obj + 1
//     byte0 bit0 == 1 : long  form -> length = *(u64 *)(obj + 8),
//                                     chars    = *(char **)(obj + 16)
// Reading it as a C string yields only the first byte (the "1" / "A" seen in
// the log), which is why .usm detection never matched.
// ---------------------------------------------------------------------------
// Decodes that object into a plain C string.
static bool decodeEnginePath(const void* obj, char* out, size_t outSize) {
    if (!obj || outSize == 0) return false;
    const uint8_t* p  = static_cast<const uint8_t*>(obj);
    uint8_t        b0 = p[0];
    uint64_t       len = 0;
    const char*    data = nullptr;

    if (b0 & 1) {
        len  = *reinterpret_cast<const uint64_t*>(p + 8);
        data = *reinterpret_cast<const char* const*>(p + 16);
    } else {
        len  = static_cast<uint64_t>(b0 >> 1);
        data = reinterpret_cast<const char*>(p + 1);
    }

    if (!data || len == 0 || len >= outSize) return false;
    std::memcpy(out, data, static_cast<size_t>(len));
    out[len] = '\0';
    return true;
}

static bool isUsmPath(const char* path) {
    if (!path) return false;
    // Must cover the whole buffer the caller passes (char[1024] in the hook).
    // The old 512 cap stopped counting early, so for any path longer than 512
    // characters the extension check below looked at the wrong position.
    size_t len = 0;
    while (len < 1024 && path[len] != '\0') len++;
    if (len >= 1024 || len < 4) return false;
    // case-insensitive check for .usm
    char c3 = path[len-3];
    char c2 = path[len-2];
    char c1 = path[len-1];
    if (c3 >= 'A' && c3 <= 'Z') c3 += 32;
    if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
    if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
    return path[len-4] == '.' && c3 == 'u' && c2 == 's' && c1 == 'm';
}

// Convert a scheme-less engine path ("rom/movie/x") to a mounted one
// ("rom:/movie/x"), which is what nn::fs expects.  Returns false when the path
// already has a scheme (a ':' before the first '/').
static bool makeMountedPath(const char* p, char* out, size_t outSize) {
    if (!p || !out || outSize == 0) return false;
    const char* slash = std::strchr(p, '/');
    if (!slash || slash == p) return false;
    const char* colon = std::strchr(p, ':');
    if (colon && colon < slash) return false;

    size_t seg  = static_cast<size_t>(slash - p);
    size_t rest = std::strlen(slash);            // includes leading '/'

    // "." and ".." are path syntax, not mount names.  Feeding ".:/rom_switch/
    // rom/movie/pv_273.mp4" to nn::fs asks the filesystem for a mount called
    // "." -- which is what killed the emulator on a plain stock movie open.
    // Also require the segment to look like a name at all.
    if (seg == 0 || seg > 63) return false;
    if (p[0] == '.') return false;
    for (size_t i = 0; i < seg; ++i) {
        const char c = p[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;
    }

    if (seg + 1 + rest + 1 > outSize) return false;

    std::memcpy(out, p, seg);
    out[seg] = ':';
    std::memcpy(out + seg + 1, slash, rest);
    out[seg + 1 + rest] = '\0';
    return true;
}

// Map a game-virtual "rom:/x" path to its physical SD path.
// ModLoader injects "<atmo>/romfs/mods/<name>" as a RomFS search root and
// rewrites it to "rom:/mods/<name>" for the engine (see ToVirtualRomPath), so a
// "rom:/..." path handed to TaskMovie::OpenFile lives under "<atmo>/romfs/..."
// on the SD.  Plain nn::fs cannot resolve the virtual form.
static bool toPhysicalPath(const char* path, char* out, size_t outSize) {
    if (!path || !out) return false;
    if (std::strncmp(path, "rom:/", 5) != 0) return false;

    const std::string& modsDir = Config::modsDirectoryPath;   // "<atmo>/romfs/mods"
    if (modsDir.empty()) return false;

    size_t cut = modsDir.rfind("/mods");
    if (cut == std::string::npos) return false;
    std::string romfs = modsDir.substr(0, cut);               // "<atmo>/romfs"

    int n = std::snprintf(out, outSize, "%s/%s", romfs.c_str(), path + 5);
    return n > 0 && static_cast<size_t>(n) < outSize;
}


// Overwrite the extension of the engine's own path object in place.  Same
// short/long layout as decodeEnginePath reads.
static bool replaceEnginePathExt(const void* obj, const char* newExt) {
    if (!obj || !newExt) return false;
    const uint8_t* p = static_cast<const uint8_t*>(obj);

    char*  data = nullptr;
    size_t len  = 0;
    if (p[0] & 1) {
        len  = *reinterpret_cast<const uint64_t*>(p + 8);
        data = *reinterpret_cast<char* const*>(p + 16);
    } else {
        len  = static_cast<size_t>(p[0] >> 1);
        data = const_cast<char*>(reinterpret_cast<const char*>(p + 1));
    }
    if (!data || len < 5) return false;

    data[len-3] = newExt[0];
    data[len-2] = newExt[1];
    data[len-1] = newExt[2];
    return true;
}




// ===========================================================================
// USM -> Matroska remux
//
// The engine cannot parse a CRI USM, but it can play Matroska/WebM.  When the
// game opens a movie that is really a USM, we repackage its video track into a
// .mkv beside it and hand the engine that file.  The frames are copied
// verbatim: no decode, no re-encode.
//
// Layout facts (verified against the mod USMs and WannaCRI's
// wannacri/usm/chunk.py):
//
//   chunk  = [4-byte tag][u32 BE size][header][payload][padding]
//            `size` counts header + payload + padding (not the 8-byte tag)
//   header: [0x09] payload offset (0x18)
//           [0x0A] padding size (u16 BE)
//           [0x0F] payload type, low 2 bits (0 = stream frame)
//           [0x14] frame rate x100 (u32 BE)
//
//   @SFV video payloads come in two flavours:
//     IVF-in-USM - the first stream packet starts with "DKIF"; the frame that
//                  follows it, and every later packet body, is
//                  12-byte IVF frame header (u32 LE size, u64 LE timestamp) +
//                  frame bytes.
//     raw        - the packet body is the codec frame itself: a VP9 frame, or
//                  an H.264 Annex-B access unit (WannaCRI writes these).
//
// The VIDEO_HDRINFO @UTF page (inside a metadata @SFV packet) carries
// mpeg_codec (5 = H.264, 9 = VP9) plus width/height/framerate_n/d, which is
// where raw-frame variants get their dimensions.
// ===========================================================================

static const bool kUsmRemuxEnabled = true;

static const uint32_t kTagSfv      = 0x40534656u;   // "@SFV"
static const uint32_t kMagicCrid   = 0x43524944u;   // "CRID"
static const uint32_t kMagicEbml   = 0x1A45DFA3u;   // EBML header
static const size_t   kNoPos       = static_cast<size_t>(-1);

enum : int { kRemuxNone = 0, kRemuxVp9 = 1, kRemuxH264 = 2 };

// Frames are described by where they live in the source file, not by a pointer
// into a heap copy of it.  The game's heap is nearly full and fragmented by the
// time a PV opens, so a single malloc(fileSize) fails for the big USMs (34 MB
// with 155 MB free) -- the whole-file read is what made those songs unplayable.
struct RemuxFrame {
    uint64_t off;           // absolute offset of the frame payload
    uint32_t size;
    uint64_t ts;            // IVF ticks (IVF variant only)
    uint8_t  first;         // first payload byte, for keyframe classification
};

static std::vector<RemuxFrame> g_remuxFrames;
static bool     g_remuxIsIvf  = false;
static int      g_remuxCodec  = kRemuxNone;
static uint32_t g_remuxW      = 0;
static uint32_t g_remuxH      = 0;
static uint32_t g_remuxFpsNum = 0;
static uint32_t g_remuxFpsDen = 1;
static uint32_t g_remuxFps100 = 0;       // chunk header frame_rate x100
static uint32_t g_remuxIvfRate  = 1;
static uint32_t g_remuxIvfScale = 1;
static int      g_remuxChannel  = -1;    // video channel being muxed

static uint32_t rdBE32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
            static_cast<uint32_t>(p[3]);
}
static uint32_t rdLE32(const uint8_t* p) {
    return  static_cast<uint32_t>(p[0])        |
           (static_cast<uint32_t>(p[1]) << 8)  |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
static uint16_t rdBE16(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}
static uint16_t rdLE16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
static uint64_t rdLE64(const uint8_t* p) {
    return  static_cast<uint64_t>(p[0])        |
           (static_cast<uint64_t>(p[1]) << 8)  |
           (static_cast<uint64_t>(p[2]) << 16) |
           (static_cast<uint64_t>(p[3]) << 24) |
           (static_cast<uint64_t>(p[4]) << 32) |
           (static_cast<uint64_t>(p[5]) << 40) |
           (static_cast<uint64_t>(p[6]) << 48) |
           (static_cast<uint64_t>(p[7]) << 56);
}
static uint64_t rdBE64(const uint8_t* p) {
    return  (static_cast<uint64_t>(p[0]) << 56) |
            (static_cast<uint64_t>(p[1]) << 48) |
            (static_cast<uint64_t>(p[2]) << 40) |
            (static_cast<uint64_t>(p[3]) << 32) |
            (static_cast<uint64_t>(p[4]) << 24) |
            (static_cast<uint64_t>(p[5]) << 16) |
            (static_cast<uint64_t>(p[6]) << 8)  |
             static_cast<uint64_t>(p[7]);
}

// ---------------------------------------------------------------------------
// CRI @UTF table: read the VIDEO_HDRINFO page.
//
// Column definitions start at +0x20, five bytes each:
//   [type: low 5 bits][occurrence: top 3 bits][u32 BE name offset]
// Recurring values live right after the columns; non-recurring values live in
// the unique array at info + 8 + uniqueArrayOffset.  Offsets are relative to
// info + 8.  This mirrors wannacri/usm/page.py exactly.
// ---------------------------------------------------------------------------
static bool utfValue(const uint8_t** cursor, const uint8_t* end,
                     uint8_t type, int64_t* out) {
    size_t sz;
    switch (type) {
        case 0x10: case 0x11: sz = 1; break;
        case 0x12: case 0x13: sz = 2; break;
        case 0x14: case 0x15: case 0x18: sz = 4; break;
        case 0x16: case 0x17: case 0x19: sz = 8; break;
        case 0x1A: case 0x1B: sz = 4; break;   // string / bytes offset
        default: return false;
    }
    if (*cursor + sz > end) return false;

    const uint8_t* p = *cursor;
    int64_t v = 0;
    switch (type) {
        case 0x10: v = static_cast<int8_t>(p[0]); break;
        case 0x11: v = p[0]; break;
        case 0x12: v = static_cast<int16_t>(rdBE16(p)); break;
        case 0x13: v = rdBE16(p); break;
        case 0x14: v = static_cast<int32_t>(rdBE32(p)); break;
        case 0x15: v = static_cast<int64_t>(rdBE32(p)); break;
        case 0x16: v = static_cast<int64_t>(rdBE64(p)); break;
        case 0x17: v = static_cast<int64_t>(rdBE64(p)); break;
        case 0x1A: v = rdBE32(p); break;
        case 0x1B: v = rdBE32(p); break;
        default:   break;
    }
    *cursor += sz;
    *out = v;
    return true;
}

static void utfParseVideoHdr(const uint8_t* info, size_t avail) {
    if (avail < 0x20 || std::memcmp(info, "@UTF", 4) != 0) return;

    const uint32_t payloadSize = rdBE32(info + 4);
    if (static_cast<size_t>(payloadSize) + 8 > avail) return;

    const uint32_t uniqueOff  = rdBE32(info + 8);
    const uint32_t stringsOff = rdBE32(info + 12);
    const uint32_t bytesOff   = rdBE32(info + 16);
    const uint32_t nameOff    = rdBE32(info + 20);
    const uint16_t numElems   = rdBE16(info + 24);
    const uint16_t uniquePer  = rdBE16(info + 26);
    const uint32_t numPages   = rdBE32(info + 28);

    if (numPages == 0 || numElems == 0) return;
    if (uniqueOff > payloadSize || stringsOff > bytesOff ||
        bytesOff > payloadSize || nameOff >= payloadSize) return;
    if (static_cast<size_t>(uniqueOff) + 8 > static_cast<size_t>(payloadSize) + 8) return;
    if (static_cast<size_t>(uniqueOff) + static_cast<size_t>(uniquePer) * numPages + 8 >
        static_cast<size_t>(payloadSize) + 8) return;

    const uint8_t* strings    = info + 8 + stringsOff;
    const uint8_t* stringsEnd = info + 8 + bytesOff;
    const uint8_t* unique     = info + 8 + uniqueOff;
    const uint8_t* uniqueEnd  = unique + uniquePer;
    const uint8_t* colsEnd    = info + 8 + uniqueOff;

    if (stringsEnd > info + 8 + payloadSize || colsEnd > info + 8 + payloadSize) return;

    const uint8_t* nm = strings + nameOff;
    if (nm >= stringsEnd) return;
    const uint8_t* nul = static_cast<const uint8_t*>(
        std::memchr(nm, 0, static_cast<size_t>(stringsEnd - nm)));
    if (!nul) return;
    const size_t nameLen = static_cast<size_t>(nul - nm);
    if (nameLen != 13 || std::memcmp(nm, "VIDEO_HDRINFO", 13) != 0) return;

    const uint8_t* shared = info + 0x20;   // columns, then recurring values
    const uint8_t* up     = unique;        // non-recurring values

    for (uint16_t i = 0; i < numElems; ++i) {
        if (shared + 5 > colsEnd) return;
        const uint8_t  type = shared[0] & 0x1F;
        const uint8_t  occ  = shared[0] >> 5;      // 1 recurring, 2 non-recurring
        const uint32_t eOff = rdBE32(shared + 1);
        shared += 5;

        const uint8_t* enm = strings + eOff;
        if (enm >= stringsEnd) continue;
        const uint8_t* enul = static_cast<const uint8_t*>(
            std::memchr(enm, 0, static_cast<size_t>(stringsEnd - enm)));
        if (!enul) continue;
        const size_t enLen = static_cast<size_t>(enul - enm);

        int64_t val = 0;
        if (occ == 1) {
            const uint8_t* c = shared;
            if (!utfValue(&c, colsEnd, type, &val)) return;
            shared = c;
        } else {
            const uint8_t* c = up;
            if (!utfValue(&c, uniqueEnd, type, &val)) return;
            up = c;
        }

        if (enLen == 5 && std::memcmp(enm, "width", 5) == 0) {
            if (val > 0 && val <= 16384) g_remuxW = static_cast<uint32_t>(val);
        } else if (enLen == 6 && std::memcmp(enm, "height", 6) == 0) {
            if (val > 0 && val <= 16384) g_remuxH = static_cast<uint32_t>(val);
        } else if (enLen == 11 && std::memcmp(enm, "framerate_n", 11) == 0) {
            if (val > 0) g_remuxFpsNum = static_cast<uint32_t>(val);
        } else if (enLen == 11 && std::memcmp(enm, "framerate_d", 11) == 0) {
            if (val > 0) g_remuxFpsDen = static_cast<uint32_t>(val);
        } else if (enLen == 10 && std::memcmp(enm, "mpeg_codec", 10) == 0) {
            if (val == 9)      g_remuxCodec = kRemuxVp9;
            else if (val == 5) g_remuxCodec = kRemuxH264;
        }
    }
}

// ---------------------------------------------------------------------------
// USM stream walk
//
// The file is read through a small rolling window; only chunk headers, the
// small @UTF metadata pages and the first frame byte are ever touched here.
// Frame payloads stay on disk and are read one at a time while the Matroska
// file is written, so peak memory is one frame plus this window.
// ---------------------------------------------------------------------------
static const size_t kUsmWindowSize = 1u << 20;

// The plugin's own file access: FsHooks' originals for the SDK functions it
// hooks (RawFs, declared in UsmSupport.hpp), handed over at install time by
// SetRawFs below.  Everything here that opens or measures a file goes through
// these, so our own probing cannot re-enter the hooks -- see the note in the fs
// side section for why that matters on hardware.
static RawFs g_rawFs = { nullptr, nullptr, nullptr };

// Those fn pointers are the SDK's ABI, where the handle is an 8-byte struct
// that travels in one register, so they are declared with the plain 64-bit
// value.  This is the one place the typed handle is turned into it.
static inline uint64_t rawHandle(const nn::fs::FileHandle& h) { return h._internal; }

struct UsmFile {
    nn::fs::FileHandle h;
    bool     open;
    int64_t  size;
    uint8_t* win;
    size_t   cap;
    uint64_t pos;           // file offset of win[0]
    size_t   len;           // valid bytes in win
};

static bool usmFileOpen(const char* path, UsmFile* f) {
    std::memset(f, 0, sizeof(*f));
    if (!g_rawFs.open || !g_rawFs.getSize || !g_rawFs.close) return false;
    Result r = g_rawFs.open(&f->h, path, nn::fs::OpenMode_Read);
    if (R_FAILED(r)) return false;
    f->open = true;

    int64_t sz = 0;
    r = g_rawFs.getSize(&sz, rawHandle(f->h));
    if (R_FAILED(r) || sz <= 0 || sz > (512LL * 1024 * 1024)) {
        WriteLog("[USM] stream: bad size %lld\n", (long long)sz);
        g_rawFs.close(rawHandle(f->h));
        f->open = false;
        return false;
    }
    f->size = sz;
    f->cap  = kUsmWindowSize;
    f->win  = static_cast<uint8_t*>(malloc(f->cap));
    if (!f->win) {
        WriteLog("[USM] stream: window malloc failed\n");
        g_rawFs.close(rawHandle(f->h));
        f->open = false;
        return false;
    }
    return true;
}

static void usmFileClose(UsmFile* f) {
    if (f->win) free(f->win);
    f->win = nullptr;
    if (f->open && g_rawFs.close) g_rawFs.close(rawHandle(f->h));
    f->open = false;
}

// Pointers returned here stay valid until the next usmFilePtr/usmFileRead
// call; never hold one across another read.
static bool usmFileEnsure(UsmFile* f, uint64_t off, size_t n) {
    if (off + n > static_cast<uint64_t>(f->size)) return false;
    if (f->len && off >= f->pos && off + n <= f->pos + f->len) return true;

    size_t want = f->cap;
    if (off + want > static_cast<uint64_t>(f->size)) {
        want = static_cast<size_t>(static_cast<uint64_t>(f->size) - off);
    }
    if (want < n) return false;
    const Result r = nn::fs::ReadFile(f->h, static_cast<int64_t>(off), f->win, want);
    if (R_FAILED(r)) return false;
    f->pos = off;
    f->len = want;
    return true;
}

static const uint8_t* usmFilePtr(UsmFile* f, uint64_t off, size_t n) {
    return usmFileEnsure(f, off, n) ? f->win + (off - f->pos) : nullptr;
}

static bool usmFileRead(UsmFile* f, uint64_t off, void* dst, size_t n) {
    return R_SUCCEEDED(
        nn::fs::ReadFile(f->h, static_cast<int64_t>(off), dst, n));
}

static bool addIvfFrameAt(UsmFile* f, uint64_t body, size_t blen) {
    if (blen < 13) return false;
    const uint8_t* p = usmFilePtr(f, body, 13);
    if (!p) return false;

    const uint32_t size = rdLE32(p);
    if (size == 0 || size > blen - 12) return false;

    RemuxFrame fr;
    fr.off   = body + 12;
    fr.size  = size;
    fr.ts    = rdLE64(p + 4);
    fr.first = p[12];
    g_remuxFrames.push_back(fr);
    return true;
}

static bool addRawFrameAt(UsmFile* f, uint64_t off, size_t size) {
    if (size == 0 || size > 64u * 1024u * 1024u) return false;

    const size_t probe = (size < 16) ? size : 16;
    const uint8_t* p = usmFilePtr(f, off, probe);
    if (!p) return false;

    if (g_remuxCodec == kRemuxNone) {
        // H.264 Annex-B starts with 00 00 01 / 00 00 00 01 followed by a
        // plausible NAL header.  MPEG-2's 00 00 01 B3 fails this (0x13 is not
        // a valid H.264 NAL type), so an unsupported USM is rejected rather
        // than muxed as garbage.
        size_t i = 0;
        while (i + 3 <= probe && i < 8 && !(p[i] == 0 && p[i+1] == 0 && p[i+2] == 1)) ++i;
        if (i + 3 <= probe && i < 8) {
            const uint8_t h = p[i + 3];
            const uint8_t t = h & 0x1F;
            if ((h & 0x80) == 0 && t >= 1 && t <= 12) g_remuxCodec = kRemuxH264;
        }
        if (g_remuxCodec == kRemuxNone) {
            // VP9 uncompressed header: frame_marker == 0b10.
            if ((p[0] >> 6) == 2) g_remuxCodec = kRemuxVp9;
        }
        if (g_remuxCodec == kRemuxNone) return false;
    }

    RemuxFrame fr;
    fr.off   = off;
    fr.size  = static_cast<uint32_t>(size);
    fr.ts    = 0;
    fr.first = p[0];
    g_remuxFrames.push_back(fr);
    return true;
}

static void parseDkif(const uint8_t* p, size_t avail) {
    if (avail < 32) return;
    const uint32_t fourcc = rdLE32(p + 8);
    const uint16_t w = rdLE16(p + 12);
    const uint16_t h = rdLE16(p + 14);
    const uint32_t rate  = rdLE32(p + 16);
    const uint32_t scale = rdLE32(p + 20);

    if (w && h) { g_remuxW = w; g_remuxH = h; }
    if (rate)  g_remuxIvfRate  = rate;
    if (scale) g_remuxIvfScale = scale;

    if (fourcc == 0x30395056u || fourcc == 0x20395056u || fourcc == 0x39305056u) {
        g_remuxCodec = kRemuxVp9;
    } else if (fourcc == 0x34363248u || fourcc == 0x31435641u) {
        g_remuxCodec = kRemuxH264;
    }
}

static bool parseUsmFileStream(UsmFile* f) {
    g_remuxFrames.clear();
    g_remuxIsIvf  = false;
    g_remuxCodec  = kRemuxNone;
    g_remuxW      = 0;
    g_remuxH      = 0;
    g_remuxFpsNum = 0;
    g_remuxFpsDen = 1;
    g_remuxFps100 = 0;
    g_remuxIvfRate  = 1;
    g_remuxIvfScale = 1;
    g_remuxChannel  = -1;

    uint64_t off = 0;
    while (off + 8 <= static_cast<uint64_t>(f->size)) {
        const uint8_t* h = usmFilePtr(f, off, 8);
        if (!h) break;
        const uint32_t tag   = rdBE32(h);
        const uint32_t csize = rdBE32(h + 4);
        if (csize < 0x18 ||
            off + 8 + static_cast<uint64_t>(csize) >
                static_cast<uint64_t>(f->size)) {
            break;
        }

        if (tag == kTagSfv) {
            const uint8_t* c = usmFilePtr(f, off, 0x18);
            if (c) {
                // Copy every field before the next read invalidates `c`.
                const uint8_t  payloadOff = c[9];
                const uint16_t pad        = rdBE16(c + 0x0A);
                const uint8_t  channel    = c[0x0C];
                const uint8_t  ptype      = c[0x0F] & 3;
                const uint32_t fr         = rdBE32(c + 0x14);
                if (fr >= 100) g_remuxFps100 = fr;

                if (payloadOff >= 0x18 &&
                    static_cast<size_t>(payloadOff) + pad <= csize) {
                    const uint64_t body = off + 8 + payloadOff;
                    const size_t   blen = static_cast<size_t>(csize) -
                                          payloadOff - pad;

                    const uint8_t* b0 = (blen >= 4)
                        ? usmFilePtr(f, body, 4) : nullptr;
                    if (b0 && std::memcmp(b0, "@UTF", 4) == 0) {
                        if (blen <= (1u << 20)) {
                            uint8_t* tmp = static_cast<uint8_t*>(malloc(blen));
                            if (tmp) {
                                if (usmFileRead(f, body, tmp, blen)) {
                                    utfParseVideoHdr(tmp, blen);
                                }
                                free(tmp);
                            }
                        }
                    } else if (ptype == 0 && blen >= 12) {
                        // Sofdec2 can carry more than one video channel (an
                        // alpha plane, for instance).  Mixing them into one
                        // track would corrupt the stream, so stick to the
                        // first one seen.
                        if (g_remuxChannel < 0) g_remuxChannel = channel;

                        if (channel == g_remuxChannel) {
                            const uint8_t* b = usmFilePtr(f, body, 32);
                            if (!g_remuxIsIvf && b &&
                                std::memcmp(b, "DKIF", 4) == 0) {
                                g_remuxIsIvf = true;
                                parseDkif(b, 32);

                                size_t hdrLen = rdLE16(b + 6);
                                if (hdrLen < 32 || hdrLen > blen) hdrLen = 32;
                                if (hdrLen + 12 <= blen) {
                                    addIvfFrameAt(f, body + hdrLen,
                                                  blen - hdrLen);
                                }
                            } else if (g_remuxIsIvf) {
                                addIvfFrameAt(f, body, blen);
                            } else {
                                addRawFrameAt(f, body, blen);
                            }
                        }
                    }
                }
            }
        }
        off += 8 + static_cast<uint64_t>(csize);
    }

    return !g_remuxFrames.empty() && g_remuxCodec != kRemuxNone;
}


static bool vp9IsKeyframe(const uint8_t* p, size_t n) {
    if (n < 1) return false;
    const uint8_t b = p[0];
    if ((b >> 6) != 2) return false;        // frame_marker
    if (b & 0x08) return false;             // show_existing_frame
    return (b & 0x04) == 0;                 // frame_type == KEY_FRAME
}

// ---------------------------------------------------------------------------
// Matroska writer
//
// Element sizes are encoded as fixed 8-byte vints, so every size can be
// computed arithmetically before anything is written (Segment, Cluster and
// Cues sizes are known up front).  The element set mirrors the one ffmpeg
// writes and that the engine is known to play: SeekHead + Void, Info, Tracks
// (with Colour and FlagDefault), keyframe-aligned clusters, and a trailing
// Cues index over every cluster.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Virtual-file plan
//
// The Matroska byte stream is two kinds of bytes: container metadata this
// code generates (headers, cluster and block headers, cue points, the two
// Voids), and movie payload that is already sitting in the .usm.  A plan is
// the whole file described as a run list instead of as bytes: every metadata
// run is kept in a small buffer, every payload run is a (virtual offset, .usm
// offset) pair.  That is what lets the engine read a Matroska file that does
// not exist on the card -- see the fs hooks at the bottom of this section.
//
// The payload runs are only faithful for VP9, whose frames go into a
// SimpleBlock byte for byte.  H.264 USMs are rewritten into AVCC on the way in
// and need a real file, so the plan refuses them.
// ---------------------------------------------------------------------------
struct VPiece {
    uint64_t vOff;   // offset in the virtual file
    uint32_t len;
    uint32_t kind;   // 0 = metadata (a = offset into meta), 1 = payload (a = .usm offset)
                     // 2 = H.264 frame: Annex-B in the .usm, AVCC on the way out
    uint64_t a;
    uint32_t srcLen; // kind 2: size of the source frame
};

struct VPlan {
    bool                 ok;
    uint64_t             size;
    std::vector<uint8_t> meta;
    std::vector<VPiece>  pieces;
    size_t               cursor;   // sequential-read hint into pieces
};

struct MkvWriter {
    uint64_t pos;

    // Nothing reaches a file.  Non-payload bytes are appended to `meta`; the
    // put that follows `pendingPayload` is recorded as one payload run
    // pointing at `pendingSrc` in the .usm.
    VPlan*   plan;
    bool     pendingPayload;
    uint64_t pendingSrc;
    uint32_t pendingKind;    // 1 = verbatim payload, 2 = H.264 frame
    uint32_t pendingSrcLen;
};


static bool mkvInitPlan(MkvWriter* w, VPlan* plan) {
    w->pos  = 0;
    w->plan = plan;
    w->pendingPayload = false;
    w->pendingSrc = 0;
    w->pendingKind = 1;
    w->pendingSrcLen = 0;
    plan->size   = 0;
    plan->cursor = 0;
    plan->pieces.clear();
    plan->meta.clear();
    return true;
}

// Appends a metadata run, merging it with the previous one when they are
// adjacent in both spaces (the common case: id, size and value of one
// element are three puts).
static void vplanMeta(VPlan* p, uint64_t vOff, const void* data, size_t n) {
    if (n == 0) return;
    if (!p->pieces.empty()) {
        VPiece& last = p->pieces.back();
        if (last.kind == 0 && last.vOff + last.len == vOff &&
            last.a + last.len == p->meta.size()) {
            const uint8_t* s = static_cast<const uint8_t*>(data);
            p->meta.insert(p->meta.end(), s, s + n);
            last.len += static_cast<uint32_t>(n);
            return;
        }
    }
    VPiece q;
    q.vOff = vOff;
    q.len  = static_cast<uint32_t>(n);
    q.kind = 0;
    q.a    = p->meta.size();
    q.srcLen = 0;
    const uint8_t* s = static_cast<const uint8_t*>(data);
    p->meta.insert(p->meta.end(), s, s + n);
    p->pieces.push_back(q);
}

static void vplanPayload(VPlan* p, uint64_t vOff, uint32_t n, uint64_t srcOff) {
    if (n == 0) return;
    if (!p->pieces.empty()) {
        VPiece& last = p->pieces.back();
        if (last.kind == 1 && last.vOff + last.len == vOff &&
            last.a + last.len == srcOff) {
            last.len += n;
            return;
        }
    }
    VPiece q;
    q.vOff = vOff;
    q.len  = n;
    q.kind = 1;
    q.a    = srcOff;
    q.srcLen = 0;
    p->pieces.push_back(q);
}

// An H.264 frame: the bytes are reframed on read (Annex-B start codes become
// 4-byte length prefixes), so the run cannot merge with anything and its
// virtual length differs from the source length.
static void vplanFrame(VPlan* p, uint64_t vOff, uint32_t n, uint64_t srcOff,
                       uint32_t srcLen) {
    if (n == 0) return;
    VPiece q;
    q.vOff = vOff;
    q.len  = n;
    q.kind = 2;
    q.a    = srcOff;
    q.srcLen = srcLen;
    p->pieces.push_back(q);
}


static void mkvPut(MkvWriter* w, const void* data, size_t n) {
    if (w->pendingPayload) {
        // The engine will read these bytes straight out of the .usm: record
        // the run, never the bytes.  An H.264 frame is recorded as a
        // transformed run instead, with its AVCC length already known.
        w->pendingPayload = false;
        if (w->pendingKind == 2)
            vplanFrame(w->plan, w->pos, static_cast<uint32_t>(n), w->pendingSrc,
                       w->pendingSrcLen);
        else
            vplanPayload(w->plan, w->pos, static_cast<uint32_t>(n), w->pendingSrc);
        w->pendingSrc = 0;
        w->pendingKind = 1;
        w->pendingSrcLen = 0;
    } else {
        vplanMeta(w->plan, w->pos, data, n);
    }
    w->pos += n;
}

static size_t idLen(uint32_t id) {
    if (id > 0xFFFFFFu) return 4;
    if (id > 0xFFFFu)   return 3;
    if (id > 0xFFu)     return 2;
    return 1;
}
static size_t uintLen(uint64_t v) {
    size_t n = 1;
    while (n < 8 && (v >> (8 * n)) != 0) ++n;
    return n;
}
static size_t elemSize(uint32_t id, size_t payload) {
    return idLen(id) + 8 + payload;
}
static size_t uintElemSize(uint32_t id, uint64_t v) {
    return elemSize(id, uintLen(v));
}

static void mkvId(MkvWriter* w, uint32_t id) {
    uint8_t b[4];
    const size_t n = idLen(id);
    for (size_t i = 0; i < n; ++i) {
        b[i] = static_cast<uint8_t>(id >> (8 * (n - 1 - i)));
    }
    mkvPut(w, b, n);
}
static void mkvSize(MkvWriter* w, uint64_t size) {
    uint8_t b[8];
    b[0] = 0x01;   // 8-byte vint
    for (int i = 0; i < 7; ++i) {
        b[7 - i] = static_cast<uint8_t>(size >> (8 * i));
    }
    mkvPut(w, b, 8);
}
static void mkvUint(MkvWriter* w, uint32_t id, uint64_t v) {
    const size_t n = uintLen(v);
    mkvId(w, id);
    mkvSize(w, n);
    uint8_t b[8];
    for (size_t i = 0; i < n; ++i) {
        b[i] = static_cast<uint8_t>(v >> (8 * (n - 1 - i)));
    }
    mkvPut(w, b, n);
}
// Always 8 payload bytes.  Used for the seek/cue positions so their element
// sizes stay constant while those positions are still being computed.
static void mkvUintFixed8(MkvWriter* w, uint32_t id, uint64_t v) {
    mkvId(w, id);
    mkvSize(w, 8);
    uint8_t b[8];
    for (int i = 0; i < 8; ++i) {
        b[i] = static_cast<uint8_t>(v >> (8 * (7 - i)));
    }
    mkvPut(w, b, 8);
}

static void mkvFloat64(MkvWriter* w, uint32_t id, double v) {
    uint64_t u = 0;
    std::memcpy(&u, &v, 8);
    mkvId(w, id);
    mkvSize(w, 8);
    uint8_t b[8];
    for (int i = 0; i < 8; ++i) {
        b[i] = static_cast<uint8_t>(u >> (8 * (7 - i)));
    }
    mkvPut(w, b, 8);
}
static void mkvString(MkvWriter* w, uint32_t id, const char* s) {
    const size_t n = std::strlen(s);
    mkvId(w, id);
    mkvSize(w, n);
    mkvPut(w, s, n);
}


// ---------------------------------------------------------------------------
// H.264: Annex-B -> AVCC
//
// A USM stores H.264 with start codes (00 00 01 / 00 00 00 01); Matroska wants
// length-prefixed NAL units plus an avcC record built from the SPS and PPS.
// The frames are not copied at plan time -- each one is reframed while the
// engine reads it, so the plan still points into the .usm.
// ---------------------------------------------------------------------------
static std::vector<uint32_t> g_h264Sizes;  // AVCC size per frame
static std::vector<uint8_t>  g_h264Keys;   // 1 when the frame carries an IDR
static std::vector<uint8_t>  g_avcC;

static inline bool nalStart(const uint8_t* p, size_t n, size_t i, size_t* len) {
    if (i + 3 <= n && p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 1) { *len = 3; return true; }
    if (i + 4 <= n && p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 0 && p[i + 3] == 1) {
        *len = 4;
        return true;
    }
    return false;
}

static bool h264Prescan(UsmFile* file) {
    g_h264Sizes.clear();
    g_h264Keys.clear();
    g_avcC.clear();

    std::vector<uint8_t> sps, pps;
    uint8_t* buf = nullptr;
    size_t cap = 0;
    for (size_t k = 0; k < g_remuxFrames.size(); ++k) {
        const RemuxFrame& fr = g_remuxFrames[k];
        if (fr.size == 0 || fr.size > (64u << 20)) { free(buf); return false; }
        if (fr.size > cap) {
            uint8_t* const nb = static_cast<uint8_t*>(realloc(buf, fr.size));
            if (nb == nullptr) { free(buf); return false; }
            buf = nb;
            cap = fr.size;
        }
        if (!usmFileRead(file, fr.off, buf, fr.size)) { free(buf); return false; }

        uint32_t dst = 0;
        bool key = false;
        size_t i = 0, sc = 0;
        while (i + 3 <= fr.size) {
            if (!nalStart(buf, fr.size, i, &sc)) { ++i; continue; }
            const size_t nal = i + sc;
            size_t j = nal + 1, nsc = 0;
            while (j + 3 <= fr.size && !nalStart(buf, fr.size, j, &nsc)) ++j;
            const size_t len = j - nal;
            if (len > 0) {
                const uint8_t t = buf[nal] & 0x1F;
                if (t == 7 && sps.empty())
                    sps.assign(buf + nal, buf + nal + len);
                else if (t == 8 && pps.empty())
                    pps.assign(buf + nal, buf + nal + len);
                else if (t == 5)
                    key = true;
                dst += 4 + static_cast<uint32_t>(len);
            }
            i = j;
        }
        g_h264Sizes.push_back(dst);
        g_h264Keys.push_back(key ? 1 : 0);
    }
    free(buf);

    if (sps.size() < 4 || pps.empty()) return false;
    g_avcC.push_back(1);
    g_avcC.push_back(sps[1]);
    g_avcC.push_back(sps[2]);
    g_avcC.push_back(sps[3]);
    g_avcC.push_back(0xFF);                 // 4-byte NAL lengths
    g_avcC.push_back(0xE1);                 // one SPS
    g_avcC.push_back(static_cast<uint8_t>(sps.size() >> 8));
    g_avcC.push_back(static_cast<uint8_t>(sps.size() & 0xFF));
    g_avcC.insert(g_avcC.end(), sps.begin(), sps.end());
    g_avcC.push_back(1);                    // one PPS
    g_avcC.push_back(static_cast<uint8_t>(pps.size() >> 8));
    g_avcC.push_back(static_cast<uint8_t>(pps.size() & 0xFF));
    g_avcC.insert(g_avcC.end(), pps.begin(), pps.end());
    return true;
}

static bool mkvBuildAndWrite(UsmFile* file, VPlan* plan) {
    plan->ok = false;
    plan->size = 0;
    plan->pieces.clear();
    plan->meta.clear();
    const int64_t sourceSize = file->size;
    const size_t n = g_remuxFrames.size();
    if (n == 0 || g_remuxW == 0 || g_remuxH == 0) return false;
    // VP9 payload is byte-identical to what the .usm holds; H.264 is reframed
    // on read and everything else is not supported.
    const bool h264 = (g_remuxCodec == kRemuxH264);
    if (g_remuxCodec != kRemuxVp9 && !h264) return false;
    if (h264 && !h264Prescan(file)) return false;

    if (g_remuxFpsNum == 0 || g_remuxFpsDen == 0) {
        if (g_remuxFps100 >= 100) {
            g_remuxFpsNum = g_remuxFps100;
            g_remuxFpsDen = 100;
        } else {
            g_remuxFpsNum = 30;
            g_remuxFpsDen = 1;
        }
    }

    const uint64_t frameDurMs =
        (1000ull * g_remuxFpsDen + g_remuxFpsNum / 2) / g_remuxFpsNum;
    const uint64_t frameDurNs =
        1000000000ull * g_remuxFpsDen / g_remuxFpsNum;
    const uint64_t durMs = frameDurMs ? frameDurMs : 1;

    uint64_t* ts = static_cast<uint64_t*>(malloc(n * sizeof(uint64_t)));
    if (!ts) return false;
    for (size_t i = 0; i < n; ++i) {
        if (g_remuxIsIvf) {
            ts[i] = g_remuxFrames[i].ts * g_remuxIvfScale * 1000ull / g_remuxIvfRate;
        } else {
            ts[i] = static_cast<uint64_t>(i) * 1000ull * g_remuxFpsDen / g_remuxFpsNum;
        }
    }

    const char* codecId = h264 ? "V_MPEG4/ISO/AVC" : "V_VP9";
    const char* docType = h264 ? "matroska" : "webm";
    // H.264 needs the avcC record out of band; VP9 needs nothing.
    const size_t codecPrivateElem = h264 ? elemSize(0x63A2, g_avcC.size()) : 0;

    // ---- per-frame classification + cluster plan -------------------------
    uint8_t*  keys    = static_cast<uint8_t*>(malloc(n));
    size_t*   samples = static_cast<size_t*>(malloc(n * sizeof(size_t)));
    uint32_t* clustStart = static_cast<uint32_t*>(malloc(n * sizeof(uint32_t)));
    uint64_t* clusterPos = nullptr;
    if (!keys || !samples || !clustStart) {
        free(keys); free(samples); free(clustStart); free(ts);
        return false;
    }

    for (size_t i = 0; i < n; ++i) {
        const RemuxFrame& f = g_remuxFrames[i];
        if (h264) {
            keys[i] = g_h264Keys[i];
            samples[i] = g_h264Sizes[i];
        } else {
            keys[i] = vp9IsKeyframe(&f.first, 1) ? 1 : 0;
            samples[i] = f.size;
        }
    }

    // Keyframe-aligned clusters, at most 5 s each: the shape ffmpeg writes.
    size_t nClusters = 0;
    for (size_t s = 0; s < n; ) {
        clustStart[nClusters++] = static_cast<uint32_t>(s);
        size_t e = s + 1;
        while (e < n && ts[e] - ts[s] <= 5000 && !keys[e]) ++e;
        s = e;
    }

    // ---- sizes -----------------------------------------------------------
    const size_t infoPayload =
        uintElemSize(0x2AD7B1, 1000000) +      // TimecodeScale
        elemSize(0x4D80, 7) +                  // MuxingApp "usm2mkv"
        elemSize(0x5741, 7) +                  // WritingApp "usm2mkv"
        elemSize(0x4489, 8);                   // Duration (double, ms)
    const size_t infoElem = elemSize(0x1549A966, infoPayload);

    const size_t colourPayload = uintElemSize(0x55B1, 1) + uintElemSize(0x55B9, 1);
    const size_t videoPayload =
        uintElemSize(0xB0, g_remuxW) + uintElemSize(0xBA, g_remuxH) +
        elemSize(0x55B0, colourPayload);
    const size_t videoElem = elemSize(0xE0, videoPayload);

    size_t trackPayload =
        uintElemSize(0xD7, 1) +                // TrackNumber
        uintElemSize(0x73C5, 0x0123456789ABCDEFull) +  // TrackUID
        uintElemSize(0x9C, 0) +                // FlagLacing
        elemSize(0x22B59C, 3) +                // Language "und"
        uintElemSize(0x88, 0) +                // FlagDefault (as in the ref)
        elemSize(0x86, std::strlen(codecId)) +
        codecPrivateElem +
        uintElemSize(0x83, 1) +                // TrackType = video
        uintElemSize(0x23E383, frameDurNs) +   // DefaultDuration
        videoElem;
    const size_t trackElem = elemSize(0xAE, trackPayload);
    const size_t tracksElem = elemSize(0x1654AE6B, trackElem);

    // SeekHead holds four fixed-size entries; every position is written as an
    // 8-byte uint, which keeps the element's size independent of its values.
    // Seek = 2-byte id + 8-byte size + [SeekID 14 + SeekPosition 18] = 42.
    static const size_t kSeekEntrySize = 42;
    const size_t seekPayload = 4 * kSeekEntrySize;
    const size_t seekElem = elemSize(0x114D9B74, seekPayload);
    static const size_t kVoidPayload = 32;
    const size_t voidElem = 1 + 8 + kVoidPayload;

    // CuePoint, all ids one byte wide: CueTime(17) + CueTrackPositions(36)
    // where CueTrackPositions = CueTrack(10) + CueClusterPosition(17) + 10.
    static const size_t kCuePointSize = 62;
    const size_t cuesPayload = nClusters * kCuePointSize;
    const size_t cuesElem = elemSize(0x1C53BB6B, cuesPayload);

    const size_t segPrefix = seekElem + voidElem + infoElem + tracksElem;

    size_t clusterBytes = 0;
    for (size_t c = 0; c < nClusters; ++c) {
        const size_t s = clustStart[c];
        const size_t e = (c + 1 < nClusters) ? clustStart[c + 1] : n;
        size_t payload = uintElemSize(0xE7, ts[s]);
        for (size_t i = s; i < e; ++i) {
            payload += elemSize(0xA3, 4 + samples[i]);
        }
        clusterBytes += elemSize(0x1F43B675, payload);
    }

    // Cluster offsets, relative to the start of the Segment payload.
    clusterPos = static_cast<uint64_t*>(malloc(nClusters * sizeof(uint64_t)));
    if (!clusterPos) {
        free(keys); free(samples); free(clustStart); free(ts);
        return false;
    }
    {
        uint64_t off = segPrefix;
        for (size_t c = 0; c < nClusters; ++c) {
            clusterPos[c] = off;
            const size_t s = clustStart[c];
            const size_t e = (c + 1 < nClusters) ? clustStart[c + 1] : n;
            size_t payload = uintElemSize(0xE7, ts[s]);
            for (size_t i = s; i < e; ++i) {
                payload += elemSize(0xA3, 4 + samples[i]);
            }
            off += elemSize(0x1F43B675, payload);
        }
    }

    const size_t segmentPayload = segPrefix + clusterBytes + cuesElem;

    const size_t ebmlPayload =
        uintElemSize(0x4286, 1) +              // EBMLVersion
        uintElemSize(0x42F7, 1) +              // EBMLReadVersion
        uintElemSize(0x42F2, 4) +              // EBMLMaxIDLength
        uintElemSize(0x42F3, 8) +              // EBMLMaxSizeLength
        elemSize(0x4282, std::strlen(docType)) +
        uintElemSize(0x4287, 2) +              // DocTypeVersion
        uintElemSize(0x4285, 2);               // DocTypeReadVersion
    const size_t ebmlElem = elemSize(0x1A45DFA3, ebmlPayload);

    static const size_t kTrailerSize = 25;     // Void element, see below
    const uint64_t totalSize =
        ebmlElem + 4 + 8 + segmentPayload + kTrailerSize;

    WriteLog("[USM] mkv: n=%zu %ux%u codec=%d totalSize=%llu\n",
             n, g_remuxW, g_remuxH, g_remuxCodec,
             (unsigned long long)totalSize);

    // ---- output -----------------------------------------------------------
    // Nothing is written: the same element stream is produced, but payload
    // runs are recorded against their .usm offsets instead of being copied.
    MkvWriter w;
    mkvInitPlan(&w, plan);

    // ---- EBML header ------------------------------------------------------
    mkvId(&w, 0x1A45DFA3);
    mkvSize(&w, ebmlPayload);
    mkvUint(&w, 0x4286, 1);
    mkvUint(&w, 0x42F7, 1);
    mkvUint(&w, 0x42F2, 4);
    mkvUint(&w, 0x42F3, 8);
    mkvString(&w, 0x4282, docType);
    mkvUint(&w, 0x4287, 2);
    mkvUint(&w, 0x4285, 2);

    // ---- Segment ----------------------------------------------------------
    mkvId(&w, 0x18538067);
    mkvSize(&w, segmentPayload);

    // SeekHead + Void
    mkvId(&w, 0x114D9B74);
    mkvSize(&w, seekPayload);
    {
        const uint32_t seekIds[4] = { 0x1549A966, 0x1654AE6B, 0x1F43B675, 0x1C53BB6B };
        const uint64_t seekPos[4] = {
            seekElem + voidElem,
            seekElem + voidElem + infoElem,
            seekElem + voidElem + infoElem + tracksElem,
            segmentPayload - cuesElem
        };
        for (int i = 0; i < 4; ++i) {
            mkvId(&w, 0x4DBB);
            mkvSize(&w, 32);
            mkvId(&w, 0x53AB);
            mkvSize(&w, 4);
            mkvId(&w, seekIds[i]);
            mkvUintFixed8(&w, 0x53AC, seekPos[i]);
        }
    }
    mkvId(&w, 0xEC);
    mkvSize(&w, kVoidPayload);
    {
        // The same padding as the trailing Void: skipped by every parser, and
        // kept because its length is part of the layout this format was
        // validated with.
        static const char kMagic[8] = { 'U','S','M','R','E','F','0','0' };
        mkvPut(&w, kMagic, 8);
        uint8_t szb[8];
        const uint64_t v = static_cast<uint64_t>(sourceSize);
        for (int i = 0; i < 8; ++i) szb[i] = static_cast<uint8_t>(v >> (8 * (7 - i)));
        mkvPut(&w, szb, 8);
        const uint8_t zero[16] = { 0 };
        mkvPut(&w, zero, 16);
    }

    // Info
    mkvId(&w, 0x1549A966);
    mkvSize(&w, infoPayload);
    mkvUint(&w, 0x2AD7B1, 1000000);            // 1 ms timecode scale
    mkvString(&w, 0x4D80, "usm2mkv");
    mkvString(&w, 0x5741, "usm2mkv");
    mkvFloat64(&w, 0x4489, static_cast<double>(ts[n - 1] + durMs));

    // Tracks
    mkvId(&w, 0x1654AE6B);
    mkvSize(&w, trackElem);
    mkvId(&w, 0xAE);
    mkvSize(&w, trackPayload);
    mkvUint(&w, 0xD7, 1);
    mkvUint(&w, 0x73C5, 0x0123456789ABCDEFull);
    mkvUint(&w, 0x9C, 0);
    mkvString(&w, 0x22B59C, "und");
    mkvUint(&w, 0x88, 0);                      // FlagDefault
    mkvString(&w, 0x86, codecId);
    if (h264) {
        mkvId(&w, 0x63A2);
        mkvSize(&w, g_avcC.size());
        mkvPut(&w, g_avcC.data(), g_avcC.size());
    }
    mkvUint(&w, 0x83, 1);                      // TrackType = video
    mkvUint(&w, 0x23E383, frameDurNs);
    mkvId(&w, 0xE0);
    mkvSize(&w, videoPayload);
    mkvUint(&w, 0xB0, g_remuxW);
    mkvUint(&w, 0xBA, g_remuxH);
    mkvId(&w, 0x55B0);
    mkvSize(&w, colourPayload);
    mkvUint(&w, 0x55B1, 1);                    // MatrixCoefficients
    mkvUint(&w, 0x55B9, 1);                    // Range

    // ---- Clusters ---------------------------------------------------------
    for (size_t c = 0; c < nClusters; ++c) {
        const size_t s = clustStart[c];
        const size_t e = (c + 1 < nClusters) ? clustStart[c + 1] : n;

        size_t payload = uintElemSize(0xE7, ts[s]);
        for (size_t i = s; i < e; ++i) {
            payload += elemSize(0xA3, 4 + samples[i]);
        }

        mkvId(&w, 0x1F43B675);
        mkvSize(&w, payload);
        mkvUint(&w, 0xE7, ts[s]);

        for (size_t i = s; i < e; ++i) {
            const RemuxFrame& f = g_remuxFrames[i];
            const uint16_t rel = static_cast<uint16_t>(ts[i] - ts[s]);
            mkvId(&w, 0xA3);
            mkvSize(&w, 4 + samples[i]);

            const uint8_t track = 0x81;        // track 1, 1-byte vint
            const uint8_t relb[2] = {
                static_cast<uint8_t>(rel >> 8),
                static_cast<uint8_t>(rel)
            };
            const uint8_t flags = keys[i] ? 0x80 : 0x00;
            mkvPut(&w, &track, 1);
            mkvPut(&w, relb, 2);
            mkvPut(&w, &flags, 1);

            // This put is the frame itself; it stays in the .usm.  H.264
            // frames are reframed on read, so they are recorded as
            // transformed runs with a known output size.
            w.pendingPayload = true;
            w.pendingSrc = f.off;
            w.pendingKind = h264 ? 2 : 1;
            w.pendingSrcLen = h264 ? f.size : 0;
            mkvPut(&w, nullptr, samples[i]);
        }
    }

    // ---- Cues -------------------------------------------------------------
    mkvId(&w, 0x1C53BB6B);
    mkvSize(&w, cuesPayload);
    for (size_t c = 0; c < nClusters; ++c) {
        mkvId(&w, 0xBB);
        mkvSize(&w, 53);                              // CueTime + CueTrackPositions
        mkvUintFixed8(&w, 0xB3, ts[clustStart[c]]);   // CueTime
        mkvId(&w, 0xB7);
        mkvSize(&w, 27);
        mkvUint(&w, 0xF7, 1);                          // CueTrack
        mkvUintFixed8(&w, 0xF1, clusterPos[c]);        // CueClusterPosition
    }

    // ---- trailer ----------------------------------------------------------
    // A top-level Void element after the Segment, skipped by every Matroska
    // parser.  Its 16 payload bytes (the marker and the source size) are what
    // the on-disk cache layout used to carry; they are kept because the
    // element's length is part of the layout this format was validated with.
    mkvId(&w, 0xEC);
    mkvSize(&w, 16);
    static const char kMagic[8] = { 'U','S','M','R','E','F','0','0' };
    mkvPut(&w, kMagic, 8);
    uint8_t szb[8];
    {
        const uint64_t v = static_cast<uint64_t>(sourceSize);
        for (int i = 0; i < 8; ++i) szb[i] = static_cast<uint8_t>(v >> (8 * (7 - i)));
    }
    mkvPut(&w, szb, 8);

    // The run list has to describe the whole file exactly, and the last put
    // must not have been a payload marker that never saw its bytes.
    const bool ok = (w.pos == totalSize) && !w.pendingPayload;
    if (ok) {
        plan->size = totalSize;
        plan->ok   = true;
    } else {
        WriteLog("[USM] plan: size mismatch pos=%llu want=%llu\n",
                 (unsigned long long)w.pos, (unsigned long long)totalSize);
    }
    free(ts);
    free(keys);
    free(samples);
    free(clustStart);
    free(clusterPos);
    return ok;
}

struct UsmSource {
    bool    found;
    char    readPath[1024];     // path that opens and starts with "CRID"
    int64_t size;
};

// The USM source lookup lives further down and is reused here as-is: it
// already knows every spelling the engine hands out.
struct UsmSource;
static bool resolveUsmSource(const char* enginePath, UsmSource* out);

// ---------------------------------------------------------------------------
// Virtual movie files
//
// Nothing is ever written to the card.  The .mp4 the PV database
// names is reported as present when its .usm sibling is (GetEntryType), and
// when the engine opens it, it is handed a token instead of a file handle.
// Every read of that token is answered from the plan built by
// mkvBuildAndWrite(sourceSize, plan): the container metadata out of a small
// in-memory buffer, the video frames straight out of the .usm at their
// original offsets.  The only thing held in RAM is the Matroska scaffolding --
// a few hundred KB for a whole song -- never the video.
//
// The token is a value no fs handle can collide with.  That keeps it from
// naming somebody else's file if it ever reaches the fs service, but it does
// not make it safe to send: on the emulator an unknown handle comes back as a
// Result, under SaltySD it aborts the console inside its fs hook.  So every
// hook above the fs refuses to pass one on -- a token is answered here or not
// at all -- and the design no longer leans on that error.
//
// What a token names is an open movie, and a movie can be closed, or its slot
// recycled for the next one, while the engine's fetch thread is still reading
// it.  So a token does not hold the movie's handle directly: it names a
// reference-counted bundle, every read takes a reference for as long as it is
// inside the plan, and the handle is closed and the plan filed or freed only
// when the last reference goes.  A read for a token that is no longer live is
// refused, never passed on.
// ---------------------------------------------------------------------------
static const bool kVirtualMovieFiles = true;

// Slots are bookkeeping only: a token, its generation, and a pointer to the
// movie that is open.  What the movie *is* -- the file handle and the plan --
// lives in a separate, reference-counted object (VBundle), and that separation
// is the whole point: the handle is closed and the plan freed by whichever
// thread drops the last reference, which is the last read still in flight,
// never the close that happened earlier.
//
// That is what used to crash.  The engine plays a movie on its own fetch thread
// and closes it (or opens the next one, which recycled the slot) on another;
// the close used to shut the handle and free the plan right there, and the
// fetch thread's next read then went out to the real fs with a handle that no
// longer existed.  On the emulator that read fails; under SaltySD it aborts the
// console inside its fs hook, and the reports of 2026-11-04 are exactly that --
// the NuCachedSource2 thread dying inside nn::fs::ReadFile with no frame of
// ours above it, because the pass-through to the original is a tail call.
static const int kVFileSlots = 8;

// Token layout, in the low 32 bits (the engine only ever carries those): the
// slot index, then a generation that counts how many movies that slot has
// served.  The generation is what makes a stale token harmless -- after a slot
// is recycled it answers to a different token, and a read that arrives for the
// old one is refused instead of being served out of the next movie.
static const uint32_t kVFileIdxBits  = 3;      // 8 slots
static const uint32_t kVFileIdxMask  = (1u << kVFileIdxBits) - 1;
static const uint32_t kVFileGenShift = kVFileIdxBits;
static const uint32_t kVFileGenMask  = 0x1FFu; // 9 bits: 512 opens per slot

// One open .usm: the handle it is read through, the plan that describes the
// Matroska stream, and the identity of the source it was built from.
struct VBundle {
    UsmFile  src;
    VPlan    plan;
    // Live references: one for the slot that publishes it (or one per slot,
    // when two spellings of the same movie are open at once) plus one for every
    // read that is inside vfileReadAt at this moment.  std::atomic, not a
    // mutex: this build's libc lock words are not all real -- std::recursive_
    // mutex is a nop here -- but an atomic is ldxr/stxr and needs no libc at
    // all.  Nothing walks off while a reference is held.
    std::atomic<int32_t> refs;
    int64_t  usmSize;
    uint32_t headHash;
    char     usmPath[1024];   // canonical .usm, for the log and the plan cache
};

struct VFileSlot {
    VBundle* bundle;   // nullptr when the slot holds no movie
    uint64_t token;    // the token this slot answers to; 0 when free
    uint32_t gen;
    uint32_t used;
};

static VFileSlot g_vfiles[kVFileSlots];
static uint32_t  g_vfileUseCounter = 0;

// The table is touched from every thread that reads or closes a movie -- the
// media fetch thread included -- so its bookkeeping has to be atomic.  Every
// critical section below is a handful of assignments: no I/O, no allocation,
// nothing that can call back into the fs hooks, so the lock is never held
// across a call that could block or re-enter.
static std::atomic<uint32_t> g_vfileLock{ 0 };

static void vfileLock() {
    while (g_vfileLock.exchange(1, std::memory_order_acquire)) { }
}
static void vfileUnlock() {
    g_vfileLock.store(0, std::memory_order_release);
}

// One-entry plan cache.
//
// The engine opens a movie several times in a row and closes it in between --
// pv_9736 was opened eight times, four under rom:/mods/... and four under the
// ExlSD spelling of the same file, with a close after each serve.  Every one of
// those opens walked the whole 13 MB .usm block-by-block to rebuild a plan that
// had just been thrown away, which is the card reads the cache removes.  The
// plan of the movie just released is kept here instead, and on the next open of
// that same movie it is moved straight into the slot: the vectors move by
// pointer, so no payload is copied, nothing is parsed and nothing is read from
// the card.  Only the open handle is per-open, and the parse window is handed
// straight back.  The plan of the movie just released is kept here instead, and
// on the next open of that same movie it is moved straight into its bundle: the
// vectors move by pointer, so no payload is copied, nothing is parsed and
// nothing is read from the card.
//
// What identifies the movie is its content -- size plus a hash of its head --
// and not its name.  Names are not unique here: the log shows one .usm being
// reached as rom:/mods/... and as its ExlSD physical path in the same load, and
// a name-keyed entry was thrown away and rebuilt on every switch.  The plan is
// nothing but offsets into that file, so the test has to be strong enough that
// a wrong match is impossible; size plus the CRID head plus the first 64 KB of
// payload leaves two distinct movies sharing it out of the question.
//
// A hit moves the plan *out* of here, and the teardown of a bundle moves it
// back, so the same plan is never in two places at once and no pointer to it can
// go stale.  A miss frees the held entry before building, so nothing
// accumulates: what is in memory is the plan of each open movie plus this one
// held between opens, which for the engine's one-movie-at-a-time pattern is a
// single plan.  A bundle that is torn down -- closed, or retired by a slot
// recycle -- is filed through here the same way, so a plan is never dropped
// just because its movie was still being read when its slot went away.
struct PlanCacheEntry {
    bool     have;
    char     key[1024];   // spelling it was built from; for the log only
    int64_t  size;        // the source's size
    uint32_t headHash;    // and the hash of its head: the identity test
    VPlan    plan;
};

static PlanCacheEntry g_planCache = {};

// The cache is shared state like the slot table -- a movie is opened on one
// thread while another still reads the previous one -- so every touch of it is
// under the same lock.  This is the unlocked body for the callers that already
// hold it.
static void planCacheDropLocked() {
    if (!g_planCache.have) return;
    std::vector<VPiece>().swap(g_planCache.plan.pieces);
    std::vector<uint8_t>().swap(g_planCache.plan.meta);
    g_planCache.plan.ok = false;
    g_planCache.plan.size = 0;
    g_planCache.plan.cursor = 0;
    g_planCache.key[0] = '\0';
    g_planCache.size = 0;
    g_planCache.headHash = 0;
    g_planCache.have = false;
}

static void planCacheDrop() {
    vfileLock();
    planCacheDropLocked();
    vfileUnlock();
}

// The token is judged on its low 32 bits alone.  That is deliberate: if this
// SDK's FileHandle ever turns out to be 32 bits wide rather than the 8 the
// header here declares, only the low half survives the round trip through the
// engine, and the token still has to be recognised.  A real fs handle is a
// small index in a session table, so nothing in this range can collide.
static const uint32_t kVFileTokenBase = 0xE1D10000u;
static const uint32_t kVFileTokenMask = 0xFFFFF000u;

// Ours, whatever state the slot is in -- including the slot being gone, which
// is what a read that arrives after the movie was closed looks like.
static bool vfileIsOurToken(uint64_t h) {
    const uint32_t low = static_cast<uint32_t>(h & 0xFFFFFFFFull);
    return (low & kVFileTokenMask) == kVFileTokenBase;
}

// Reads are answered against a bundle, never against a slot: a reader finds the
// movie under the lock, takes a reference to it, and hands the reference back
// when it is done.  The handle and the plan therefore stay alive for as long as
// any read is inside them, whatever the engine does with the token meanwhile.
// A token that names nothing -- the movie was closed, or its slot was recycled
// -- comes back as nullptr, and the caller refuses it rather than passing it to
// the fs.
static VBundle* vfileAcquire(uint64_t h) {
    if (!vfileIsOurToken(h)) return nullptr;
    const uint32_t low = static_cast<uint32_t>(h & 0xFFFFFFFFull);
    const uint32_t idx = low & kVFileIdxMask;
    if (idx >= static_cast<uint32_t>(kVFileSlots)) return nullptr;

    VFileSlot* s = &g_vfiles[idx];
    vfileLock();
    // The token, not the slot index, is the identity: after a recycle the same
    // slot answers to a different token, and a read that arrives for the old
    // one has to be refused, not served out of the movie that took its place.
    VBundle* b = (s->token == h) ? s->bundle : nullptr;
    if (b) b->refs.fetch_add(1, std::memory_order_relaxed);
    vfileUnlock();
    return b;
}

static void vfileDestroy(VBundle* b);

// Hand a reference back.  The last one is what closes the movie -- the handle
// is shut and the plan filed or freed by whichever thread drops the last
// reference, which is the last read still in flight.
static void vfileDrop(VBundle* b) {
    if (!b) return;
    if (b->refs.fetch_sub(1, std::memory_order_acq_rel) == 1) vfileDestroy(b);
}

// Close the movie a token names.  Only the slot is retired here; the handle
// waits for the reads that are still in flight, which is the difference between
// this and what used to be a straight close under the fetch thread's feet.
static void vfileRetire(uint64_t h) {
    if (!vfileIsOurToken(h)) return;
    const uint32_t idx = static_cast<uint32_t>(h & 0xFFFFFFFFull) & kVFileIdxMask;
    if (idx >= static_cast<uint32_t>(kVFileSlots)) return;

    VBundle* b = nullptr;
    VFileSlot* s = &g_vfiles[idx];
    vfileLock();
    if (s->token == h) {
        b = s->bundle;
        s->bundle = nullptr;
        s->token  = 0;
    }
    vfileUnlock();
    vfileDrop(b);
}

// Publish a bundle as a token, recycling the oldest slot when every one is
// busy.  A recycled movie stops answering its token immediately and is torn
// down once its own reads are done, so a movie being read can never have its
// file pulled out from under it -- and with the engine holding two spellings of
// one movie at once, recycling is routine rather than an emergency.
//
// The bundle arrives with one reference (the caller's) and leaves with the
// slot's on top of it; the caller drops its own after this returns.
static uint64_t vfileInstall(VBundle* b) {
    vfileLock();

    VFileSlot* slot = nullptr;
    for (int i = 0; i < kVFileSlots; ++i) {
        if (g_vfiles[i].bundle == nullptr && g_vfiles[i].token == 0) {
            slot = &g_vfiles[i];
            break;
        }
    }
    VBundle* retired = nullptr;
    if (!slot) {
        slot = &g_vfiles[0];
        for (int i = 1; i < kVFileSlots; ++i) {
            if (g_vfiles[i].used < slot->used) slot = &g_vfiles[i];
        }
        retired = slot->bundle;
        WriteLog("[USM] vfile: every slot busy, retiring the oldest\n");
    }

    slot->gen  = (slot->gen + 1) & kVFileGenMask;
    slot->used = ++g_vfileUseCounter;
    slot->bundle = b;
    b->refs.fetch_add(1, std::memory_order_relaxed);
    slot->token = static_cast<uint64_t>(kVFileTokenBase) |
                  (static_cast<uint64_t>(slot->gen) << kVFileGenShift) |
                  static_cast<uint64_t>(slot - g_vfiles);
    const uint64_t token = slot->token;
    vfileUnlock();

    vfileDrop(retired);
    return token;
}

// Everything a movie holds, given up at last.  A plan worth keeping is filed
// for the next open of the same content; the handle and the parse window go for
// real, so a session of movies accumulates nothing.
static void vfileDestroy(VBundle* b) {
    // Hand the plan to the one-entry cache rather than freeing it: the next
    // open of this same .usm is almost always moments away, from the engine
    // itself, and it is cheaper to hold these vectors than to walk the file
    // again.  A plan that never got built (ok == false) is not worth keeping.
    if (b->plan.ok && b->usmPath[0] != '\0' && b->headHash != 0) {
        vfileLock();
        planCacheDropLocked();
        g_planCache.plan = std::move(b->plan);
        std::snprintf(g_planCache.key, sizeof(g_planCache.key), "%s", b->usmPath);
        g_planCache.size     = b->usmSize;
        g_planCache.headHash = b->headHash;
        g_planCache.have     = true;
        vfileUnlock();
        WriteLog("[USM] vfile: caching plan for %s (%llu bytes, %zu pieces)\n",
                 b->usmPath, (unsigned long long)g_planCache.plan.size,
                 g_planCache.plan.pieces.size());
    }
    usmFileClose(&b->src);
    // swap, not shrink_to_fit: this build has no exceptions to catch.  The move
    // above left these empty already, so this is the free path for a plan that
    // was not cacheable.
    std::vector<VPiece>().swap(b->plan.pieces);
    std::vector<uint8_t>().swap(b->plan.meta);
    free(b);
}

// Identity of a movie source, taken from the file itself: FNV-1a over its first
// 64 KB.  The engine names one .usm more than one way -- rom:/mods/... and the
// ExlSD physical path in the same load -- so a name cannot be the key, and this
// is what a cached plan is checked against.  The head carries the CRID header,
// the USM header (codecs, resolution, durations) and the first payloads, so
// with the size checked as well no two different movies share it.  0 means the
// read failed and is never cached.
static uint32_t vfileHeadHash(UsmFile* f) {
    const size_t kHead = 64 * 1024;
    size_t want = kHead;
    if (static_cast<int64_t>(want) > f->size) want = static_cast<size_t>(f->size);
    if (want == 0) return 0;

    // Heap, not stack: this runs on whatever thread the engine opens files
    // from, and 64 KB of stack in a movie thread is not something to gamble on.
    uint8_t* buf = static_cast<uint8_t*>(malloc(want));
    if (!buf) return 0;
    const Result r = nn::fs::ReadFile(f->h, 0, buf, want);
    if (R_FAILED(r)) {
        free(buf);
        return 0;
    }
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < want; ++i) {
        h ^= buf[i];
        h *= 16777619u;
    }
    free(buf);
    return h;
}

// Builds the plan for the file `f` already has open.  The caller owns that
// handle: on failure it is left closed here, on success it travels into the
// bundle because reads are served through it.
static bool vfileBuildPlan(VBundle* b, UsmFile* f, const char* usmPath) {
    if (!parseUsmFileStream(f)) {
        WriteLog("[USM] vfile: parse failed %s\n", usmPath);
        usmFileClose(f);
        g_remuxFrames.clear();
        return false;
    }
    WriteLog("[USM] vfile: plan %s frames=%zu codec=%d %ux%u\n",
             usmPath, g_remuxFrames.size(), g_remuxCodec, g_remuxW, g_remuxH);
    const bool ok = mkvBuildAndWrite(f, &b->plan);
    g_remuxFrames.clear();
    if (!ok) {
        usmFileClose(f);
        return false;
    }
    WriteLog("[USM] vfile: plan ok size=%llu pieces=%zu meta=%zu\n",
             (unsigned long long)b->plan.size, b->plan.pieces.size(),
             b->plan.meta.size());
    return true;
}

static bool vfileReadAt(VBundle* b, uint64_t off, void* dst, size_t n) {
    if (n == 0) return true;
    if (off > b->plan.size || n > b->plan.size - off) return false;

    uint8_t* out = static_cast<uint8_t*>(dst);
    const VPiece* p = b->plan.pieces.data();
    const size_t cnt = b->plan.pieces.size();

    size_t i = b->plan.cursor;
    if (i >= cnt || p[i].vOff > off) {
        size_t lo = 0, hi = cnt;
        while (lo < hi) {
            const size_t mid = (lo + hi) / 2;
            if (p[mid].vOff + p[mid].len <= off) lo = mid + 1;
            else hi = mid;
        }
        i = lo;
    }

    while (n > 0) {
        if (i >= cnt) return false;
        const VPiece& q = p[i];
        if (off >= q.vOff + q.len) { ++i; continue; }
        const size_t delta = static_cast<size_t>(off - q.vOff);
        size_t take = q.len - delta;
        if (take > n) take = n;
        if (q.kind == 0) {
            std::memcpy(out, b->plan.meta.data() + q.a + delta, take);
        } else if (q.kind == 2) {
            // H.264 frame: walk the Annex-B NALs of the source frame and copy
            // the [delta, delta+take) slice of its length-prefixed form.  Only
            // the requested bytes are produced, so nothing is cached and two
            // readers can use the same bundle at once.
            uint8_t* const src = static_cast<uint8_t*>(malloc(q.srcLen));
            if (src == nullptr || !usmFileRead(&b->src, q.a, src, q.srcLen)) {
                free(src);
                return false;
            }
            const size_t wantLo = delta;
            const size_t wantHi = delta + take;
            size_t vpos = 0, i2 = 0, sc = 0;
            while (i2 + 3 <= q.srcLen) {
                if (!nalStart(src, q.srcLen, i2, &sc)) { ++i2; continue; }
                const size_t nal = i2 + sc;
                size_t j = nal + 1, nsc = 0;
                while (j + 3 <= q.srcLen && !nalStart(src, q.srcLen, j, &nsc)) ++j;
                const size_t len = j - nal;
                if (len > 0) {
                    // The length prefix and the NAL body are two runs in the
                    // output; copy whichever part of each the read covers.
                    const uint8_t pre[4] = {
                        static_cast<uint8_t>(len >> 24),
                        static_cast<uint8_t>(len >> 16),
                        static_cast<uint8_t>(len >> 8),
                        static_cast<uint8_t>(len)
                    };
                    for (int part = 0; part < 2; ++part) {
                        const uint8_t* const s = part == 0 ? pre : src + nal;
                        const size_t plen = part == 0 ? 4 : len;
                        const size_t lo = vpos + (part == 0 ? 0 : 4);
                        const size_t hi = lo + plen;
                        if (hi > wantLo && lo < wantHi) {
                            const size_t from = (wantLo > lo) ? wantLo - lo : 0;
                            const size_t to = (wantHi < hi) ? wantHi - lo : plen;
                            std::memcpy(out + (lo + from - wantLo), s + from, to - from);
                        }
                    }
                    vpos += 4 + len;
                }
                i2 = j;
            }
            free(src);
        } else if (!usmFileRead(&b->src, q.a + delta, out, take)) {
            WriteLog("[USM] vfile: source read failed at %llu+%zu\n",
                     (unsigned long long)(q.a + delta), take);
            return false;
        }
        out += take;
        off += take;
        n   -= take;
    }
    // A hint, not state: two readers sharing a bundle may write it in any
    // order, and a wrong cursor only costs the binary search below.
    b->plan.cursor = i;
    return true;
}

static bool pathHasExt(const char* path, const char* ext) {
    const size_t len = std::strlen(path);
    return len >= 5 && path[len - 4] == '.' &&
           std::memcmp(path + len - 4, ext, 4) == 0;
}

// ---- the fs side ----------------------------------------------------------
// The trampolines live in FsHooks.cpp: it owns every nn::fs address and passes
// its own original in as `orig`.  The signatures are the SDK's and must match
// exactly -- nn::fs::FileHandle is an 8-byte struct passed by value in one
// register, and ReadOption is a reference (a pointer).

// The plugin's own file access, through FsHooks' originals (g_rawFs, declared
// with the stream helpers above).
//
// This plugin is not linked against libnx: its own nn::fs calls resolve through
// exlaunch's reloc table to the *game's* functions -- the very addresses these
// hooks are installed on.  So the moment a hook tries to find the .usm behind a
// name, its own probing re-enters the hook, with the same missing .mp4, and
// resolves to it again: an unbounded chain of nested probes, one identical
// failed OpenFile per level, until the thread's stack is gone.  The diagnosing
// build showed it as a hundred and twenty identical "vf open ... r=00000202"
// lines and then silence.
//
// That recursion used to be held off with a depth counter, and a *global*
// counter was the wrong shape for it.  It does not only fence in our own call
// chain: while one thread is inside a probe, every other thread that reaches
// one of these hooks is handed `orig` as well -- the movie task's own fetch
// thread included.  A read of a token file that takes that path is passed to
// the real nn::fs::ReadFile with a handle the fs service has never issued, and
// that is not the harmless error the token design assumed (see the note on the
// token): on the emulator the read fails, but under SaltySD the console aborts
// inside its fs hook -- three of the four reports from the 2026-11-03 hardware
// run are exactly that, the fetch thread dying at saltysd_core+0x574 with
// nn::fs::ReadFile above it and no frame of ours in between, because the
// pass-through to the original is a tail call.
//
// So the recursion is removed at its source instead: FsHooks hands over the
// originals it saved, everything in this file that opens or measures a file
// goes straight to them, and the depth counter is gone.  Nothing here re-enters
// a hook and no thread's call is ever suppressed.
void SetRawFs(RawFs const& fs) { g_rawFs = fs; }

// What a read of a token that cannot be served comes back as.  The engine only
// checks the Result, and any failure ends the playback cleanly.
static const uint32_t kResVFileRead = 0x00000202;

// The 1 MB window is a builder's scratch: the plan is made by walking the .usm
// through it, and once the plan exists reads go straight from the file into the
// caller's buffer at the offset asked for (usmFileRead above).  So the window is
// given back as soon as the plan is built and only the handle travels on with
// the bundle: a served movie costs its plan and its handle, nothing per read.
// Should anything ever ask for a window this file no longer has, usmFileEnsure
// refuses (want < n) instead of reading through a null pointer.
static void usmFileKeepHandleOnly(UsmFile* f) {
    if (f->win) free(f->win);
    f->win = nullptr;
    f->cap = 0;
    f->pos = 0;
    f->len = 0;
}

uint32_t vfileOpenFileImpl(VOpenFileFn orig, void* outHandle,
                                  const char* path, int mode) {
    const uint32_t r = orig(outHandle, path, mode);
    if (R_SUCCEEDED(r)) return r;                 // a real file: never ours
    if (!kVirtualMovieFiles || !outHandle || !path) return r;
    // Never answer an open-for-write with a read-only illusion.
    if (mode & (2 | 4)) return r;                 // Write / Append
    if (!pathHasExt(path, ".mp4")) return r;

    // Find the USM this name stands for.  resolveUsmSource knows every
    // spelling the engine hands out (scheme-less, rom:, physical) and checks
    // the CRID magic, so a real .mp4 or one of our own old caches -- neither of
    // which this branch is reached for anyway -- cannot be mistaken for a
    // source here.
    UsmSource src;
    if (!resolveUsmSource(path, &src)) {
        WriteLog("[USM] vfile: no USM behind %s\n", path);
        return r;
    }

    // Open for reads first: a cached plan still reads its payload out of the
    // file, and this handle is what identifies the source below.
    UsmFile f;
    if (!usmFileOpen(src.readPath, &f)) {
        WriteLog("[USM] vfile: cannot open %s\n", src.readPath);
        return r;
    }
    const int64_t  size     = f.size;
    const uint32_t headHash = vfileHeadHash(&f);

    // Is this movie already open under another name?  The engine reaches one
    // .usm as rom:/mods/... and as its ExlSD physical path in the same load and
    // can hold both at once, and identity is the content, not the name.  It is
    // safe to share because nothing in a bundle is per-reader: reads are
    // positional and go straight to the file, and the plan is read-only once
    // built.  One handle and one plan then answer both spellings.
    VBundle* b = nullptr;
    if (headHash != 0) {
        vfileLock();
        for (int i = 0; i < kVFileSlots; ++i) {
            VBundle* o = g_vfiles[i].bundle;
            if (o && o->headHash == headHash && o->usmSize == size) {
                o->refs.fetch_add(1, std::memory_order_relaxed);
                b = o;
                break;
            }
        }
        vfileUnlock();
    }

    if (b) {
        WriteLog("[USM] vfile: %s shares the open %s\n", path, b->usmPath);
        usmFileClose(&f);                 // our second handle is not needed
    } else {
        b = static_cast<VBundle*>(malloc(sizeof(VBundle)));
        if (!b) {
            usmFileClose(&f);
            return r;
        }
        new (b) VBundle();                // the plan's vectors need constructing
        b->src      = f;
        b->usmSize  = size;
        b->headHash = headHash;
        std::snprintf(b->usmPath, sizeof(b->usmPath), "%s", src.readPath);
        b->refs.store(1, std::memory_order_relaxed);   // the caller's reference

        // The same movie as the plan still held from the last time it was open?
        // Move it in and skip the parse entirely.  Identity is the content --
        // the size and a hash of the head -- not the name: the log shows the
        // engine reaching one .usm as both rom:/mods/... and its ExlSD physical
        // path, and a name-keyed entry was rebuilt on every switch.
        bool cached = false;
        if (headHash != 0) {
            vfileLock();
            cached = g_planCache.have && g_planCache.size == size &&
                     g_planCache.headHash == headHash;
            if (cached) {
                b->plan = std::move(g_planCache.plan);
                g_planCache.have     = false;
                g_planCache.key[0]   = '\0';
                g_planCache.size     = 0;
                g_planCache.headHash = 0;
            }
            vfileUnlock();
        }
        if (cached) {
            WriteLog("[USM] vfile: plan cache hit %s size=%llu pieces=%zu\n",
                     src.readPath, (unsigned long long)b->plan.size,
                     b->plan.pieces.size());
        } else {
            // Nothing here is reusable, so let the held entry go before the new
            // plan is built: that is what stops a plan per movie served from
            // piling up over a session.
            planCacheDrop();
            if (!vfileBuildPlan(b, &b->src, src.readPath)) {
                // The file is closed already; the vectors may hold a partial
                // plan, so let them go with the bundle.
                std::vector<VPiece>().swap(b->plan.pieces);
                std::vector<uint8_t>().swap(b->plan.meta);
                free(b);
                return r;
            }
        }
    }

    usmFileKeepHandleOnly(&b->src);
    const uint64_t token = vfileInstall(b);
    std::memset(outHandle, 0, sizeof(uint64_t));
    std::memcpy(outHandle, &token, sizeof(uint64_t));
    WriteLog("[USM] vfile: serving %s out of %s token=0x%llx size=%llu\n",
             path, b->usmPath, (unsigned long long)token,
             (unsigned long long)b->plan.size);
    vfileDrop(b);                         // the slot holds it from here on
    return 0;
}

uint32_t vfileGetEntryTypeImpl(VEntryTypeFn orig, void* outType,
                                      const char* path) {
    const uint32_t r = orig(outType, path);
    if (R_SUCCEEDED(r)) return r;
    if (!kVirtualMovieFiles || !outType || !path) return r;
    if (!pathHasExt(path, ".mp4")) return r;

    UsmSource src;
    if (!resolveUsmSource(path, &src)) return r;

    // Ask the same question about the file that really exists and keep
    // whatever answer it fills in: the .usm is a File, which is exactly what
    // the .mp4 has to be.  Nothing is written to outType by hand -- its width
    // and value belong to the SDK, and this is its own answer.
    const uint32_t r2 = orig(outType, src.readPath);
    if (R_SUCCEEDED(r2)) {
        WriteLog("[USM] vfile: %s exists as %s\n", path, src.readPath);
    }
    return r2;
}

uint32_t vfileGetFileSizeImpl(VGetSizeFn orig, int64_t* out,
                                     uint64_t handle) {
    VBundle* b = vfileAcquire(handle);
    if (b) {
        if (out) *out = static_cast<int64_t>(b->plan.size);
        vfileDrop(b);
        return 0;
    }
    if (vfileIsOurToken(handle)) return kResVFileRead;
    return orig(out, handle);
}

uint32_t vfileReadImpl(VReadFn5 orig, uint64_t handle, int64_t pos,
                              void* buf, size_t size, const void* opt) {
    VBundle* b = vfileAcquire(handle);
    if (!b) {
        if (vfileIsOurToken(handle)) return kResVFileRead;
        return orig(handle, pos, buf, size, opt);
    }
    if (pos < 0 || !buf) { vfileDrop(b); return kResVFileRead; }
    // Same rule as the counted variant below: a request past EOF is served
    // with the bytes that are there, not refused.  Whatever the caller's
    // buffer holds past that point is left alone, which is what a real file
    // leaves there too -- the engine played the identical byte stream from a
    // cache file on disk, so this is the behaviour it is used to.
    const uint64_t off = static_cast<uint64_t>(pos);
    if (off >= b->plan.size) { vfileDrop(b); return 0; }
    size_t want = size;
    if (want > b->plan.size - off) want = static_cast<size_t>(b->plan.size - off);
    const bool ok = vfileReadAt(b, off, buf, want);
    vfileDrop(b);
    return ok ? 0 : kResVFileRead;
}

uint32_t vfileReadImpl6(VReadFn6 orig, size_t* bytesRead, uint64_t handle,
                               int64_t pos, void* buf, size_t size) {
    VBundle* b = vfileAcquire(handle);
    if (!b) {
        if (vfileIsOurToken(handle)) {
            if (bytesRead) *bytesRead = 0;
            return kResVFileRead;
        }
        return orig(bytesRead, handle, pos, buf, size);
    }
    if (pos < 0 || !buf) { vfileDrop(b); return kResVFileRead; }
    // A read that runs off the end is a short read, not a failure.  The
    // demuxer's index pass reads the Cues at the very end of the file and its
    // last request straddles EOF by design (pos=plan.size-264537, size=65536);
    // a real file answers that with the 23393 bytes it has, and the plan this
    // serves was written by the same code path that wrote the cache file the
    // engine plays happily.  Returning 0x202 there is what left the PV on the
    // loading screen with the extractor already past every other step.
    const uint64_t off = static_cast<uint64_t>(pos);
    if (off >= b->plan.size) {
        if (bytesRead) *bytesRead = 0;
        vfileDrop(b);
        return 0;
    }
    size_t want = size;
    if (want > b->plan.size - off) want = static_cast<size_t>(b->plan.size - off);
    const bool ok = vfileReadAt(b, off, buf, want);
    if (bytesRead) *bytesRead = ok ? want : 0;
    vfileDrop(b);
    return ok ? 0 : kResVFileRead;
}

void vfileCloseImpl(VCloseFn orig, uint64_t handle) {
    // A token is always one of ours, whatever state its slot is in -- a token
    // whose slot is already gone is the engine's fetch thread outliving the
    // close by a read or two, and the fs service has never heard of it.  Either
    // way it is answered here; neither reaches the fs.
    if (vfileIsOurToken(handle)) {
        WriteLog("[USM] vfile: close token 0x%llx\n", (unsigned long long)handle);
        // The slot stops answering now; the handle is shut and the plan filed
        // once the reads still in flight for it are done.
        vfileRetire(handle);
        return;
    }
    orig(handle);
}

uint32_t vfileFlushImpl(VFlushFn orig, uint64_t handle) {
    if (vfileIsOurToken(handle)) return 0;
    return orig(handle);
}


// ---------------------------------------------------------------------------
// Source lookup: find the .usm a name stands for
// ---------------------------------------------------------------------------
static bool fileSizeAt(const char* path, int64_t* size, uint32_t* magic) {
    if (!g_rawFs.open || !g_rawFs.getSize || !g_rawFs.close) return false;
    nn::fs::FileHandle h;
    if (R_FAILED(g_rawFs.open(&h, path, nn::fs::OpenMode_Read))) return false;

    int64_t sz = 0;
    Result r = g_rawFs.getSize(&sz, rawHandle(h));
    uint8_t b[4] = { 0, 0, 0, 0 };
    if (R_SUCCEEDED(r)) r = nn::fs::ReadFile(h, 0, b, 4);
    g_rawFs.close(rawHandle(h));
    if (R_FAILED(r) || sz <= 0) return false;

    if (size)  *size  = sz;
    if (magic) *magic = rdBE32(b);
    return true;
}


// ---------------------------------------------------------------------------
// Source resolution: the spellings the engine hands out
// ---------------------------------------------------------------------------
static bool probeUsmVariant(const char* p, UsmSource* out) {
    int64_t size = 0;
    uint32_t magic = 0;
    if (!fileSizeAt(p, &size, &magic)) return false;
    if (magic != kMagicCrid) return false;
    std::snprintf(out->readPath, sizeof(out->readPath), "%s", p);
    out->size = size;
    return true;
}

static bool probeUsmAnyPath(const char* p, UsmSource* out) {
    if (probeUsmVariant(p, out)) return true;

    char mounted[1024];
    if (makeMountedPath(p, mounted, sizeof(mounted))) {
        if (probeUsmVariant(mounted, out)) return true;
        char phys[1024];
        if (toPhysicalPath(mounted, phys, sizeof(phys)) &&
            probeUsmVariant(phys, out)) return true;
    } else {
        char phys[1024];
        if (toPhysicalPath(p, phys, sizeof(phys)) &&
            probeUsmVariant(phys, out)) return true;
    }
    return false;
}

static bool resolveUsmSource(const char* enginePath, UsmSource* out) {
    std::memset(out, 0, sizeof(*out));

    // The requested file itself, when it is a USM named .usm or a mod that
    // replaced the movie without changing its name.
    if (probeUsmAnyPath(enginePath, out)) {
        out->found = true;
        return true;
    }

    // A sibling .usm (game asked for pv_x.mp4, the mod ships pv_x.usm).  This
    // only works when the requested name has a three-character extension,
    // because the engine's path object is rewritten in place.
    const size_t len = std::strlen(enginePath);
    if (len >= 5 && enginePath[len - 4] == '.' &&
        !(enginePath[len - 3] == 'u' && enginePath[len - 2] == 's' &&
          enginePath[len - 1] == 'm')) {
        char sibling[1024];
        std::memcpy(sibling, enginePath, len + 1);
        sibling[len - 3] = 'u';
        sibling[len - 2] = 's';
        sibling[len - 1] = 'm';
        if (probeUsmAnyPath(sibling, out)) {
            out->found = true;
            return true;
        }
    }
    return false;
}


// (No directory scan here any more.  Walking the card from init() and probing
// from the hooks both crashed the game; what actually makes the second boot
// work is UsmResolvePathHook resolving the *other* spelling of the name, which
// leaves the engine with a real path and nothing for us to fake.)

// ---------------------------------------------------------------------------
// Open-time entry point: the engine is asking for a movie, and the request is
// a .usm.  Nothing is converted and nothing is written; all that is left to do
// is put the movie under the ".mp4" name -- diva gates the whole PV movie path
// on that extension (0x6fa7a0) before it builds the extractor -- which is the
// name the fs hooks further up serve out of the .usm.
//
// Returns true only when the path object was actually rewritten.
// ---------------------------------------------------------------------------
static bool tryServeUsmMovie(const void* pathObj, const char* enginePath) {
    if (!enginePath || !isUsmPath(enginePath)) return false;
    return replaceEnginePathExt(pathObj, "mp4");
}

// ---------------------------------------------------------------------------
// PC name rule: a PV database entry names the movie as .mp4, the pack ships
// the same name as .usm.  Take the PC build's behaviour.
//
// The PC build never has this problem because its PV loader strips the
// extension from every entry in a PV's movie list and appends ".usm"
// (DivaMegaMix FUN_14025ad30), and CRI Sofdec2 parses the result.  The Switch
// build has no such step and no USM parser, so a pack that ships
// rom/movie/pv_x.usm while its mod_pv_db.txt still says rom/movie/pv_x.mp4
// asks for a file that does not exist.
//
// That is not a soft failure.  Every movie list entry is routed through main
// 0x001f3f70 -- `ResolveFilePath(name, name)`, which resolves the scheme-less
// database name against the mounted romfs search roots and returns 0 when no
// root holds the file (per component it calls the entry-type query, 0x379d0).
// The PV loader skips entries whose resolution returns 0 (0x00190fac `tbz`),
// so no TaskMovie is ever built and the PV sits on the loading screen without
// the OpenFile hook ever seeing the path.
//
// So the name is fixed here, before the resolver: resolve the .mp4 first and
// leave it alone when it succeeds -- that single call is what protects stock
// PVs and a mod's own .mp4.  Only when that fails (the requested file
// genuinely is not there) is the extension switched, in place, to .usm and
// resolved again.  If the .usm resolves, its installed path is what the caller
// receives, exactly as if the database had named .usm to begin with; our
// OpenFile hook then puts it back under the .mp4 name the player wants.
//
// No nn::fs probing of our own: the engine's resolver is the oracle, so
// scheme-less names, mod search roots and the emulator's relative paths all
// keep working without this code guessing at any of them.
// ---------------------------------------------------------------------------
static const bool kPreferUsmName = true;

// Trampoline hook at diva's ResolveFilePath wrapper (0x001F3F70)
HOOK_DEFINE_TRAMPOLINE(UsmResolvePathHook) {
    static uint64_t Callback(const void* path_obj) {
        const uint64_t resolved = Orig(path_obj);

        // Anything the engine resolved on its own is left alone: that is a
        // real file -- a stock movie, or a mod's own .mp4 -- and it wins.
        if (!kPreferUsmName || !kUsmRemuxEnabled) return resolved;
        if ((resolved & 1) || !path_obj) return resolved;

        char path[1024];
        if (!decodeEnginePath(path_obj, path, sizeof(path))) return resolved;
        const size_t len = std::strlen(path);
        if (len < 5 || path[len - 4] != '.') return resolved;

        const bool mp4Name = std::memcmp(path + len - 4, ".mp4", 4) == 0;
        const bool usmName = std::memcmp(path + len - 4, ".usm", 4) == 0;
        if (!mp4Name && !usmName) return resolved;

        // Ask for the *other* spelling of the same movie and let the engine
        // resolve it for real.  Both directions are the same rule: a mod may
        // ship pv_x.usm where the database says pv_x.mp4, and the reverse is
        // what a converted movie looks like afterwards -- the .usm is deleted
        // and the cache we wrote sits beside it as pv_x.mp4, a real file that
        // is in the romfs index on every later boot.
        //
        // Nothing is ever reported as resolved that the engine did not resolve
        // itself.  Returning success for a name that is not on the card leaves
        // the caller with a path it never filled, and that is a crash, not a
        // saved PV entry.
        const char* other = mp4Name ? "usm" : "mp4";
        if (!replaceEnginePathExt(path_obj, other)) return resolved;
        const uint64_t swapped = Orig(path_obj);
        if (swapped & 1) {
            WriteLog("[USM] name rule: %s -> .%s\n", path, other);
            return swapped;
        }

        replaceEnginePathExt(path_obj, mp4Name ? "mp4" : "usm");   // restore
        return resolved;
    }
};

// ---------------------------------------------------------------------------
// Trampoline hook at TaskMovie::OpenFile (0x00439FD0)
// ---------------------------------------------------------------------------
HOOK_DEFINE_TRAMPOLINE(UsmMovieOpenFileHook) {
    static uint64_t Callback(uintptr_t this_ptr, const char* path_arg) {
        // 2D / no-MV mode: block movie playback exactly as Mode2DNoMV used to.
        // This hook was moved here from Mode2DNoMV so a single trampoline owns
        // 0x00439FD0; the policy still comes from ShouldBlockMovie().
        if (Mode2DNoMV::ShouldBlockMovie()) {
            WriteLog("[USM] OPEN blocked (noMV)\n");
            return 0;
        }

        // path_arg is the engine's length-tagged string object, NOT a C string.
        // Decode it before doing anything else (see decodeEnginePath).
        //
        // Anything that is not literally a .usm request passes straight
        // through: no filesystem probing, no path rewriting, no state
        // juggling.  An ordinary .mp4/.wmv open must reach Orig with the
        // engine exactly as it found it -- including a real .mp4 sitting
        // beside a .usm, and a stock movie.  A pv_x.mp4 that does not exist
        // at all is not handled here either: the fs hooks answer that name
        // out of the .usm sibling whenever the engine opens it.
        char path[1024];
        if (decodeEnginePath(path_arg, path, sizeof(path)) &&
            kUsmRemuxEnabled && isUsmPath(path)) {
            WriteLog("[USM] OPEN path=%s\n", path);
            if (tryServeUsmMovie(path_arg, path)) {
                WriteLog("[USM] serve: virtual, renamed to .mp4\n");
            } else {
                WriteLog("[USM] serve: rename to .mp4 failed\n");
            }
        }
        return Orig(this_ptr, path_arg);
    }
};

// ---------------------------------------------------------------------------
// Hook FUN_0043A310 (0x0043A310) -- TaskMovie::IsReady.
//
// Moved here from Mode2DNoMV so a single trampoline owns this address; the
// no-MV policy still comes from Mode2DNoMV::ShouldBlockMovie(), and in no-MV
// mode the answer is short-circuited to "ready" so gameplay never waits on a
// movie.  Everything else is the engine's own answer.
// ---------------------------------------------------------------------------
HOOK_DEFINE_TRAMPOLINE(UsmMovieReadyHook) {
    static uint64_t Callback(uintptr_t this_ptr) {
        if (Mode2DNoMV::ShouldBlockMovie()) return 1;
        return Orig(this_ptr);
    }
};



// ---------------------------------------------------------------------------
// movie::NnExtractor format fix-up (Subsdk1 = the `multimedia` module)
//
// diva always builds movie::Extractor with ContainerType 1 (MP4): main+0x6fa190
// loads it from [movie+0xd0], stored as 1 at main+0x6fa840.  The ctor copies it
// into NnExtractor+0x20 via the ExtractorConfig, and subsdk1's SetDataSource
// validator (0x127b60) rejects the file at 0x127bbc unless that field equals
// the container class it detected from the file mime: 1=mp4, 2=matroska,
// 3=webm, 4=ogg.  The virtual file is a WebM under a .mp4 name, so it detects
// as 3; without this correction the validator returns 0x4000 and diva never
// reaches CountTracks / GetTrackFormat / SelectTrack.  The only writers of the
// field are the two NnExtractor ctors and the only reader is that compare, so
// correcting it here -- right after the real format is known, before the
// compare -- is safe.
//
// The prologues are from this build, so a different multimedia module (or a
// different game version) refuses to be patched instead of crashing.
// ---------------------------------------------------------------------------
namespace {
    using NnGetFileFormatFn = int (*)(void* self, void* out);
    // movie::NnMediaData::FindString -- the movie-level metadata lookup.
    // movie::MediaData::FindString tail-calls into it, so the recorded return
    // address is the game's real call site, not a thunk.
    using NnFindStringFn    = bool (*)(void* self, const char* key,
                                       const char** value);

    NnGetFileFormatFn g_origNnGetFileFormat = nullptr;
    NnFindStringFn    g_origNnFindString    = nullptr;

    int NnGetFileFormatHook(void* self, void* out) {
        const int ret = g_origNnGetFileFormat(self, out);

        const char* mime = nullptr;
        int cls = 0;
        if (ret == 0 && out != nullptr && g_origNnFindString != nullptr) {
            void* impl = *reinterpret_cast<void**>(out);
            if (impl != nullptr &&
                g_origNnFindString(impl, "mime", &mime) && mime != nullptr) {
                if (std::strcmp(mime, "video/mp4") == 0 ||
                    std::strcmp(mime, "audio/mp4") == 0) {
                    cls = 1;
                } else if (std::strcmp(mime, "video/x-matroska") == 0) {
                    cls = 2;
                } else if (std::strcmp(mime, "video/webm") == 0 ||
                           std::strcmp(mime, "audio/webm") == 0) {
                    cls = 3;
                } else if (std::strcmp(mime, "application/ogg") == 0) {
                    cls = 4;
                }
                if (cls != 0) {
                    int* field = reinterpret_cast<int*>(
                        reinterpret_cast<char*>(self) + 0x20);
                    *field = cls;
                }
            }
        }
        return ret;
    }

    // Hooked only so the format hook above has a callable original.
    bool NnFindStringHook(void* self, const char* key, const char** value) {
        return g_origNnFindString(self, key, value);
    }

    bool checkPrologue(uintptr_t target, const uint8_t* expected, const char* what) {
        if (std::memcmp(reinterpret_cast<const void*>(target), expected, 8) != 0) {
            WriteLog("[USM] %s prologue mismatch at %p -- skipped\n", what,
                     reinterpret_cast<const void*>(target));
            return false;
        }
        return true;
    }
}

static bool installMultimediaHooks() {
    constexpr uintptr_t kNnGetFileFormatOffset  = 0x00107380;
    constexpr uintptr_t kNnFindStringOffset     = 0x00113290;

    if (!exl::util::HasModule(exl::util::ModuleIndex::Subsdk1)) {
        WriteLog("[USM] Subsdk1 missing -- format hook skipped\n");
        return false;
    }
    const auto& mod = exl::util::GetModuleInfo(exl::util::ModuleIndex::Subsdk1);
    const uintptr_t base = mod.m_Total.m_Start;

    static const uint8_t kNnGetFileFormatPrologue[8] = { 0xff, 0xc3, 0x00, 0xd1,
                                                         0xff, 0xcf, 0x00, 0xa9 };
    static const uint8_t kNnFindStringPrologue[8]    = { 0xf9, 0x0f, 0x1b, 0xf8,
                                                         0xf8, 0x5f, 0x01, 0xa9 };

    const uintptr_t fmt = base + kNnGetFileFormatOffset;
    if (!checkPrologue(fmt, kNnGetFileFormatPrologue, "NnExtractor::GetFileFormat")) {
        return false;
    }
    g_origNnGetFileFormat = reinterpret_cast<NnGetFileFormatFn>(exl::hook::Hook(
        reinterpret_cast<void*>(fmt), &NnGetFileFormatHook, true));
    WriteLog("[USM] GetFileFormat hooked: %p -> %p\n",
             reinterpret_cast<const void*>(fmt),
             reinterpret_cast<const void*>(g_origNnGetFileFormat));

    const uintptr_t find = base + kNnFindStringOffset;
    if (checkPrologue(find, kNnFindStringPrologue, "NnMediaData::FindString")) {
        g_origNnFindString = reinterpret_cast<NnFindStringFn>(exl::hook::Hook(
            reinterpret_cast<void*>(find), &NnFindStringHook, true));
        WriteLog("[USM] FindString hooked: %p -> %p\n",
                 reinterpret_cast<const void*>(find),
                 reinterpret_cast<const void*>(g_origNnFindString));
    }

    return g_origNnGetFileFormat != nullptr;
}

void init() {
    WriteLog("[USM] INIT\n");
    installMultimediaHooks();
    UsmResolvePathHook::InstallAtOffset(0x001F3F70);
    UsmMovieOpenFileHook::InstallAtOffset(0x00439FD0);
    UsmMovieReadyHook::InstallAtOffset(0x0043A310);
}

} // namespace UsmSupport
