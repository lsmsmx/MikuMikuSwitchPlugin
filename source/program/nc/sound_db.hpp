#pragma once
#include <stdint.h>
#include <string>
#include <vector>

struct SoundInfo
{
	int32_t id;
	std::string name;
	std::string se_name;
	std::string se_preview_name;
};

namespace sound_db
{
	const std::vector<SoundInfo>* GetStarSoundDB();
	const std::vector<SoundInfo>* GetStarWSoundDB();
	const std::vector<SoundInfo>* GetLinkSoundDB();
	const std::vector<SoundInfo>* GetButtonWSoundDB();
	const std::vector<SoundInfo>* GetButtonLongOnSoundDB();
	const std::vector<SoundInfo>* GetButtonLongOffSoundDB();
}
