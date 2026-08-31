#pragma once

#include <string>
#include <chrono>
#include "../diva_nc.hpp"

class SoundEffectManager
{
public:
	static constexpr int32_t QueueIndex = 3;

	// Dedicated queue for sustain renewals. Renewing on the shared QueueIndex
	// churned the cue slots every other note hit uses and broke them.
	static constexpr int32_t SustainQueueIndex = 4;

	// The sustain cue is a one-shot sample (~1.8s incl. attack); re-trigger it
	// while the player is still holding so long holds never go silent. Must
	// stay below the file length to avoid a silence gap between renewals.
	static constexpr int64_t LongSeRestartMs = 1600;

	void Init();

	void PlayButtonSE();
	void PlayDoubleSE();
	void PlayStarSE();
	void PlayCymbalSE();
	void PlayStarDoubleSE();
	void StartLongSE();
	void EndLongSE(bool fail);
	void TickLongSE();
	void StartRushBackSE();
	void EndRushBackSE(bool popped);
	void StartLinkSE();
	void EndLinkSE();
private:
	std::string button;
	std::string w_button;
	std::string l_button_on;
	std::string l_button_off;
	std::string star;
	std::string w_star;
	std::string link;
	std::string rush_on;
	std::string rush_off;
	bool long_se_active = false;
	std::chrono::steady_clock::time_point long_se_start = { };
};

inline SoundEffectManager se_mgr = { };

namespace sound_effects
{
	std::string GetGameSoundEffect(int32_t kind);
}
