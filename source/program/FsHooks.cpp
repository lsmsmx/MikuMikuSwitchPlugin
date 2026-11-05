#include "FsHooks.hpp"
#include "DebugMode.hpp"
#include "Config.hpp"
#include "UsmSupport.hpp"
#include "logger.hpp"
#include <cstring>
#include <cstdio>
#include <mutex>
#include <set>
#include <lib/reloc/reloc.hpp>
#include <nn/fs.hpp>

namespace FsHooks {

static char g_AmsRomfsBase[512] = {0};
static std::set<std::string> g_FolderCache;
static std::mutex g_CacheMtx;

uintptr_t FindSymbol(const char* sym) {
    for(int i = static_cast<int>(exl::util::ModuleIndex::Start); i < static_cast<int>(exl::util::ModuleIndex::End); i++) {
        auto index = static_cast<exl::util::ModuleIndex>(i);
        if(exl::util::HasModule(index)) {
            Elf_Sym* symbol = exl::reloc::GetSymbol(index, sym);
            if (symbol != nullptr) return exl::util::GetModuleInfo(index).m_Total.m_Start + symbol->st_value;
        }
    }
    return 0;
}

void InitAmsPath() {
    if (g_AmsRomfsBase[0] != '\0') return;
    const char* tids[] = { "0100F3100DA46000", "01001CC00FA1A000", "0100BE300FF62000" };
    nn::fs::DirectoryHandle dh;
    for (const char* tid : tids) {
        char test[384];
        snprintf(test, sizeof(test), "ExlSD:/atmosphere/contents/%s/romfs", tid);
        if (R_SUCCEEDED(nn::fs::OpenDirectory(&dh, test, nn::fs::OpenDirectoryMode_Directory))) {
            nn::fs::CloseDirectory(dh);
            snprintf(g_AmsRomfsBase, sizeof(g_AmsRomfsBase), "%s/", test);
            return;
        }
    }
    snprintf(g_AmsRomfsBase, sizeof(g_AmsRomfsBase), "ExlSD:/atmosphere/contents/01001CC00FA1A000/romfs/");
}

void SafeCreateDirs(const char* fullPath) {
    char temp[768];
    strncpy(temp, fullPath, sizeof(temp));
    temp[767] = '\0';
    char* lastSlash = strrchr(temp, '/');
    if (!lastSlash) return;
    *lastSlash = '\0';
    {
        std::lock_guard<std::mutex> lock(g_CacheMtx);
        if (g_FolderCache.count(temp)) return;
        g_FolderCache.insert(temp);
    }
    char* p = temp;
    if (strncmp(p, "ExlSD:/", 7) == 0) p += 7;
    while (*p) {
        if (*p == '/') {
            *p = '\0';
            nn::fs::CreateDirectory(temp);
            *p = '/';
        }
        p++;
    }
    nn::fs::CreateDirectory(temp);
}

// Fastest path redirection
bool RedirectDebugPath(const char* origPath, char* outPath, size_t outSize, bool isWriteMode = false) {
    if (!origPath || origPath[0] == '\0') return false;
    if (strncmp(origPath, "ExlSD:/", 7) == 0) return false;

    bool isNativeSave = false;
    const char* suffix = origPath;

    if (strncmp(origPath, "savedata:/", 10) == 0) { suffix = origPath + 10; isNativeSave = true; }
    else if (strncmp(origPath, "save:/", 6) == 0) { suffix = origPath + 6; isNativeSave = true; }
    else if (strncmp(origPath, "rom:/", 5) == 0)  { suffix = origPath + 5; }
    else if (strncmp(origPath, "host:/", 6) == 0) { suffix = origPath + 6; }
    // Any other mount is somebody else's business.  Now that the write-mode
    // rule below actually fires, an unrecognised prefix would otherwise be
    // rewritten to "<romfs base>/album:/..." and the write would land nowhere.
    else if (strchr(origPath, ':') != nullptr) return false;

    while (*suffix == '/' || *suffix == '\\' || *suffix == '.') {
        suffix++;
    }

    bool isDebugDir = (strncmp(suffix, "ram", 3) == 0 ||
                       strncmp(suffix, "dev_ram", 7) == 0 ||
                       strncmp(suffix, "osage", 5) == 0 ||
                       (isWriteMode && strstr(suffix, "light_") != nullptr));

    if (isNativeSave && !isDebugDir) return false;
    if (!isNativeSave && !isDebugDir && !isWriteMode) return false;

    InitAmsPath();
    char rawJoined[512];
    snprintf(rawJoined, sizeof(rawJoined), "%s%s", g_AmsRomfsBase, suffix);

    size_t j = 0;
    bool lastWasSlash = false;
    for (size_t i = 0; rawJoined[i] != '\0' && j < outSize - 1; i++) {
        char c = (rawJoined[i] == '\\') ? '/' : rawJoined[i];
        if (c == '/') {
            if (!lastWasSlash) { outPath[j++] = c; lastWasSlash = true; }
        } else {
            outPath[j++] = c; lastWasSlash = false;
        }
    }
    outPath[j] = '\0';
    if (j > 0 && outPath[j - 1] == '/') outPath[j - 1] = '\0';

    return true;
}

// ---------------------------------------------------------------------------
// Virtual movie files (UsmSupport)
//
// These trampolines live here, not in UsmSupport, so there is exactly one hook
// per nn::fs address and the debug redirection above composes with the movie
// serving instead of fighting it for the same address.  UsmSupport supplies
// the work as plain functions and receives this hook's original as `orig`.
//
// A symbol can be defined by more than one module and one trampoline holds one
// Orig, so each copy needs its own instance; every defining module is patched.
// ---------------------------------------------------------------------------
template <int I> struct VOpenFileHook;
template <int I> struct VEntryTypeHook;
template <int I> struct VGetFileSizeHook;
template <int I> struct VReadHook5;
template <int I> struct VReadHook6;
template <int I> struct VCloseHook;
template <int I> struct VFlushHook;

// The debug redirection, applied from the movie callbacks below so that one
// address still holds one trampoline.  Kept out of the per-module template so
// the logic exists once, not once per copy.  `res` is set when it handled the
// call; false means "not a debug path, carry on with the movie serving".
using FsOpenOrig  = uint32_t (*)(void*, const char*, int);
using FsEntryOrig = uint32_t (*)(void*, const char*);

// Defined next to the write-side hooks further down; declared here because the
// read-side OpenFile trampoline is also where write-mode opens land.

static bool DebugRedirectOpen(FsOpenOrig orig, void* out, const char* path,
                              int mode, uint32_t* res) {
    // Write and Append both mean the handle is about to write, which is what
    // decides whether a path outside the debug dirs is redirected as well.
    // This flag existed from the beginning but was never passed, so the
    // write-mode rule in RedirectDebugPath could not fire.
    const bool isWrite = (mode & (2 | 4)) != 0;

    char newPath[768];
    if (!RedirectDebugPath(path, newPath, sizeof(newPath), isWrite)) return false;

    if (isWrite) {
        SafeCreateDirs(newPath);
        // Check if the file exists via read mode; `orig` is the real function,
        // so this cannot re-enter a hook.
        nn::fs::FileHandle tmp;
        if (R_FAILED(orig(&tmp, newPath, 1))) nn::fs::CreateFile(newPath, 0);
        else                                  nn::fs::CloseFile(tmp);
    }
    *res = orig(out, newPath, mode);
    return true;
}

static bool DebugRedirectEntryType(FsEntryOrig orig, void* out, const char* path,
                                   uint32_t* res) {
    char newPath[768];
    if (!RedirectDebugPath(path, newPath, sizeof(newPath))) return false;
    *res = orig(out, newPath);
    return true;
}

#define VFILE_HOOKS(I)                                                            \
    template <> struct VOpenFileHook<I>                                           \
        : exl::hook::impl::TrampolineHook<VOpenFileHook<I>> {                     \
        static uint32_t Orig_(void* a, const char* b, int c) {                    \
            return Orig(a, b, c);                                                 \
        }                                                                         \
        static uint32_t Callback(void* out, const char* path, int mode) {         \
            uint32_t res;                                                         \
            if (DebugRedirectOpen(&Orig_, out, path, mode, &res)) return res;     \
            return UsmSupport::vfileOpenFileImpl(&Orig_, out, path, mode);        \
        }                                                                         \
    };                                                                            \
    template <> struct VEntryTypeHook<I>                                          \
        : exl::hook::impl::TrampolineHook<VEntryTypeHook<I>> {                    \
        static uint32_t Orig_(void* a, const char* b) { return Orig(a, b); }      \
        static uint32_t Callback(void* out, const char* path) {                   \
            uint32_t res;                                                         \
            if (DebugRedirectEntryType(&Orig_, out, path, &res)) return res;      \
            return UsmSupport::vfileGetEntryTypeImpl(&Orig_, out, path);          \
        }                                                                         \
    };                                                                            \
    template <> struct VGetFileSizeHook<I>                                        \
        : exl::hook::impl::TrampolineHook<VGetFileSizeHook<I>> {                  \
        static uint32_t Orig_(int64_t* a, uint64_t b) { return Orig(a, b); }      \
        static uint32_t Callback(int64_t* out, uint64_t h) {                      \
            return UsmSupport::vfileGetFileSizeImpl(&Orig_, out, h);              \
        }                                                                         \
    };                                                                            \
    template <> struct VReadHook5<I>                                              \
        : exl::hook::impl::TrampolineHook<VReadHook5<I>> {                        \
        static uint32_t Orig_(uint64_t a, int64_t b, void* c, size_t d,           \
                              const void* e) {                                    \
            return Orig(a, b, c, d, e);                                           \
        }                                                                         \
        static uint32_t Callback(uint64_t h, int64_t pos, void* buf, size_t n,    \
                                 const void* opt) {                               \
            return UsmSupport::vfileReadImpl(&Orig_, h, pos, buf, n, opt);        \
        }                                                                         \
    };                                                                            \
    template <> struct VReadHook6<I>                                              \
        : exl::hook::impl::TrampolineHook<VReadHook6<I>> {                        \
        static uint32_t Orig_(size_t* a, uint64_t b, int64_t c, void* d,          \
                              size_t e) {                                         \
            return Orig(a, b, c, d, e);                                           \
        }                                                                         \
        static uint32_t Callback(size_t* got, uint64_t h, int64_t pos, void* buf, \
                                 size_t n) {                                      \
            return UsmSupport::vfileReadImpl6(&Orig_, got, h, pos, buf, n);       \
        }                                                                         \
    };                                                                            \
    template <> struct VCloseHook<I>                                              \
        : exl::hook::impl::TrampolineHook<VCloseHook<I>> {                        \
        static void Orig_(uint64_t a) { Orig(a); }                                \
        static void Callback(uint64_t h) { UsmSupport::vfileCloseImpl(&Orig_, h); }\
    };                                                                            \
    template <> struct VFlushHook<I>                                              \
        : exl::hook::impl::TrampolineHook<VFlushHook<I>> {                        \
        static uint32_t Orig_(uint64_t a) { return Orig(a); }                     \
        static uint32_t Callback(uint64_t h) {                                    \
            return UsmSupport::vfileFlushImpl(&Orig_, h);                         \
        }                                                                         \
    };

VFILE_HOOKS(0)
VFILE_HOOKS(1)
VFILE_HOOKS(2)
VFILE_HOOKS(3)

// Collect every module that *defines* `name` (GetSymbol skips imports).  Up to
// four definers: the loader binds an import to the first module in its own
// resolution order, which is not this scan's order, so patching only the first
// copy found is a guess that the reader's copy is among them.
//
// Two things are checked before a site is patched, because exlaunch does not
// check either of them:
//
//  * exlaunch's HookFuncImpl relocates exactly 4 or 5 instructions and writes
//    16 or 20 bytes at the address it is given, whatever the function's real
//    length is.  A body shorter than that loses the first bytes of the next
//    function in the module, and every call to that neighbour then starts with
//    our LDR/BR -- it runs the wrong function.  The engine's own file reads go
//    through these same nn::fs entry points, so a movie hook can corrupt a
//    chart load.  A site whose body does not reach 20 bytes is skipped.
//
//  * an exported entry point is sometimes a one-instruction veneer that just
//    branches into the body.  Those are shorter than the patch by definition;
//    the branch is followed to the body and that is what gets patched, which
//    keeps the hook working instead of dropping it.
static uintptr_t resolveThunk(uintptr_t addr, exl::util::ModuleIndex index) {
    const exl::util::Range& range = exl::util::GetModuleInfo(index).m_Total;
    for (int hop = 0; hop < 8; ++hop) {
        const uint32_t ins = *reinterpret_cast<const uint32_t*>(addr);
        // Plain `b imm26`; `bl` is a call and the function continues below it.
        if ((ins & 0xFC000000u) != 0x14000000u) return addr;
        const uintptr_t target = static_cast<uintptr_t>(static_cast<int64_t>(addr) +
            (static_cast<int64_t>(static_cast<int32_t>(ins << 6) >> 6) << 2));
        // A branch out of this module at the entry is not a veneer: it is
        // somebody else's hook (SaltySD installs its own on these same nn::fs
        // functions) or an import stub.  Following it would mean patching *that*
        // code, so the site is dropped instead and said so in the log.
        if (!range.InRange(target)) return 0;
        addr = target;
    }
    return addr;
}

static bool patchFits(uintptr_t addr) {
    for (int i = 0; i < 5; ++i) {
        if (*reinterpret_cast<const uint32_t*>(addr + static_cast<uintptr_t>(i) * 4u)
            == 0xD65F03C0u)
            return false;   // the function ends inside the rewrite window
    }
    return true;
}

static int findSymbolSites(const char* name, uintptr_t* out, int cap) {
    int n = 0;
    for (int i = static_cast<int>(exl::util::ModuleIndex::Start);
         i < static_cast<int>(exl::util::ModuleIndex::End) && n < cap; ++i) {
        const auto index = static_cast<exl::util::ModuleIndex>(i);
        if (!exl::util::HasModule(index)) continue;
        const Elf_Sym* sym = exl::reloc::GetSymbol(index, name);
        if (!sym || sym->st_value == 0) continue;

        const uintptr_t entry = exl::util::GetModuleInfo(index).m_Total.m_Start + sym->st_value;
        const uintptr_t site = resolveThunk(entry, index);
        if (site == 0) {

            continue;
        }
        if (!patchFits(site)) {

            continue;
        }

        // Two names, or two modules, can reach one body; patching it twice
        // would put a hook on top of a hook.
        bool seen = false;
        for (int k = 0; k < n; ++k) seen = seen || out[k] == site;
        if (seen) continue;

        out[n++] = site;
    }
    return n;
}

#define VFILE_INSTALL(HOOKS, NAME, FOUND)                                         \
    do {                                                                          \
        uintptr_t sites[4] = {0, 0, 0, 0};                                        \
        const int found = findSymbolSites(NAME, sites, 4);                        \
        FOUND = found;                                                            \
        if (found > 0)                                                            \
            HOOKS<0>::InstallAtFuncPtr(                                           \
                reinterpret_cast<decltype(&HOOKS<0>::Callback)>(sites[0]));       \
        if (found > 1)                                                            \
            HOOKS<1>::InstallAtFuncPtr(                                           \
                reinterpret_cast<decltype(&HOOKS<1>::Callback)>(sites[1]));       \
        if (found > 2)                                                            \
            HOOKS<2>::InstallAtFuncPtr(                                           \
                reinterpret_cast<decltype(&HOOKS<2>::Callback)>(sites[2]));       \
        if (found > 3)                                                            \
            HOOKS<3>::InstallAtFuncPtr(                                           \
                reinterpret_cast<decltype(&HOOKS<3>::Callback)>(sites[3]));       \
    } while (0)

static void installVirtualFileHooks() {
    // The first three counts are what UsmSupport needs below; the installs whose
    // count nothing looks at share nUnused.
    int nOpen = 0, nSize = 0, nClose = 0, nUnused = 0;
    (void)nUnused;

    VFILE_INSTALL(VOpenFileHook, "_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci",
                  nOpen);
    VFILE_INSTALL(VEntryTypeHook,
                  "_ZN2nn2fs12GetEntryTypeEPNS0_18DirectoryEntryTypeEPKc",
                  nUnused);
    VFILE_INSTALL(VGetFileSizeHook, "_ZN2nn2fs11GetFileSizeEPlNS0_10FileHandleE",
                  nSize);
    VFILE_INSTALL(VReadHook5,
                  "_ZN2nn2fs8ReadFileENS0_10FileHandleElPvmRKNS0_10ReadOptionE",
                  nUnused);
    VFILE_INSTALL(VReadHook6, "_ZN2nn2fs8ReadFileEPmNS0_10FileHandleElPvm",
                  nUnused);
    VFILE_INSTALL(VCloseHook, "_ZN2nn2fs9CloseFileENS0_10FileHandleE", nClose);
    VFILE_INSTALL(VFlushHook, "_ZN2nn2fs9FlushFileENS0_10FileHandleE", nUnused);

    // The originals UsmSupport does its own file access with, so its probing
    // never comes back through these hooks.  Copy 0's is enough: every copy is
    // the same SDK library, and only an installed one has an original to give.
    if (nOpen > 0 && nSize > 0 && nClose > 0) {
        UsmSupport::RawFs raw;
        raw.open    = &VOpenFileHook<0>::Orig_;
        raw.getSize = &VGetFileSizeHook<0>::Orig_;
        raw.close   = &VCloseHook<0>::Orig_;
        UsmSupport::SetRawFs(raw);
    }
}

// ---------------------------------------------------------------------------
// Write-side path redirection.
//
// These five were installed with FindSymbol(), which stops at the first module
// that *defines* the name -- the exact guess that findSymbolSites() above was
// written to avoid, because the loader binds an import to the first copy in its
// own resolution order, which is not this scan's order.  The read side
// (OpenFile / GetEntryType) has always patched every defining copy; the write
// side is what the OPD generator writes through, so it patches every copy too,
// following veneers and skipping any body the 16/20-byte rewrite would run
// past.
//
// The redirection is also no longer tied to Config::enableDebug.  It used to
// be, and OpdMakerFix::init() is installed unconditionally, so a build with
// `debug = false` ran the generator with no redirection at all: every path the
// generator handed to nn::fs went to the real `savedata:` mount instead of the
// title's Atmosphère romfs.  What *is* redirected is unchanged and still
// decided by RedirectDebugPath; only the switch in front of it is gone.
enum class VfsKind { CreateFile, OpenDirectory, DeleteFile, CreateDirectory, RenameFile };

template <VfsKind K, int I>
struct VfsHook : exl::hook::impl::TrampolineHook<VfsHook<K, I>> {
    // One signature for all five.  The widest call is (out, path, mode); a
    // one- or two-argument callee simply ignores the extra register, and a
    // void callee ignores the return value, so this is ABI-safe at every site.
    static uint32_t Orig_(uint64_t a, uint64_t b, uint64_t c) { return VfsHook::Orig(a, b, c); }
    static uint32_t Callback(uint64_t a, uint64_t b, uint64_t c) {
        return Dispatch(&Orig_, a, b, c);
    }

    static uint32_t Dispatch(uint32_t (*orig)(uint64_t, uint64_t, uint64_t),
                             uint64_t a, uint64_t b, uint64_t c) {
        char newPath[768];

        if constexpr (K == VfsKind::CreateFile) {
            const char* path = reinterpret_cast<const char*>(a);
            if (RedirectDebugPath(path, newPath, sizeof(newPath), true)) {
                SafeCreateDirs(newPath);
                const uint32_t res = orig(reinterpret_cast<uint64_t>(newPath), b, c);
                return res;
            }
            const uint32_t res = orig(a, b, c);
            return res;
        } else if constexpr (K == VfsKind::OpenDirectory) {
            const char* path = reinterpret_cast<const char*>(b);
            if (RedirectDebugPath(path, newPath, sizeof(newPath))) {
                const uint32_t res = orig(a, reinterpret_cast<uint64_t>(newPath), c);
                return res;
            }
            const uint32_t res = orig(a, b, c);
            return res;
        } else if constexpr (K == VfsKind::DeleteFile) {
            const char* path = reinterpret_cast<const char*>(a);
            if (RedirectDebugPath(path, newPath, sizeof(newPath))) {
                const uint32_t res = orig(reinterpret_cast<uint64_t>(newPath), b, c);
                return res;
            }
            const uint32_t res = orig(a, b, c);
            return res;
        } else if constexpr (K == VfsKind::CreateDirectory) {
            const char* path = reinterpret_cast<const char*>(a);
            if (RedirectDebugPath(path, newPath, sizeof(newPath), true)) {
                SafeCreateDirs(newPath);
                const uint32_t res = orig(reinterpret_cast<uint64_t>(newPath), b, c);
                return res;
            }
            const uint32_t res = orig(a, b, c);
            return res;
        } else { // RenameFile
            const char* oldPath = reinterpret_cast<const char*>(a);
            const char* newPathRaw = reinterpret_cast<const char*>(b);
            char newOldPath[768];
            char newNewPath[768];
            const bool rOld = RedirectDebugPath(oldPath, newOldPath, sizeof(newOldPath));
            const bool rNew = RedirectDebugPath(newPathRaw, newNewPath, sizeof(newNewPath));

            if (rOld && rNew) {
                SafeCreateDirs(newNewPath);
                nn::fs::DeleteFile(newNewPath);
                const uint32_t res = orig(reinterpret_cast<uint64_t>(newOldPath),
                                          reinterpret_cast<uint64_t>(newNewPath), c);
                return res;
            }
            if (rOld)
                return orig(reinterpret_cast<uint64_t>(newOldPath), b, c);
            if (rNew) {
                SafeCreateDirs(newNewPath);
                nn::fs::DeleteFile(newNewPath);
                const uint32_t res = orig(a, reinterpret_cast<uint64_t>(newNewPath), c);
                return res;
            }
        }
        return orig(a, b, c);
    }
};

template <VfsKind K>
static void installVfsKind(const char* name) {
    uintptr_t sites[4] = {0, 0, 0, 0};
    const int n = findSymbolSites(name, sites, 4);
    // Whether a symbol was found at all is the difference between "the write is
    // redirected" and "the write went to the real savedata while the listing
    // went to romfs", which is exactly the shape of a listing that comes back
    // empty after a bake that ran to completion.
    if (n > 0)
        VfsHook<K, 0>::InstallAtFuncPtr(
            reinterpret_cast<decltype(&VfsHook<K, 0>::Callback)>(sites[0]));
    if (n > 1)
        VfsHook<K, 1>::InstallAtFuncPtr(
            reinterpret_cast<decltype(&VfsHook<K, 1>::Callback)>(sites[1]));
    if (n > 2)
        VfsHook<K, 2>::InstallAtFuncPtr(
            reinterpret_cast<decltype(&VfsHook<K, 2>::Callback)>(sites[2]));
    if (n > 3)
        VfsHook<K, 3>::InstallAtFuncPtr(
            reinterpret_cast<decltype(&VfsHook<K, 3>::Callback)>(sites[3]));
}

void Init() {

    // Virtual movie files first, and unconditionally: that install is what
    // serves a mod's .usm under the .mp4 name, and it is the only set of
    // trampolines on OpenFile / GetEntryType / FlushFile (+ ReadFile,
    // GetFileSize, CloseFile).  The path redirection is applied *inside* those
    // same callbacks, so one address still holds exactly one trampoline and
    // neither feature displaces the other.
    installVirtualFileHooks();

    installVfsKind<VfsKind::CreateFile>("_ZN2nn2fs10CreateFileEPKcl");
    installVfsKind<VfsKind::OpenDirectory>("_ZN2nn2fs13OpenDirectoryEPNS0_15DirectoryHandleEPKci");
    installVfsKind<VfsKind::DeleteFile>("_ZN2nn2fs10DeleteFileEPKc");
    installVfsKind<VfsKind::CreateDirectory>("_ZN2nn2fs15CreateDirectoryEPKc");
    installVfsKind<VfsKind::RenameFile>("_ZN2nn2fs10RenameFileEPKcS2_");
}

} // namespace FsHooks
