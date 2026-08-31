#pragma once

#include <stdint.h>

enum TechZoneStyle : int32_t
{
	TechZoneStyle_F     = 0,
	TechZoneStyle_F2nd  = 1,
	TechZoneStyle_X     = 2,
	TechZoneStyle_FT    = 3,
	TechZoneStyle_FS    = 4,
	TechZoneStyle_CT    = 5,
	TechZoneStyle_M39   = 6,
	TechZoneStyle_Match = 20, // NOTE: Match UI
	TechZoneStyle_Song  = 21  // NOTE: Song's default
};

struct TechZoneState
{
	int32_t first_target_index = -1;
	int32_t last_target_index = -1;
	int32_t targets_hit = 0;
	bool failed = false;

	TechZoneState() = default;
	~TechZoneState() = default;

	TechZoneState(int32_t first, int32_t last)
	{
		if (first > -1 && last > -1 && last > first)
		{
			first_target_index = first;
			last_target_index = last;
		}

		targets_hit = 0;
		failed = false;
	}

	bool IsValid() const;
	bool IsSuccessful() const;
	int32_t GetTargetCount() const;
	int32_t GetRemainingCount() const;
	bool PushNewHitState(int32_t target_index, int32_t hit_state);
	void ResetPlayState();
};

struct TechZoneDispState
{
	TechZoneState* data;
	uint32_t scene;
	std::string layer_name;
	int32_t prio;
	int32_t state;
	bool end;
	bool fail_in;

	TechZoneDispState()
	{
		data = nullptr;
		scene = 0;
		layer_name = "";
		prio = 4;
		state = 0;
		end = false;
		fail_in = false;
	}

	void Reset();
	void Ctrl();
	void Disp() const;
};
