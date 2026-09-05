#pragma once

#include <chrono>
#include <vector>
#include <stdint.h>
#include "diva_nc.hpp"

enum Button : int32_t
{
	Button_Triangle    = 0,
	Button_Circle      = 1,
	Button_Cross       = 2,
	Button_Square      = 3,
	Button_Up          = 4,
	Button_Right       = 5,
	Button_Down        = 6,
	Button_Left        = 7,
	Button_L1          = 8,
	Button_L2          = 9,
	Button_L3          = 10,
	Button_L4          = 11,
	Button_R1          = 12,
	Button_R2          = 13,
	Button_R3          = 14,
	Button_R4          = 15,
	Button_LStick      = 24,
	Button_RStick      = 25,
	Button_Max,
	Button_None
};

enum Stick : int32_t
{
	Stick_L   = 0,
	Stick_R   = 1,
	Stick_Max = 2
};

struct ButtonState
{
	struct StateData
	{
		bool down{};
		bool up{};
		bool tapped{};
		bool released{};
		std::chrono::steady_clock::time_point time;
	};

	static constexpr size_t HISTORY_CAP = 32;
	std::array<StateData, HISTORY_CAP> data{};
	size_t count = 0;

	StateData& Push(const std::chrono::steady_clock::time_point& time);

	inline bool IsDown() const { return count > 0 && data[0].down; }
	inline bool IsTapped() const { return count > 0 && data[0].tapped; }
	inline bool IsUp() const { return count > 0 && data[0].up; }
	inline bool IsReleased() const { return count > 0 && data[0].released; }
	bool IsTappedInNearFrames() const;
};

struct StickState
{
	diva_nc::vec2 pos = {0.0f, 0.0f}; // Stick position in previous frame
	float distance = 0.0f;            // Distance from center (0.0, 0.0)
	float prev_distance = 0.0f;
	bool returning = false;
	bool flicked = false;             // Stick flicked this frame
	bool flick_block = false;         // Prevent re-triggering flick
};

struct MacroState
{
	ButtonState buttons[Button_Max];
	ButtonState raw_buttons[Button_Max];
	int32_t last_raw_frame = -1;
	StickState sticks[2];
	float hold_sensivity;
	float sensivity;
	int32_t device;
	diva_nc::vec2 stick_deadzone;

	MacroState()
	{
		for (auto& s : sticks) s = StickState{};
		hold_sensivity = 0.75f;
		sensivity = 0.5f;
		device = InputDevice_Switch;
		stick_deadzone = { 0.15f, 0.15f };
	}

	bool Update(void* internal_handler, int32_t player_index);
	void UpdateSticks(diva_nc::InputState* input_state, const std::chrono::steady_clock::time_point& time);
	void UpdateRawButtons();
	bool IsRawDown(int32_t button) const;
	bool IsRawReleased(int32_t button) const;
	bool GetStarHit() const;
	bool GetDoubleStarHit() const;
	bool GetStarHitCancel() const;

	uint64_t GetDownBitfield() const;
	uint64_t GetTappedBitfield() const;
	uint64_t GetReleasedBitfield() const;
	uint64_t GetTappedInNearFramesBitfield() const;
};

namespace nc
{
	void BlockInputs();
	void UnblockInputs();
	bool IsButtonTappedOrRepeat(diva_nc::InputState* input_state, int32_t key);
	void InstallInputHooks();
}

namespace input {
	inline void init() {
		nc::InstallInputHooks();
	}
}

uint64_t GetButtonMask(int32_t button);
uint64_t GetMainButtonsMask();
