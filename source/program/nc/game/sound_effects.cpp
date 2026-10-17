#include "../util.hpp"
#include "../sound_db.hpp"
#include "../nc_state.hpp"
#include "../save_data.hpp"
#include "sound_effects.hpp"
#include "../../Sound.hpp"
#include "logger.hpp"
#include "lib.hpp"
#include <nn/os.hpp>

void SoundEffectManager::Init()
{
	auto tryFindSound = [&](int32_t id, const std::vector<SoundInfo>& data, std::string fallback = "", int32_t default_id = -1)
	{
		if (id > 0)
		{
			if (auto* info = util::FindWithID(data, id); info != nullptr)
				return info->se_name;
		}
		else if (id == -2 && default_id != -1 && state.nc_song_entry.has_value())
		{
			switch (default_id)
			{
			case 0:
				return state.nc_song_entry.value().star_se_name;
			default:
				break;
			}
		}

		return fallback;
	};

	const auto& config = *nc::GetConfigSet();
	button = sound_effects::GetGameSoundEffect(0);

	w_button = tryFindSound(config.button_w_se_id, *sound_db::GetButtonWSoundDB(), button);
	l_button_on = tryFindSound(config.button_l_se_id, *sound_db::GetButtonLongOnSoundDB(), button);
	l_button_off = tryFindSound(config.button_l_se_id, *sound_db::GetButtonLongOffSoundDB(), button);
	star = tryFindSound(config.star_se_id, *sound_db::GetStarSoundDB(), "", 0);
	w_star = tryFindSound(config.star_w_se_id, *sound_db::GetStarWSoundDB(), star);
	link = tryFindSound(config.link_se_id, *sound_db::GetLinkSoundDB(), "");
	rush_on = "se_pv_button_rush1_on";
	rush_off = "se_pv_button_rush1_off";
}

void SoundEffectManager::PlayButtonSE() { sound::PlaySoundEffect(QueueIndex, button.c_str(), 1.0f); }
void SoundEffectManager::PlayDoubleSE() { sound::PlaySoundEffect(QueueIndex, w_button.c_str(), 1.0f); }
void SoundEffectManager::PlayCymbalSE() { sound::PlaySoundEffect(QueueIndex, "cymbal_mmv", 1.0f); }
void SoundEffectManager::PlayStarDoubleSE() { sound::PlaySoundEffect(QueueIndex, w_star.c_str(), 1.0f); }

void SoundEffectManager::PlayStarSE()
{
	sound::PlaySoundEffect(QueueIndex, star.c_str(), 1.0f);
}

void SoundEffectManager::StartLongSE()
{
	auto now = std::chrono::steady_clock::now();

	// Clean up hanging hold sound from previous pause/restart before starting a new one
	if (long_se_active)
	{
		if (now - long_se_start < std::chrono::milliseconds(50))
			return;

		EndLongSE(true);
	}

	// 1. Attack click (Queue 3)
	sound::PlaySoundEffect(QueueIndex, l_button_on.c_str(), 1.0f);

	// 2. Loop cue (SustainQueueIndex = Queue 4)
	sound::PlaySoundEffect(SustainQueueIndex, l_button_on.c_str(), 1.0f);

	long_se_active = true;
	long_se_start = now;
}

void SoundEffectManager::EndLongSE(bool fail)
{
	SustainAudio::StopAll();
	long_se_active = false;
	sound::ReleaseCue(QueueIndex, l_button_on.c_str(), !fail);
	sound::ReleaseCue(SustainQueueIndex, l_button_on.c_str(), true);
	if (!fail)
		sound::PlaySoundEffect(QueueIndex, l_button_off.c_str(), 1.0f);
}

static constexpr int64_t TICKS_PER_MS = 19200LL;
static constexpr int64_t RUSH_INTRO_TICKS = 2567LL * TICKS_PER_MS; // 49,286,400 ticks (2.567s)

static bool    s_rush_active  = false;
static bool    s_rush_looping = false;
static int64_t s_rush_start_tick = 0;
static int64_t s_last_log_ms = 0;

static int     s_link_count   = 0;
static bool    s_link_looping = false;
static int64_t s_link_start_tick = 0;
static int64_t s_link_intro_ticks = 0;
static std::string s_link_sound_name;

// Calculate intro duration from sample properties in property_nc.txt:
static inline int64_t GetLinkIntroTicks(const std::string& name) {
    if (name.find("scratch_l1") != std::string::npos) return 0; // loop_start = 0
    if (name.find("scratch_l2") != std::string::npos) return 0; // loop_start = 0
    if (name.find("scratch_l3") != std::string::npos) return 146580LL * 19200LL / 48000LL; // 3054 ms
    if (name.find("scratch_l4") != std::string::npos) return 36008LL  * 19200LL / 48000LL; // 750 ms
    if (name.find("scratch_l5") != std::string::npos) return 35308LL  * 19200LL / 48000LL; // 735 ms
    return 0;
}

void SoundEffectManager::StartRushBackSE()
{
    if (s_rush_active) return;

    s_rush_active  = true;
    s_rush_looping = false;
    s_rush_start_tick = nn::os::GetSystemTick().GetInt64Value();
    s_last_log_ms = 0;

    sound::PlaySoundEffect(QueueIndex, rush_on.c_str(), 1.0f);
}

void SoundEffectManager::EndRushBackSE(bool popped)
{
    s_rush_active  = false;
    s_rush_looping = false;

    SustainAudio::StopSlot(6);
    sound::ReleaseCue(QueueIndex, rush_on.c_str(), popped);
    sound::ReleaseCue(SoundEffectManager::SustainQueueIndex, rush_on.c_str(), true);

    if (popped)
        sound::PlaySoundEffect(QueueIndex, rush_off.c_str(), 1.0f);
}

void SoundEffectManager::StartLinkSE()
{
    s_link_count++;

    // If already playing from the first star, subsequent stars maintain playback
    if (s_link_count > 1) return;

    s_link_sound_name  = link;
    s_link_intro_ticks = GetLinkIntroTicks(link);
    s_link_looping     = false;
    s_link_start_tick  = nn::os::GetSystemTick().GetInt64Value();

    // 1. Attack / intro playback in Queue 3
    sound::PlaySoundEffect(QueueIndex, link.c_str(), 1.0f);

    // 2. If scratch loop_start == 0 (scratch_l1 and l2), start sustain loop immediately
    if (s_link_intro_ticks == 0) {
        s_link_looping = true;
        sound::PlaySoundEffect(SustainQueueIndex, link.c_str(), 1.0f);
    }
}

void SoundEffectManager::EndLinkSE()
{
    if (s_link_count > 0) {
        s_link_count--;
    }

    // Stop sound only after all active stars are released (counter drops to 0)
    if (s_link_count == 0) {
        s_link_looping = false;

        // Stop scratch slots 7..11
        for (int i = 7; i <= 11; i++) {
            SustainAudio::StopSlot(i);
        }

        sound::ReleaseCue(QueueIndex, link.c_str(), false);
        sound::ReleaseCue(SustainQueueIndex, link.c_str(), true);
    }
}

namespace sound_effects {
    void ResetState() {
        s_rush_active  = false;
        s_rush_looping = false;
        s_link_count   = 0;
        s_link_looping = false;
    }

    void UpdateSELoop() {
        // Rush:
        if (s_rush_active && !s_rush_looping) {
            int64_t diff = nn::os::GetSystemTick().GetInt64Value() - s_rush_start_tick;
            if (diff >= RUSH_INTRO_TICKS) {
                s_rush_looping = true;
                sound::PlaySoundEffect(SoundEffectManager::SustainQueueIndex, "se_pv_button_rush1_on", 1.0f);
            }
        }

        // Long scratches (l3, l4, l5):
        if (s_link_count > 0 && !s_link_looping) {
            int64_t diff = nn::os::GetSystemTick().GetInt64Value() - s_link_start_tick;
            if (diff >= s_link_intro_ticks) {
                s_link_looping = true;
                sound::PlaySoundEffect(SoundEffectManager::SustainQueueIndex, s_link_sound_name.c_str(), 1.0f);
            }
        }
    }
}

std::string sound_effects::GetGameSoundEffect(int32_t kind)
{
	if (kind < 0 || kind >= 6) return "";
	int32_t pv_id = game::GetGlobalPvID();
	int32_t difficulty = util::Clamp(game::GetGlobalPVInfo()->difficulty, 0, 3);

	if (auto* data = static_cast<int32_t*>(game::GetConfigSet(game::GetSaveData(), pv_id, false)); data != nullptr)
	{
		int32_t id = data[kind];
		if (kind == 0)
		{
			if (id == 0)
			{
				if (auto* pv_entry = pv_db::FindPVEntry(pv_id); pv_entry != nullptr)
				{
					if (auto* diff_entry = pv_db::FindDifficulty(pv_entry, difficulty, 0); diff_entry != nullptr)
						return std::string(diff_entry->button_se);
				}
			}
			if (auto* se = game::GetButtonSE(id); se != nullptr)
				return std::string(se->se_name);
		}
		else if (kind == 3)
		{
			return id == 1 ? "se_ft_option_preview_laser" : "se_ft_option_preview_windchime";
		}
	}
	return "";
}
