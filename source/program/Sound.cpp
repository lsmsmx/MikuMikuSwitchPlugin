#include "Sound.hpp"
#include "lib.hpp"
#include "logger.hpp"
#include "Config.hpp"
#include <atomic>
#include <string_view>
#include <cmath>

namespace SustainAudio {

// =========================================================================
// 1. NN::AUDIO STRUCTURES (SWITCH SDK)
// =========================================================================

struct WaveBuffer {
    const void* buffer;              // +0x00
    size_t size;                     // +0x08
    int32_t startSampleOffset;       // +0x10
    int32_t endSampleOffset;         // +0x14
    bool isLoop;                     // +0x18
    bool isEndOfStream;              // +0x19
    uint8_t _pad[6];                 // +0x1A
    const void* pContext;            // +0x20
    size_t contextSize;              // +0x28
};

enum PlayState {
    PlayState_Started = 0,
    PlayState_Stopped = 1,
    PlayState_Paused  = 2
};

typedef void (*SetVoicePlayStateFn)(void* voice, int playState);
static SetVoicePlayStateFn s_SetVoicePlayState = nullptr;

// =========================================================================
// 2. CUE OBJECT (diva_nx.nss)
// =========================================================================
//
// The engine keeps 32 cue slots at 0xbe350930, stride 0xa0 (the loop in
// FUN_00606d80). FUN_006085cc picks one for every play and, when all 32 are
// busy, reuses the cue with the oldest play id -- which is what a looping
// sustain always looks like, since it never re-triggers. FUN_00608a20 (the
// function we hook) fills it in, and the voice's PCM arrives later through
// nn::audio::AppendWaveBuffer().
//
// The appended voice is not reachable from the cue, so a sustain is still bound
// the way it always was -- the wave buffer that follows the queue-4 start is
// ours. What the cue *is* good for is telling whether that start is still the
// one we armed: every recycle (steal, restart, release) writes a new play id
// and clears the archive entry, and the engine stops the voice while doing so.

#define ADDR_CUE_POOL        0x00BE350930
#define CUE_STRIDE           0xa0
#define CUE_COUNT            32

static const uintptr_t CUE_PROP   = 0x68;  // sound archive entry, 0 while idle
static const uintptr_t CUE_PLAYID = 0x70;  // int32, new on every start

struct SustainSlot {
    void*   cue;         // cue that started this sustain (nullptr = free)
    void*   voice;       // voice, once its wave buffer has been seen
    int32_t play_id;     // cue+0x70 at the time the sustain was started
    int32_t loop_start;
    int32_t loop_end;
};

static SustainSlot s_sustain[16] = {};

// Slot that is waiting for its wave buffer. One at a time, like the claim it
// replaces: two sustains started in the same frame must not fight over the same
// voice.
static int s_claimSlot = -1;

// The cue is set up on the game thread, the wave buffer is appended on the sound
// thread -- so everything shared sits behind this lock.
static std::atomic<uint32_t> s_sustainLock{ 0 };

static void SustainLock()   { while (s_sustainLock.exchange(1, std::memory_order_acquire)) { } }
static void SustainUnlock() { s_sustainLock.store(0, std::memory_order_release); }

static inline int32_t CuePlayId(void* cue) {
    return *reinterpret_cast<int32_t*>((uintptr_t)cue + CUE_PLAYID);
}

// Is this still the cue we armed the sustain on? A recycle gives it a new play
// id and clears its archive entry, and the engine stops the voice while doing
// so -- whatever we still hold from back then is worthless and must not be
// poked (the sound that owns that voice now is an innocent bystander).
static inline bool CueAlive(void* cue, int32_t play_id) {
    return cue != nullptr
        && CuePlayId(cue) == play_id
        && *reinterpret_cast<void**>((uintptr_t)cue + CUE_PROP) != nullptr;
}

static void StopVoice(void* voice) {
    if (voice && s_SetVoicePlayState) {
        s_SetVoicePlayState(voice, PlayState_Stopped);
    }
}

static void ClearRecordLocked(int slot) {
    SustainSlot& rec = s_sustain[slot];

    rec.cue        = nullptr;
    rec.voice      = nullptr;
    rec.play_id    = 0;
    rec.loop_start = 0;
    rec.loop_end   = 0;

    if (s_claimSlot == slot)
        s_claimSlot = -1;
}

// Unlink slot `slot` and hand back its voice -- but only while the cue still
// runs the play we armed.
static void* DetachVoiceLocked(int slot) {
    SustainSlot& rec = s_sustain[slot];
    void* voice = rec.voice;

    if (!(voice && CueAlive(rec.cue, rec.play_id)))
        voice = nullptr;

    ClearRecordLocked(slot);
    return voice;
}

// The official loop region has to lie inside the buffer we are handed. That is
// what keeps a short hit or menu blip from ever being taken for the sustain
// sample when it gets to the sound thread first.
static inline bool LoopFits(const SustainSlot& rec, const WaveBuffer* waveBuffer) {
    return rec.loop_start >= 0
        && rec.loop_end > rec.loop_start
        && rec.loop_end <= static_cast<int32_t>(waveBuffer->endSampleOffset)
        && waveBuffer->endSampleOffset > 10000;
}

inline int GetSustainSlot(std::string_view name) {
    if (name.find("rush") != std::string_view::npos) return 6;

    auto pos_btn = name.find("button_l");
    if (pos_btn != std::string_view::npos && pos_btn + 8 < name.size()) {
        char digit = name[pos_btn + 8];
        if (digit >= '1' && digit <= '5') {
            return digit - '0'; // 1..5
        }
    }

    auto pos_scr = name.find("scratch_l");
    if (pos_scr != std::string_view::npos && pos_scr + 9 < name.size()) {
        char digit = name[pos_scr + 9];
        if (digit >= '1' && digit <= '5') {
            return 6 + (digit - '0'); // 7..11
        }
    }

    return -1;
}

// =========================================================================
// 3. SUSTAIN SLOT STOPPING MANAGEMENT
// =========================================================================

void StopSlot(int slot) {
    if (slot < 0 || slot >= 16)
        return;

    SustainLock();
    void* voice = DetachVoiceLocked(slot);
    SustainUnlock();

    StopVoice(voice);
}

void StopAll() {
    void* voices[16];

    SustainLock();
    for (int i = 0; i < 16; i++)
        voices[i] = DetachVoiceLocked(i);
    SustainUnlock();

    for (int i = 0; i < 16; i++)
        StopVoice(voices[i]);
}

// =========================================================================
// 4. LOOPING HOOKS
// =========================================================================

static inline bool IsSystemSe(std::string_view name) {
    return name.find("dialog")  != std::string_view::npos
        || name.find("sys_")    != std::string_view::npos
        || name.find("pause")   != std::string_view::npos
        || name.find("result")  != std::string_view::npos
        || name.find("restart") != std::string_view::npos
        || name.find("retry")   != std::string_view::npos
        || name.find("clear")   != std::string_view::npos
        || name.find("miss")    != std::string_view::npos
        || name.find("wrong")   != std::string_view::npos;
}

HOOK_DEFINE_TRAMPOLINE(SoundCueLoadHook) {
    static uint32_t Callback(void* cue, uint32_t queue_index, const char* se_name, float volume) {

        uint32_t ret = Orig(cue, queue_index, se_name, volume);

        if (!se_name || !cue) {
            return ret;
        }

        std::string_view name(se_name);

        // A system cue means the player left the song or a menu took over: let
        // go of every sustain we still hold (their voices only, see DetachVoice).
        if (IsSystemSe(name)) {
            StopAll();
            return ret;
        }

        int slot = GetSustainSlot(name);
        if (slot >= 0) {
            if (name.find("_off") != std::string_view::npos) {
                StopSlot(slot);
                return ret;
            }

            // ret is the new play id, 0 when the name is not in the archive and
            // the cue was never started -- then there is no voice to loop.
            // Only the sustain queue carries the looping voice; slot >= 6 are
            // the rush/scratch ones, which come back on that queue too.
            bool isSustainQueue = (queue_index == 4);
            bool isLoopStart    = (name.find("_on") != std::string_view::npos) || slot >= 6;

            if (ret != 0 && isSustainQueue && isLoopStart) {
                int32_t loop_start = 0;
                int32_t loop_end   = 0;

                uintptr_t prop = *reinterpret_cast<uintptr_t*>((uintptr_t)cue + CUE_PROP);
                if (prop) {
                    loop_start = *reinterpret_cast<int32_t*>(prop + 0x10);
                    loop_end   = *reinterpret_cast<int32_t*>(prop + 0x18);
                }

                if (slot == 6 && (loop_end <= loop_start || loop_start == 0)) {
                    loop_start = 123206;
                    loop_end   = 125305;
                }

                void* stale = nullptr;

                SustainLock();
                if (s_sustain[slot].cue != cue)
                    stale = DetachVoiceLocked(slot);

                SustainSlot& rec = s_sustain[slot];
                rec.cue        = cue;
                rec.voice      = nullptr;
                rec.play_id    = CuePlayId(cue);
                rec.loop_start = loop_start;
                rec.loop_end   = loop_end;
                s_claimSlot    = slot;
                SustainUnlock();

                StopVoice(stale);
            }
        }
        return ret;
    }
};

HOOK_DEFINE_TRAMPOLINE(AppendWaveBufferHook) {
    static void Callback(void* voice, const WaveBuffer* waveBuffer) {
        if (!voice || !waveBuffer) {
            Orig(voice, waveBuffer);
            return;
        }

        int32_t loop_start = 0;
        int32_t loop_end   = 0;
        int     slot       = -1;

        SustainLock();

        bool tracked = false;
        for (int i = 0; i < 16; i++) {
            if (s_sustain[i].cue) {
                tracked = true;
                break;
            }
        }

        if (tracked) {
            // Forget the sustains whose cue the engine has recycled: it changed
            // the play id, stopped the voice and, if this was a steal, gave the
            // slot to another sound. Nothing of ours is left in there.
            for (int i = 0; i < 16; i++) {
                SustainSlot& rec = s_sustain[i];
                if (rec.cue && !CueAlive(rec.cue, rec.play_id)) {
                    ClearRecordLocked(i);
                }
            }

            // Renewal: the engine hands us a buffer for a voice we already loop.
            for (int i = 0; i < 16 && slot < 0; i++) {
                SustainSlot& rec = s_sustain[i];
                if (!rec.cue || rec.voice != voice || !LoopFits(rec, waveBuffer))
                    continue;

                slot       = i;
                loop_start = rec.loop_start;
                loop_end   = rec.loop_end;
            }

            // The wave buffer that follows the queue-4 start is ours -- and the
            // claim is not used up by a buffer that cannot hold the loop region,
            // so a short blip racing us to the sound thread costs nothing.
            if (slot < 0 && s_claimSlot >= 0 && s_claimSlot < 16) {
                SustainSlot& rec = s_sustain[s_claimSlot];
                if (rec.cue && !rec.voice && LoopFits(rec, waveBuffer)) {
                    rec.voice  = voice;
                    slot       = s_claimSlot;
                    loop_start = rec.loop_start;
                    loop_end   = rec.loop_end;
                }
            }
        }

        SustainUnlock();

        if (slot >= 0) {
            auto* wb = const_cast<WaveBuffer*>(waveBuffer);
            wb->startSampleOffset = loop_start;
            wb->endSampleOffset   = loop_end;
            wb->isLoop            = true;
            wb->isEndOfStream     = false;
        }

        Orig(voice, waveBuffer);
    }
};

// =========================================================================
// 5. VOLUME BOOST HOOKS
// =========================================================================

#define ADDR_APPLY_SOUND_SETTINGS  0x000C9660
#define ADDR_SET_GLOBAL_BGM        0x006092C0
#define ADDR_SET_QUEUE_VOL         0x00609220
#define ADDR_SET_HANDLE_VOL        0x0060DC00
#define ADDR_SET_STREAM_VOL        0x00609350
#define ADDR_SET_DUMMY_VOL         0x006092E0

#define ADDR_DAT_BGM_SETTING       0x04D243EE8
#define ADDR_DAT_DUMMY_SETTING     0x04D243EE4
#define ADDR_DAT_BTN_SETTING       0x04D243EEC
#define ADDR_DAT_SE_SETTING        0x04D243EF0

#define ADDR_BGM_STORAGE           0x00BE351D84
#define ADDR_QUEUE_STORAGE         0x00BE351D6C

HOOK_DEFINE_REPLACE(SetHandleVolumeHook) {
    static void Callback(float volume, uintptr_t handle) {
        if (!handle) return;
        if (volume < 0.0f) volume = 0.0f;
        *reinterpret_cast<float*>(handle + 0x20) = volume;
        *reinterpret_cast<uint32_t*>(handle + 0x3c) = 0;
    }
};

HOOK_DEFINE_REPLACE(SetGlobalBgmVolumeHook) {
    static void Callback(float volume) {
        if (volume < 0.0f) volume = 0.0f;

        if (Config::bgmBoost > 0) {
            volume *= (1.0f + ((float)Config::bgmBoost / 100.0f));
        }

        uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
        *reinterpret_cast<float*>(base + ADDR_BGM_STORAGE) = volume;
    }
};

HOOK_DEFINE_REPLACE(SetQueueVolumeHook) {
    static void Callback(float volume, uint32_t queue_index) {
        if (queue_index < 6) {
            if (volume < 0.0f) volume = 0.0f;

            if (queue_index == 3 || queue_index == 4) {
                if (Config::btnBoost > 0) {
                    volume *= (1.0f + ((float)Config::btnBoost / 100.0f));
                }
            } else {
                if (Config::seBoost > 0) {
                    volume *= (1.0f + ((float)Config::seBoost / 100.0f));
                }
            }

            uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
            *reinterpret_cast<float*>(base + ADDR_QUEUE_STORAGE + queue_index * 4) = volume;
        }
    }
};

HOOK_DEFINE_TRAMPOLINE(SetStreamVolumeHook) {
    static bool Callback(float volume, uint32_t stream_idx) {
        if ((stream_idx == 0 || stream_idx == 2) && Config::bgmBoost > 0) {
            volume *= (1.0f + ((float)Config::bgmBoost / 100.0f));
        }
        else if (stream_idx == 1) {
            if (Config::acapellaMode && Config::bgmBoost > 0) {
                volume *= (1.0f + ((float)Config::bgmBoost / 100.0f));
            }
            else if (Config::seBoost > 0) {
                volume *= (1.0f + ((float)Config::seBoost / 100.0f));
            }
        }
        return Orig(volume, stream_idx);
    }
};

HOOK_DEFINE_REPLACE(SetBgmStreamVolumeHook) {
    static void Callback(float volume) {
        uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;

        auto fun_0060d370 = reinterpret_cast<void(*)(float)>(base + 0x0060d370);
        auto fun_0060d380 = reinterpret_cast<float(*)()>(base + 0x0060d380);
        auto fun_0060d350 = reinterpret_cast<uintptr_t(*)(uint32_t)>(base + 0x0060d350);
        auto fun_0060dc00 = reinterpret_cast<void(*)(float, uintptr_t)>(base + 0x0060dc00);

        float boostedVol = volume;
        if (Config::bgmBoost > 0) {
            boostedVol *= (1.0f + ((float)Config::bgmBoost / 100.0f));
        }

        fun_0060d370(boostedVol);
        float fVar2 = fun_0060d380();

        if (Config::bgmBoost > 0 && fVar2 < boostedVol) {
            fVar2 = boostedVol;
        }

        uint8_t dat_be351d56 = *reinterpret_cast<uint8_t*>(base + 0x00be351d56);
        if (dat_be351d56 != 0) {
            uintptr_t lVar1 = fun_0060d350(0);
            if (lVar1 != 0) fun_0060dc00(fVar2, lVar1);
        }

        uint8_t dat_be351d58 = *reinterpret_cast<uint8_t*>(base + 0x00be351d58);
        if (dat_be351d58 != 0) {
            uintptr_t lVar1 = fun_0060d350(2);
            if (lVar1 != 0) fun_0060dc00(fVar2, lVar1);
        }
    }
};

HOOK_DEFINE_REPLACE(ApplySoundSettingsHook) {
    static void Callback() {
        uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;

        auto set_bgm    = reinterpret_cast<void(*)(float)>(base + ADDR_SET_GLOBAL_BGM);
        auto set_dummy  = reinterpret_cast<void(*)(float)>(base + ADDR_SET_DUMMY_VOL);
        auto set_stream = reinterpret_cast<bool(*)(float, uint32_t)>(base + ADDR_SET_STREAM_VOL);
        auto set_queue  = reinterpret_cast<void(*)(float, uint32_t)>(base + ADDR_SET_QUEUE_VOL);

        int32_t raw_bgm   = *reinterpret_cast<int32_t*>(base + ADDR_DAT_BGM_SETTING);
        int32_t raw_dummy = *reinterpret_cast<int32_t*>(base + ADDR_DAT_DUMMY_SETTING);
        int32_t raw_btn   = *reinterpret_cast<int32_t*>(base + ADDR_DAT_BTN_SETTING);
        int32_t raw_se    = *reinterpret_cast<int32_t*>(base + ADDR_DAT_SE_SETTING);

        if (Config::acapellaMode) {
            if (raw_se  <= 0) raw_se = 10;
        } else {
            if (raw_se < 0) raw_se = 10;
        }
        if (raw_bgm < 0) raw_bgm = 10;
        if (raw_btn < 0) raw_btn = 10;

        float bgm   = ((float)raw_bgm * 0.6f) / 10.0f;
        float dummy = ((float)raw_dummy / 10.0f) * 0.6f;
        float btn   = ((float)raw_btn * 0.6f) / 10.0f;
        float se    = ((float)raw_se  * 0.6f) / 10.0f;

        set_bgm(bgm);
        set_dummy(dummy);
        set_stream(se, 1);
        set_queue(btn, 3);
        set_queue(btn, 4);

        set_queue(1.0f, 0);
        set_queue(1.0f, 1);
        set_queue(1.0f, 2);
        set_queue(1.0f, 5);
    }
};

void ApplyBoost() {
    uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
    auto apply_sound_settings = reinterpret_cast<void(*)()>(base + ADDR_APPLY_SOUND_SETTINGS);
    if (apply_sound_settings) {
        apply_sound_settings();
    }
}

// =========================================================================
// 6. INITIALIZATION
// =========================================================================

inline uintptr_t ResolveSymbol(const char* sym) {
    for (int i = static_cast<int>(exl::util::ModuleIndex::Start);
         i < static_cast<int>(exl::util::ModuleIndex::End); i++) {
        auto index = static_cast<exl::util::ModuleIndex>(i);
        if (exl::util::HasModule(index)) {
            Elf_Sym* symbol = exl::reloc::GetSymbol(index, sym);
            if (symbol != nullptr) {
                return exl::util::GetModuleInfo(index).m_Total.m_Start + symbol->st_value;
            }
        }
    }
    return 0;
}

void Init() {
    SoundCueLoadHook::InstallAtOffset(0x608a20);

    uintptr_t append_addr = ResolveSymbol("_ZN2nn5audio16AppendWaveBufferEPNS0_9VoiceTypeEPKNS0_10WaveBufferE");
    if (append_addr) {
        AppendWaveBufferHook::InstallAtFuncPtr(reinterpret_cast<decltype(&AppendWaveBufferHook::Callback)>(append_addr));
    }

    uintptr_t play_state_addr = ResolveSymbol("_ZN2nn5audio17SetVoicePlayStateEPNS0_9VoiceTypeENS1_9PlayStateE");
    if (play_state_addr) {
        s_SetVoicePlayState = reinterpret_cast<SetVoicePlayStateFn>(play_state_addr);
    }

    SetHandleVolumeHook::InstallAtOffset(ADDR_SET_HANDLE_VOL);
    SetGlobalBgmVolumeHook::InstallAtOffset(ADDR_SET_GLOBAL_BGM);
    SetQueueVolumeHook::InstallAtOffset(ADDR_SET_QUEUE_VOL);
    SetBgmStreamVolumeHook::InstallAtOffset(ADDR_SET_DUMMY_VOL);
    SetStreamVolumeHook::InstallAtOffset(ADDR_SET_STREAM_VOL);
    ApplySoundSettingsHook::InstallAtOffset(ADDR_APPLY_SOUND_SETTINGS);

    ApplyBoost();
}

} // namespace SustainAudio
