#include "Sound.hpp"
#include "lib.hpp"
#include "logger.hpp"
#include "Config.hpp"
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

static void*    s_sustainVoices[16] = {};
static int      s_pendingSlot       = -1;
static uint64_t s_officialLoopStart = 0;
static uint64_t s_officialLoopEnd   = 0;

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
// 2. SUSTAIN SLOT STOPPING MANAGEMENT
// =========================================================================

void StopSlot(int slot) {
    if (slot >= 0 && slot < 16 && s_sustainVoices[slot]) {
        void* voice = s_sustainVoices[slot];
        s_sustainVoices[slot] = nullptr;

        if (s_SetVoicePlayState) {
            s_SetVoicePlayState(voice, PlayState_Stopped);
        }
    }
}

void StopAll() {
    s_pendingSlot = -1;
    for (int i = 0; i < 16; i++) {
        StopSlot(i);
    }
}

// =========================================================================
// 3. LOOPING HOOKS
// =========================================================================

HOOK_DEFINE_TRAMPOLINE(SoundCueLoadHook) {
    static uint32_t Callback(void* cue, uint32_t queue_index, const char* se_name, float volume) {

        uint32_t ret = Orig(cue, queue_index, se_name, volume);

        if (!se_name || !cue) {
            return ret;
        }

        std::string_view name(se_name);

        // System events, pause, and menus reset all sustain voices
        if (name.find("dialog")  != std::string_view::npos ||
            name.find("sys_")    != std::string_view::npos ||
            name.find("pause")   != std::string_view::npos ||
            name.find("result")  != std::string_view::npos ||
            name.find("restart") != std::string_view::npos ||
            name.find("retry")   != std::string_view::npos ||
            name.find("clear")   != std::string_view::npos ||
            name.find("miss")    != std::string_view::npos ||
            name.find("wrong")   != std::string_view::npos) {
            StopAll();
            return ret;
        }

        int slot = GetSustainSlot(name);
        if (slot >= 0) {
            if (name.find("_off") != std::string_view::npos) {
                StopSlot(slot);
                if (s_pendingSlot == slot) {
                    s_pendingSlot = -1;
                }
                return ret;
            }

            bool isSustainQueue = (queue_index == 4);

            // slot >= 6 covers both Rush (6) and Scratch effects (7..11)
            if (isSustainQueue && (name.find("_on") != std::string_view::npos || slot >= 6)) {
                uintptr_t prop = *reinterpret_cast<uintptr_t*>((uintptr_t)cue + 0x68);
                if (prop) {
                    s_officialLoopStart = *reinterpret_cast<uint64_t*>(prop + 0x10);
                    s_officialLoopEnd   = *reinterpret_cast<uint64_t*>(prop + 0x18);
                } else {
                    s_officialLoopStart = 0;
                    s_officialLoopEnd   = 0;
                }

                // Fallback loop sample offsets for rush notes
                if (slot == 6 && (s_officialLoopEnd <= s_officialLoopStart || s_officialLoopStart == 0)) {
                    s_officialLoopStart = 123206;
                    s_officialLoopEnd   = 125305;
                }

                s_pendingSlot = slot;
            }
        }
        return ret;
    }
};

HOOK_DEFINE_TRAMPOLINE(AppendWaveBufferHook) {
    static void Callback(void* voice, const WaveBuffer* waveBuffer) {
        if (!waveBuffer) {
            Orig(voice, waveBuffer);
            return;
        }

        // Threshold > 10000 prevents intercepting short UI click sound cues
        if (s_pendingSlot >= 0 && waveBuffer->endSampleOffset > 10000) {
            int slot = s_pendingSlot;
            s_pendingSlot = -1;

            if (s_sustainVoices[slot]) {
                StopSlot(slot);
            }

            auto* wb = const_cast<WaveBuffer*>(waveBuffer);

            int32_t loopStart = static_cast<int32_t>(s_officialLoopStart);
            int32_t loopEnd   = static_cast<int32_t>(s_officialLoopEnd);

            wb->startSampleOffset = loopStart;
            wb->endSampleOffset   = loopEnd;
            wb->isLoop            = true;
            wb->isEndOfStream     = false;

            s_sustainVoices[slot] = voice;
        }

        Orig(voice, waveBuffer);
    }
};

// =========================================================================
// 4. VOLUME BOOST HOOKS (BGM / BTN / SE)
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
// 5. INITIALIZATION
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
