#include "sound_db.hpp"

static std::vector<SoundInfo> button_w_sounds = {
    { 1, "Double A", "button_w1_mmv" },
    { 2, "Double B", "button_w2_mmv" },
    { 3, "Double C", "button_w3_mmv" },
    { 4, "Double D", "button_w4_mmv" },
    { 5, "Double E", "button_w5_mmv" }
};

static std::vector<SoundInfo> star_sounds = {
    { 1, "Star A", "scratch1_mmv" },
    { 2, "Star B", "scratch2_mmv" },
    { 3, "Star C", "scratch3_mmv" },
    { 4, "Star D", "scratch4_mmv" },
    { 5, "Star E", "scratch5_mmv" },
    { 6, "Star F", "scratch6_mmv" },
    { 7, "Star G", "scratch7_mmv" },
    { 8, "Star H", "scratch8_mmv" },
    { 9, "Star I", "scratch9_mmv" }
};

static std::vector<SoundInfo> star_w_sounds = {
    { 1, "D-Star A", "scratch_w1_mmv" },
    { 2, "D-Star B", "scratch_w2_mmv" },
    { 3, "D-Star C", "scratch_w3_mmv" },
    { 4, "D-Star D", "scratch_w4_mmv" },
    { 5, "D-Star E", "scratch_w5_mmv" }
};

static std::vector<SoundInfo> link_star_sounds = {
    { 1, "Link A", "scratch_l1_mmv", "se_nc_option_preview_sl1" },
    { 2, "Link B", "scratch_l2_mmv", "se_nc_option_preview_sl2" },
    { 3, "Link C", "scratch_l3_mmv", "se_nc_option_preview_sl3" },
    { 4, "Link D", "scratch_l4_mmv", "se_nc_option_preview_sl4" },
    { 5, "Link E", "scratch_l5_mmv", "se_nc_option_preview_sl5" }
};

static std::vector<SoundInfo> button_lon_sounds = {
    { 1, "Sustain A", "button_l1_on_mmv", "se_nc_option_preview_l1" },
    { 2, "Sustain B", "button_l2_on_mmv", "se_nc_option_preview_l2" },
    { 3, "Sustain C", "button_l3_on_mmv", "se_nc_option_preview_l3" },
    { 4, "Sustain D", "button_l4_on_mmv", "se_nc_option_preview_l4" },
    { 5, "Sustain E", "button_l5_on_mmv", "se_nc_option_preview_l5" }
};

static std::vector<SoundInfo> button_loff_sounds = {
    { 1, "Sustain A", "button_l1_off_mmv", "se_nc_option_preview_l1" },
    { 2, "Sustain B", "button_l2_off_mmv", "se_nc_option_preview_l2" },
    { 3, "Sustain C", "button_l3_off_mmv", "se_nc_option_preview_l3" },
    { 4, "Sustain D", "button_l4_off_mmv", "se_nc_option_preview_l4" },
    { 5, "Sustain E", "button_l5_off_mmv", "se_nc_option_preview_l5" }
};

const std::vector<SoundInfo>* sound_db::GetStarSoundDB()
{
	return &star_sounds;
}

const std::vector<SoundInfo>* sound_db::GetStarWSoundDB()
{
    return &star_w_sounds;
}

const std::vector<SoundInfo>* sound_db::GetLinkSoundDB()
{
    return &link_star_sounds;
}

const std::vector<SoundInfo>* sound_db::GetButtonWSoundDB()
{
    return &button_w_sounds;
}

const std::vector<SoundInfo>* sound_db::GetButtonLongOnSoundDB()
{
    return &button_lon_sounds;
}

const std::vector<SoundInfo>* sound_db::GetButtonLongOffSoundDB()
{
    return &button_loff_sounds;
}
