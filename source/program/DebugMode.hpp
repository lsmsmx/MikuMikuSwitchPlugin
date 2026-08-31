#pragma once
#include "lib.hpp"
#include "hid.hpp"
#include <array>

namespace DebugMode {

    enum class GameState : int32_t { STARTUP = 0, ADVERTISE = 1, GAME = 2, DATA_TEST = 3, TEST_MODE = 4, APP_ERROR = 5, CS_MENU = 6, CUSTOMIZE = 7, GALLERY = 8, MENU_SWITCH = 9, GAME_SWITCH = 10, TSHIRT_EDIT = 11, MAX = 12 };
    enum class GameSubState : int32_t { DATA_INITIALIZE = 0, SYSTEM_STARTUP = 1, LOGO = 2, TITLE = 3, CONCEAL = 4, PV_SEL = 5, PLAYLIST_SEL = 6, GAME = 7, DATA_TEST_MAIN = 8, DATA_TEST_MISC = 9, DATA_TEST_OBJ = 10, DATA_TEST_STG = 11, DATA_TEST_MOT = 12, DATA_TEST_COLLISION = 13, DATA_TEST_SPR = 14, DATA_TEST_AET = 15, DATA_TEST_AUTH3D = 16, DATA_TEST_CHR = 17, DATA_TEST_ITEM = 18, DATA_TEST_PERF = 19, DATA_TEST_PVSCRIPT = 20, DATA_TEST_PRINT = 21, DATA_TEST_CARD = 22, DATA_TEST_OPD = 23, DATA_TEST_SLIDER = 24, DATA_TEST_GLITTER = 25, DATA_TEST_GRAPHICS = 26, DATA_TEST_COLLECTION_CARD = 27, DATA_TEST_PAD = 28, TEST_MODE = 29, APP_ERROR = 30, UNK_31 = 31, CS_MENU = 32, CS_COMMERCE = 33, CS_OPTION_MENU = 34, CS_UNK_TUTORIAL_35 = 35, CS_CUSTOMIZE_SEL = 36, CS_UNK_TUTORIAL_37 = 37, CS_GALLERY = 38, MAX = 47 };

    struct DivaInputState {
        static constexpr int MaxButtonBit = 0x6F;
        enum InputType { Type_Tapped, Type_Released, Type_Down, Type_DoubleTapped, Type_IntervalTapped, Type_Max };
        struct ButtonState { std::array<uint32_t, 4> State; };

        ButtonState Tapped; ButtonState Released; ButtonState Down;
        uint32_t Padding_20[4]; ButtonState DoubleTapped; uint32_t Padding_30[4];
        ButtonState IntervalTapped; uint32_t Padding_38[12]; uint32_t Padding_MM[8];
        int32_t MouseX; int32_t MouseY; int32_t MouseDeltaX; int32_t MouseDeltaY;
        uint32_t Padding_AC[4];
        float LeftJoystickX; float LeftJoystickY; float RightJoystickX; float RightJoystickY;
        char KeyPadding[3]; char Key;

        uint8_t *GetInputBuffer(InputType inputType) {
            switch (inputType) {
                case Type_Tapped: return reinterpret_cast<uint8_t *>(&Tapped);
                case Type_Released: return reinterpret_cast<uint8_t *>(&Released);
                case Type_Down: return reinterpret_cast<uint8_t *>(&Down);
                case Type_DoubleTapped: return reinterpret_cast<uint8_t *>(&DoubleTapped);
                case Type_IntervalTapped: return reinterpret_cast<uint8_t *>(&IntervalTapped);
                default: return nullptr;
            }
        }

        void SetBit(uint32_t bit, bool value, InputType inputType) {
            uint8_t *data = GetInputBuffer(inputType);
            if (!data || bit >= MaxButtonBit) return;
            data[bit / 8] = value ? (data[bit / 8] | (1 << (bit % 8))) : (data[bit / 8] & ~(1 << (bit % 8)));
        }
    };

    extern bool g_DebugModeEnabled;

    // Function prototypes
    void ToggleDebugModePatches();
    void ForceGameSubState(GameState state, GameSubState substate);
    void ChangeGameSubState(GameState state, GameSubState substate);
    void RequestStateChange(GameState state, GameSubState substate);
    void ProcessDebugInputs();
    void Init();
}
