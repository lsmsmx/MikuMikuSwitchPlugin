#include "../util.hpp"
#include "../sound_db.hpp"
#include "../nc_state.hpp"
#include "../save_data.hpp"
#include "sound_effects.hpp"
#include "logger.hpp"

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
	if (long_se_active && (now - long_se_start < std::chrono::milliseconds(50)))
		return;

	sound::PlaySoundEffect(QueueIndex, l_button_on.c_str(), 1.0f);
	long_se_active = true;
	long_se_start = now;
}

void SoundEffectManager::EndLongSE(bool fail)
{
	long_se_active = false;
	sound::ReleaseCue(QueueIndex, l_button_on.c_str(), !fail);
	sound::ReleaseCue(SustainQueueIndex, l_button_on.c_str(), true);
	if (!fail)
		sound::PlaySoundEffect(QueueIndex, l_button_off.c_str(), 1.0f);
}

void SoundEffectManager::TickLongSE()
{
}

void SoundEffectManager::StartRushBackSE() { sound::PlaySoundEffect(QueueIndex, rush_on.c_str(), 1.0f); }

void SoundEffectManager::EndRushBackSE(bool popped)
{
	sound::ReleaseCue(QueueIndex, rush_on.c_str(), popped);
	if (popped)
		sound::PlaySoundEffect(QueueIndex, rush_off.c_str(), 1.0f);
}

void SoundEffectManager::StartLinkSE() { sound::PlaySoundEffect(QueueIndex, link.c_str(), 1.0f); }
void SoundEffectManager::EndLinkSE() { sound::ReleaseCue(QueueIndex, link.c_str(), false); }

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
