#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <bitset>
#include <array>
#include <string.h>

#include "lib.hpp"
#include "diva_nc.hpp"
#include "save_data.hpp"
#include "input.hpp"
#include "logger.hpp"
#include "../hid.hpp"

constexpr int32_t NearFramesBaseCount = 3;
constexpr float NearFramesBaseRate = 60.0f;
constexpr size_t MaxKeyCount = 0x73;

static int32_t g_input_frame = 0;

struct RepeatTapMemory
{
	float float00;
	float float04;
	int32_t dword08;
	int32_t dword0C;

	RepeatTapMemory()
	{
		float00 = 3.0f;
		float04 = 40.0f;
		dword08 = 0;
		dword0C = 0;
	}
};

struct InputStateExData
{
	std::bitset<MaxKeyCount> down;
	std::bitset<MaxKeyCount> repeat_tap;
	std::array<RepeatTapMemory, MaxKeyCount> repeat_tap_memory;
	bool blocked;

	InputStateExData()
	{
		repeat_tap_memory.fill(RepeatTapMemory());
		blocked = false;
	}

	void ResetMultiFrameInputs()
	{
		for (size_t i = 0; i < MaxKeyCount; i++)
		{
			repeat_tap_memory[i].dword08 = 0;
			repeat_tap_memory[i].dword0C = 0;
		}
	}
};

static std::unordered_map<diva_nc::InputState*, InputStateExData> input_ex_data;

enum ButtonIndex : int32_t
{
	ButtonIndex_Circle = 0,
	ButtonIndex_Cross = 1,
	ButtonIndex_Triangle = 2,
	ButtonIndex_Square = 3,
	ButtonIndex_L = 4,
	ButtonIndex_05 = 5,
	ButtonIndex_06 = 6,
	ButtonIndex_R = 7
};

enum GameButton : int32_t
{
	GameButton_Up = 1,
	GameButton_Right = 2,
	GameButton_Down = 4,
	GameButton_Left = 8,
	GameButton_Triangle = 16,
	GameButton_Circle = 32,
	GameButton_Cross = 64,
	GameButton_Square = 128,
	GameButton_L1       = 256,
	GameButton_L2       = 512,
	GameButton_L3       = 1024,
	GameButton_L4       = 2048,
	GameButton_R1       = 4096,
	GameButton_R2       = 8192,
	GameButton_R3       = 16384,
	GameButton_R4       = 32768,
};

// Switch function pointers:
inline FUNCTION_PTR(int32_t, __fastcall, GetPvKeyStateDown, 0x1b7750, void* handler, int32_t key);
inline FUNCTION_PTR(int32_t, __fastcall, GetPvKeyStateTapped, 0x1b77a0, void* handler, int32_t key);
inline FUNCTION_PTR(bool, __fastcall, PollRepeatTapInput, 0x1fc420, RepeatTapMemory* a1, float a2, bool is_down);

ButtonState::StateData& ButtonState::Push(const std::chrono::steady_clock::time_point& time)
{
	size_t to_move = count < HISTORY_CAP ? count : (HISTORY_CAP - 1);
	for (size_t i = to_move; i > 0; i--) {
		data[i] = data[i - 1];
	}

	data[0] = StateData{false, false, false, false, time};
	if (count < HISTORY_CAP) count++;

	return data[0];
}

bool ButtonState::IsTappedInNearFrames() const
{
	if (count == 0) return false;
	for (size_t i = 0; i < count; i++)
	{
		if (std::chrono::duration_cast<std::chrono::milliseconds>(data[0].time - data[i].time).count() > 200)
			return false;

		if (data[i].tapped)
			return true;
	}
	return false;
}

static void UpdateButtonState(ButtonState* state, const int32_t* down_buffer, const int32_t* tapped_buffer, int32_t index, int32_t mask)
{
	state->data[0].down = (down_buffer[index] & mask) != 0;
	state->data[0].up = !state->data[0].down;
	state->data[0].tapped = (tapped_buffer[index] & mask) != 0;
}

bool MacroState::Update(void* internal_handler, int32_t player_index)
{
	if (internal_handler == nullptr)
		return false;

	diva_nc::InputState* diva_input = diva_nc::GetInputState(player_index);
	device = diva_input->GetDevice();

	auto time = std::chrono::steady_clock::now();

	for (int32_t i = 0; i < Button_Max; i++)
		buttons[i].Push(time);

	int32_t key_down[8];
	int32_t key_tapped[8];
	for (int i = 0; i < 8; i++)
	{
		key_down[i] = GetPvKeyStateDown(internal_handler, i);
		key_tapped[i] = GetPvKeyStateTapped(internal_handler, i);
	}

	UpdateButtonState(&buttons[Button_Triangle], key_down, key_tapped, ButtonIndex_Triangle, GameButton_Triangle);
	UpdateButtonState(&buttons[Button_Circle], key_down, key_tapped, ButtonIndex_Circle, GameButton_Circle);
	UpdateButtonState(&buttons[Button_Cross], key_down, key_tapped, ButtonIndex_Cross, GameButton_Cross);
	UpdateButtonState(&buttons[Button_Square], key_down, key_tapped, ButtonIndex_Square, GameButton_Square);
	UpdateButtonState(&buttons[Button_Up], key_down, key_tapped, ButtonIndex_Triangle, GameButton_Up);
	UpdateButtonState(&buttons[Button_Right], key_down, key_tapped, ButtonIndex_Circle, GameButton_Right);
	UpdateButtonState(&buttons[Button_Down], key_down, key_tapped, ButtonIndex_Cross, GameButton_Down);
	UpdateButtonState(&buttons[Button_Left], key_down, key_tapped, ButtonIndex_Square, GameButton_Left);
	UpdateButtonState(&buttons[Button_L1], key_down, key_tapped, ButtonIndex_L, GameButton_L1);
	UpdateButtonState(&buttons[Button_L2], key_down, key_tapped, ButtonIndex_L, GameButton_L2);
	UpdateButtonState(&buttons[Button_L3], key_down, key_tapped, ButtonIndex_L, GameButton_L3);
	UpdateButtonState(&buttons[Button_L4], key_down, key_tapped, ButtonIndex_L, GameButton_L4);
	UpdateButtonState(&buttons[Button_R1], key_down, key_tapped, ButtonIndex_R, GameButton_R1);
	UpdateButtonState(&buttons[Button_R2], key_down, key_tapped, ButtonIndex_R, GameButton_R2);
	UpdateButtonState(&buttons[Button_R3], key_down, key_tapped, ButtonIndex_R, GameButton_R3);
	UpdateButtonState(&buttons[Button_R4], key_down, key_tapped, ButtonIndex_R, GameButton_R4);

	for (int i = 0; i < Button_Max; i++)
	{
		buttons[i].data[0].released = buttons[i].data[0].up && buttons[i].data[1].down;
	}

	UpdateSticks(diva_input, time);
	UpdateRawButtons();
	return true;
}

void MacroState::UpdateRawButtons()
{
	if (g_input_frame == last_raw_frame)
		return;
	last_raw_frame = g_input_frame;

	auto time = std::chrono::steady_clock::now();

	uint64_t hid_down = 0;
	if (nn::hid::GetNpadStateHandheld)
	{
		nn::hid::NpadHandheldState sHH = {};
		nn::hid::GetNpadStateHandheld(&sHH, nn::hid::CONTROLLER_HANDHELD);
		hid_down |= sHH.buttons;
	}
	if (nn::hid::GetNpadStateFullKey)
	{
		nn::hid::NpadHandheldState sFK = {};
		nn::hid::GetNpadStateFullKey(&sFK, nn::hid::CONTROLLER_PLAYER_1);
		hid_down |= sFK.buttons;
	}
	if (nn::hid::GetNpadStateJoyDual)
	{
		nn::hid::NpadHandheldState sJD = {};
		nn::hid::GetNpadStateJoyDual(&sJD, nn::hid::CONTROLLER_PLAYER_1);
		hid_down |= sJD.buttons;
	}

	auto mapBtn = [&](int32_t gameBtn, uint64_t hidBit)
	{
		raw_buttons[gameBtn].Push(time);
		raw_buttons[gameBtn].data[0].down = (hid_down & hidBit) != 0;
		raw_buttons[gameBtn].data[0].up = !raw_buttons[gameBtn].data[0].down;
	};

	mapBtn(Button_Triangle, nn::hid::Button::Y);
	mapBtn(Button_Circle, nn::hid::Button::A);
	mapBtn(Button_Cross, nn::hid::Button::B);
	mapBtn(Button_Square, nn::hid::Button::X);
	mapBtn(Button_Up, nn::hid::Button::Up);
	mapBtn(Button_Right, nn::hid::Button::Right);
	mapBtn(Button_Down, nn::hid::Button::Down);
	mapBtn(Button_Left, nn::hid::Button::Left);

	const int32_t raw_mapped[] = {
		Button_Triangle, Button_Circle, Button_Cross, Button_Square,
		Button_Up, Button_Right, Button_Down, Button_Left
	};
	for (int32_t btn : raw_mapped)
	{
		raw_buttons[btn].data[0].tapped = raw_buttons[btn].data[0].down && raw_buttons[btn].data[1].up;
		raw_buttons[btn].data[0].released = raw_buttons[btn].data[0].up && raw_buttons[btn].data[1].down;
	}
}

bool MacroState::IsRawDown(int32_t button) const
{
	return button >= 0 && button < Button_Max && raw_buttons[button].IsDown();
}

bool MacroState::IsRawReleased(int32_t button) const
{
	return button >= 0 && button < Button_Max && raw_buttons[button].IsReleased();
}

void MacroState::UpdateSticks(diva_nc::InputState* input_state, const std::chrono::steady_clock::time_point& time)
{
	auto checkDeadzoned = [&](const diva_nc::vec2& pos)
	{
		return pos.x <= stick_deadzone.x && pos.y <= stick_deadzone.y
			&& pos.x >= -stick_deadzone.x && pos.y >= -stick_deadzone.y;
	};

	auto updateStick = [&](int32_t index)
	{
		StickState* state = &sticks[index];

		// Switch analog stick position indices
		int32_t baseIndex = (index == 0) ? 0x0E : 0x10;

		int32_t rawX = input_state->GetPosition(baseIndex + 0);
		int32_t rawY = input_state->GetPosition(baseIndex + 1);

		diva_nc::vec2 pos = {
			static_cast<float>(rawX) / 32767.0f,
			static_cast<float>(-(rawY)) / 32767.0f
		};

		state->prev_distance = state->distance;
		state->distance = checkDeadzoned(pos) ? 0.0f : pos.length();
		state->flicked = (state->distance >= sensivity) && (state->prev_distance < sensivity);
		state->returning = state->distance < state->prev_distance;

		if (!state->returning)
		{
			state->flicked = state->distance >= sensivity && !state->flick_block;
			if (state->flicked)
				state->flick_block = true;
		}
		else
		{
			if (state->distance < sensivity)
				state->flick_block = false;
		}
	};

	auto updateStickButtonState = [&](int32_t index)
	{
		StickState* state = &sticks[index];
		auto& button = buttons[Button_LStick + index].data[0];

		button.down = state->distance >= sensivity;
		button.up = !button.down;
		button.tapped = state->flicked;
		button.time = time;
	};

	updateStick(Stick_L);
	updateStick(Stick_R);
	updateStickButtonState(Stick_L);
	updateStickButtonState(Stick_R);
}

bool MacroState::GetStarHit() const
{
	// NOTE: Fetch Star Control setting (0: Sticks Only, 1: Buttons Only, 2: Both)
	int32_t star_control = *reinterpret_cast<const int32_t*>(&nc::GetSharedData().reserved[0]);

	// NOTE: Evaluate stick inputs
	bool hit_stick = buttons[Button_LStick].IsTapped() || buttons[Button_RStick].IsTapped();

	// NOTE: Evaluate button inputs (D-Pad + Action Buttons)
	bool hit_button = false;
	if (star_control == 1 || star_control == 2)
	{
		uint64_t main_mask = GetMainButtonsMask();
		hit_button = (GetTappedBitfield() & main_mask) != 0;
	}

	if (star_control == 0) return hit_stick;
	if (star_control == 1) return hit_button;
	if (star_control == 2) return hit_stick || hit_button;

	return false;
}

bool MacroState::GetDoubleStarHit() const
{
	// NOTE: Fetch Star Control setting
	int32_t star_control = *reinterpret_cast<const int32_t*>(&nc::GetSharedData().reserved[0]);

	// NOTE: Evaluate stick inputs (Both sticks flicked simultaneously)
	bool hit_stick = (buttons[Button_LStick].IsTapped() && buttons[Button_RStick].IsTappedInNearFrames()) ||
					 (buttons[Button_RStick].IsTapped() && buttons[Button_LStick].IsTappedInNearFrames());

	// NOTE: Evaluate button inputs (Any 2 main buttons pressed simultaneously)
	bool hit_button = false;
	if (star_control == 1 || star_control == 2)
	{
		int32_t tapped_count = 0;
		for (int32_t i = 0; i < 8; i++) // NOTE: Check 4 D-Pad arrows and 4 Action buttons
		{
			if (buttons[i].IsTapped() || buttons[i].IsTappedInNearFrames())
				tapped_count++;
		}
		hit_button = (tapped_count >= 2);
	}

	if (star_control == 0) return hit_stick;
	if (star_control == 1) return hit_button;
	if (star_control == 2) return hit_stick || hit_button;

	return false;
}

HOOK_DEFINE_TRAMPOLINE(PollInputRepeatAndDoubleHook) {
	static void Callback(diva_nc::InputState* input_state) {
		g_input_frame++;

		// 1. Run original engine input poll
		Orig(input_state);

		auto& ex = input_ex_data[input_state];

		// 2. Backup input state for mod menu navigation
		memcpy((void*)&ex.down, &input_state->_data[0x30], 0x10);
		memcpy((void*)&ex.repeat_tap, &input_state->_data[0x90], 0x10);

		// 3. Block game input when mod menu is open
		if (ex.blocked)
		{
			// Process repeat tap memory for mod menu
			memset(&ex.repeat_tap, 0, sizeof(ex.repeat_tap));
			for (size_t i = 0; i < MaxKeyCount; i++)
			{
				if (PollRepeatTapInput(&ex.repeat_tap_memory[i], 1.0f, ex.down[i]))
					ex.repeat_tap[i] = true;
			}
			memset(&input_state->_data[0x30], 0, 0x10); // down
			memset(&input_state->_data[0x48], 0, 0x10); // tapped
			memset(&input_state->_data[0x60], 0, 0x10); // released
			memset(&input_state->_data[0x90], 0, 0x10); // repeat_tap
		}
		else
		{
			ex.ResetMultiFrameInputs();
		}
	}
};

void nc::BlockInputs()
{
	for (auto& [ptr, state] : input_ex_data)
		state.blocked = true;
}

void nc::UnblockInputs()
{
	for (auto& [ptr, state] : input_ex_data)
		state.blocked = false;
}

bool nc::IsButtonTappedOrRepeat(diva_nc::InputState* input_state, int32_t key)
{
	if (!input_ex_data[input_state].blocked)
		return input_state->IsButtonTappedOrRepeat(key);

	if (key >= 0x73)
		return false;

	return input_ex_data[input_state].repeat_tap[key];
}

void nc::InstallInputHooks()
{
	// Global Switch input poll hook
	PollInputRepeatAndDoubleHook::InstallAtOffset(0x604d90);
}

bool MacroState::GetStarHitCancel() const
{
	uint64_t left = GetButtonMask(Button_L1) |
		GetButtonMask(Button_L2) |
		GetButtonMask(Button_L3) |
		GetButtonMask(Button_L4);

	uint64_t right = GetButtonMask(Button_R1) |
		GetButtonMask(Button_R2) |
		GetButtonMask(Button_R3) |
		GetButtonMask(Button_R4);

	return (GetDownBitfield() & left) != 0 && (GetDownBitfield() & right) != 0;
}

uint64_t MacroState::GetDownBitfield() const
{
	uint64_t mask = 0;
	for (size_t i = 0; i < Button_Max; i++)
		mask |= static_cast<uint64_t>(buttons[i].IsDown()) << i;
	return mask;
}

uint64_t MacroState::GetTappedBitfield() const
{
	uint64_t mask = 0;
	for (size_t i = 0; i < Button_Max; i++)
		mask |= static_cast<uint64_t>(buttons[i].IsTapped()) << i;
	return mask;
}

uint64_t MacroState::GetReleasedBitfield() const
{
	uint64_t mask = 0;
	for (size_t i = 0; i < Button_Max; i++)
		mask |= static_cast<uint64_t>(buttons[i].IsReleased()) << i;
	return mask;
}

uint64_t MacroState::GetTappedInNearFramesBitfield() const
{
	uint64_t mask = 0;
	for (size_t i = 0; i < Button_Max; i++)
		mask |= static_cast<uint64_t>(buttons[i].IsTappedInNearFrames()) << i;
	return mask;
}

uint64_t GetButtonMask(int32_t button)
{
	if (button < 0 || button > Button_Max)
		return 0;
	else if (button == Button_Max)
	{
		uint64_t mask = 0;
		for (size_t i = 0; i < Button_Max; i++)
			mask |= static_cast<uint64_t>(1) << i;
		return mask;
	}

	return static_cast<uint64_t>(1) << static_cast<uint64_t>(button);
}

uint64_t GetMainButtonsMask()
{
	uint64_t mask = 0;
	for (int i = 0; i < 8; i++)
		mask |= GetButtonMask(i);
	return mask;
}
