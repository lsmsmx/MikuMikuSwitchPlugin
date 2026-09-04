#pragma once
#include "lib.hpp"
#include "keyboard_sliders.hpp"
#include <stdint.h>
#include <cmath>
#include <nn/os.hpp>
#include <lib/reloc/reloc.hpp>
#include <lib/util/sys/mem_layout.hpp>

namespace nn::hid {
    inline bool g_keyboardModeEnabled = false;

    struct NpadHandheldState {
        int64_t samplingNumber;
        uint64_t buttons;
        int32_t analogStickL[2];
        int32_t analogStickR[2];
        uint32_t attributes;
        uint32_t reserved;
        uint64_t padding[4];
    };

    inline const uint32_t CONTROLLER_PLAYER_1 = 0x0;
    inline const uint32_t CONTROLLER_HANDHELD = 0x20;

    enum Button : uint64_t {
        A = (1ULL << 0), B = (1ULL << 1), X = (1ULL << 2), Y = (1ULL << 3),
        LStick = (1ULL << 4), RStick = (1ULL << 5), L = (1ULL << 6), R = (1ULL << 7),
        ZL = (1ULL << 8), ZR = (1ULL << 9), Plus = (1ULL << 10), Minus = (1ULL << 11),
        Left = (1ULL << 12), Up = (1ULL << 13), Right = (1ULL << 14), Down = (1ULL << 15)
    };

    typedef void (*GetNpadStateFunc)(NpadHandheldState* out, const uint32_t& id);
    inline GetNpadStateFunc GetNpadStateHandheld = nullptr;
    inline GetNpadStateFunc GetNpadStateFullKey = nullptr;
    inline GetNpadStateFunc GetNpadStateJoyDual = nullptr;

    struct TouchState {
        uint64_t deltaTime; uint32_t attributes; uint32_t fingerId;
        uint32_t x; uint32_t y; uint32_t diameterX; uint32_t diameterY;
        uint32_t rotationAngle; uint32_t reserved;
    };
    struct TouchScreenState {
        int64_t samplingNumber; int32_t count; uint32_t reserved;
        TouchState touches[16];
    };
    struct MouseState {
        int64_t samplingNumber; int32_t x; int32_t y; int32_t deltaX; int32_t deltaY;
        int32_t wheelDeltaX; int32_t wheelDeltaY; uint32_t buttons; uint32_t attributes;
    };
    struct KeyboardState {
        int64_t samplingNumber; uint64_t modifiers; uint64_t keys[4];
    };

    typedef void (*InitializeDeviceFunc)();
    // Fixed: Matches void (TouchScreenState*) from your Ghidra
    typedef void (*GetTouchScreenStateFunc)(TouchScreenState* outState);
    typedef void (*GetMouseStateFunc)(MouseState* out);
    typedef void (*GetKeyboardStateFunc)(KeyboardState* out);

    inline InitializeDeviceFunc InitializeTouchScreen = nullptr;
    inline InitializeDeviceFunc InitializeMouse = nullptr;
    inline InitializeDeviceFunc InitializeKeyboard = nullptr;
    inline GetTouchScreenStateFunc GetTouchScreenState = nullptr;
    inline GetMouseStateFunc GetMouseState = nullptr;
    inline GetKeyboardStateFunc GetKeyboardState = nullptr;

    inline bool isHidInitialized = false;

    inline void Initialize() {
        if (isHidInitialized) return;

        const char* symHandheld = "_ZN2nn3hid12GetNpadStateEPNS0_17NpadHandheldStateERKj";
        const char* symFullKey  = "_ZN2nn3hid12GetNpadStateEPNS0_16NpadFullKeyStateERKj";
        const char* symJoyDual  = "_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyDualStateERKj";
        const char* symInitTouch = "_ZN2nn3hid21InitializeTouchScreenEv";
        // EXACT SYMBOL FROM YOUR GHIDRA SCREENSHOT:
        const char* symGetTouch  = "_ZN2nn3hid19GetTouchScreenStateILm4EEEvPNS0_16TouchScreenStateIXT_EEE";
        const char* symInitMouse = "_ZN2nn3hid15InitializeMouseEv";
        const char* symGetMouse  = "_ZN2nn3hid13GetMouseStateEPNS0_10MouseStateE";
        const char* symInitKb    = "_ZN2nn3hid18InitializeKeyboardEv";
        const char* symGetKb     = "_ZN2nn3hid16GetKeyboardStateEPNS0_13KeyboardStateE";

        for(int i = static_cast<int>(exl::util::ModuleIndex::Start); i < static_cast<int>(exl::util::ModuleIndex::End); i++) {
            auto index = static_cast<exl::util::ModuleIndex>(i);
            if(exl::util::HasModule(index)) {
                uintptr_t base = exl::util::GetModuleInfo(index).m_Total.m_Start;
                auto resolve = [&](const char* sym, auto& ptr) {
                    Elf_Sym* s = exl::reloc::GetSymbol(index, sym);
                    if (s && !ptr) ptr = reinterpret_cast<std::remove_reference_t<decltype(ptr)>>(base + s->st_value);
                };
                resolve(symHandheld, GetNpadStateHandheld);
                resolve(symFullKey, GetNpadStateFullKey);
                resolve(symJoyDual, GetNpadStateJoyDual);
                resolve(symInitTouch, InitializeTouchScreen);
                resolve(symGetTouch, GetTouchScreenState);
                resolve(symInitMouse, InitializeMouse);
                resolve(symGetMouse, GetMouseState);
                resolve(symInitKb, InitializeKeyboard);
                resolve(symGetKb, GetKeyboardState);
            }
        }

        if (InitializeTouchScreen) InitializeTouchScreen();
        if (InitializeMouse) InitializeMouse();
        if (InitializeKeyboard) InitializeKeyboard();

        isHidInitialized = true;
    }

    inline NpadHandheldState GetMergedNpadState() {
        static NpadHandheldState cached_state = {};
        static uint64_t last_poll_tick = 0;
        uint64_t current_tick = nn::os::GetSystemTick().GetInt64Value();

        if (current_tick - last_poll_tick < 76800) {
            return cached_state;
        }
        last_poll_tick = current_tick;

        NpadHandheldState merged = {};

        if (GetNpadStateHandheld) {
            GetNpadStateHandheld(&merged, CONTROLLER_HANDHELD);
            if ((merged.attributes & 1) != 0) {
                keyboard_sliders::Inject(&merged);
                cached_state = merged;
                return merged;
            }
        }

        auto merge_sticks = [&](const NpadHandheldState& s) {
            if (std::abs(s.analogStickL[0]) > std::abs(merged.analogStickL[0])) merged.analogStickL[0] = s.analogStickL[0];
            if (std::abs(s.analogStickL[1]) > std::abs(merged.analogStickL[1])) merged.analogStickL[1] = s.analogStickL[1];
            if (std::abs(s.analogStickR[0]) > std::abs(merged.analogStickR[0])) merged.analogStickR[0] = s.analogStickR[0];
            if (std::abs(s.analogStickR[1]) > std::abs(merged.analogStickR[1])) merged.analogStickR[1] = s.analogStickR[1];
        };

        NpadHandheldState sFK = {}, sJD = {};
        if (GetNpadStateFullKey) GetNpadStateFullKey(&sFK, CONTROLLER_PLAYER_1);
        if (GetNpadStateJoyDual) GetNpadStateJoyDual(&sJD, CONTROLLER_PLAYER_1);
        merged.buttons |= sFK.buttons | sJD.buttons;
        merge_sticks(sFK); merge_sticks(sJD);

        keyboard_sliders::Inject(&merged);

        cached_state = merged;
        return merged;
    }
}
