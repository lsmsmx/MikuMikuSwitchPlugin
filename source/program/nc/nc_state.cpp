#include "save_data.hpp"
#include "sound_db.hpp"
#include "shared.hpp"
#include "game/hit_state.hpp"
#include "nc_state.hpp"

using namespace diva_nc;

void TargetStateEx::ResetPlayState()
{
	hold_button = nullptr;
	org = nullptr;
	if (shared_data)
		shared_data->Reset();
	hit_state = HitState_None;
	force_hit_state = HitState_None;
	hit_time = 0.0f;
	flying_time_max = 0.0f;
	flying_time_remaining = 0.0f;
	delta_time_max = 0.0f;
	delta_time = 0.0f;
	length_remaining = length;
	kiseki_time = 0.0f;
	alpha = 0.0f;
	holding = false;
	success = false;
	current_step = false;
	step_state = LinkStepState_None;
	link_ending = false;
	kiseki_pos = { 0.0f, 0.0f };
	kiseki_dir = { 0.0f, 0.0f };
	kiseki_dir_norot = { 0.0f, 0.0f };
	kiseki.clear();
	vertex_count_max = 0;
	fix_long_kiseki = false;
	sustain_bonus_time = 0.0f;
	score_bonus = 0;
	double_tapped = false;
	bal_hit_count = 0;
	bal_scale = 0.0f;
	ResetAetData();
}


void TargetStateExShared::Reset()
{
	ct_score_bonus = 0;
}

void TargetStateEx::ResetAetData()
{
	aet::Stop(&target_aet);
	aet::Stop(&button_aet);
	aet::Stop(&bal_effect_aet);
	kiseki.clear();
}

bool TargetStateEx::IsChainSucessful()
{
	for (TargetStateEx* ex = this; ex != nullptr; ex = ex->next)
		if (!nc::IsHitGreat(ex->hit_state))
			return false;

	return true;
}

void TargetStateEx::StopAet(bool button, bool target, bool kiseki)
{
	if (button)
		aet::Stop(&button_aet);
	if (target)
		aet::Stop(&target_aet);
	aet::Stop(&bal_effect_aet);
	if (kiseki)
	{
		this->kiseki.clear();
		vertex_count_max = 0;
	}
}

bool TargetStateEx::SetLongNoteAet()
{
	if (org == nullptr)
		return false;

	// NOTE: Copy target aet and set play params
	target_aet = org->target_aet;
	aet::SetPlay(target_aet, false);
	aet::SetFrame(target_aet, 360.0f);

	diva_nc::vec3 scale = { 1.0f, 1.0f, 1.0f };
	aet::SetScale(target_aet, &scale);

	// NOTE: Free original aets
	org->target_aet = 0;
	aet::Stop(&org->button_aet);
	aet::Stop(&org->target_eff_aet);
	aet::Stop(&org->dword78);

	return true;
}

bool TargetStateEx::SetLinkNoteAet()
{
	if (org == nullptr || !IsLinkNoteStart())
		return false;

	target_aet = org->target_aet;
	button_aet = org->button_aet;
	org->target_aet = 0;
	org->button_aet = 0;
	aet::Stop(&org->target_eff_aet);
	aet::Stop(&org->dword78);

	return true;
}

bool TargetStateEx::SetRushNoteAet()
{
	if (org == nullptr || !IsRushNote())
		return false;

	button_aet = org->button_aet;
	aet::SetPlay(button_aet, false);
	aet::SetFrame(button_aet, bal_time);

	diva_nc::vec2 scaled_pos = GetScaledPosition(target_pos);
	diva_nc::vec3 pos = { scaled_pos.x, scaled_pos.y, 0.0f };
	aet::SetPosition(button_aet, &pos);

	diva_nc::vec3 scale = { 1.0f, 1.0f, 1.0f };
	aet::SetScale(button_aet, &scale);

	org->button_aet = 0;
	aet::Stop(&org->target_eff_aet);
	aet::Stop(&org->dword78);

	if (bal_effect_aet != 0)
		aet::SetPlay(bal_effect_aet, true);

	return true;
}

void UIState::SetLayer(int32_t index, bool visible, const char* name, int32_t prio, int32_t flags)
{
	aet::Stop(&aet_list[index]);
	aet_visibility[index] = visible;
	aet_list[index] = aet::PlayLayer(
		AetSceneID,
		prio,
		flags,
		name,
		nullptr,
		0,
		nullptr,
		nullptr,
		-1.0f,
		-1.0f,
		0,
		nullptr
	);
}

std::shared_ptr<AetElement>& UIState::GetLayer(int32_t id)
{
	if (!elements[id])
		elements[id] = std::make_shared<AetElement>();
	return elements[id];
}

std::shared_ptr<AetElement> UIState::PushHitEffect()
{
	if (!GetState()->nc_song_entry.has_value() || !GetState()->nc_song_entry->IsHitEffectsValid())
		return nullptr;

	std::shared_ptr<AetElement> ptr = nullptr;
	for (int32_t i = 0; i < 64; i++)
	{
		auto& data = hit_effect_buffer[(hit_effect_index + i) % 64];
		if (!data || data->Ended())
		{
			data = std::make_shared<AetElement>(GetState()->nc_song_entry->target_hit_effect_scene_id);
			ptr = data;
			hit_effect_index = (hit_effect_index + i) % 64;
			break;
		}
	}

	return ptr;
}

void UIState::ResetAllLayers()
{
	for (int i = 0; i < LayerUI_Max; i++)
	{
		aet::Stop(&aet_list[i]);
		elements[i].reset();
		aet_visibility[i] = false;
	}

	for (int i = 0; i < 64; i++)
		hit_effect_buffer[i].reset();
	hit_effect_index = 0;
}

void StateEx::ResetPlayState()
{
	target_references.clear();
	for (TargetStateEx& ex : target_ex)
		ex.ResetPlayState();
	chance_time.ResetPlayState();
	score.ct_score_bonus = 0;
	score.double_tap_bonus = 0;
	score.sustain_bonus = 0;
	score.link_bonus = 0;
	score.rush_bonus = 0;
	for (TechZoneState& tz : tech_zones)
		tz.ResetPlayState();
	tz_disp.Reset();
	tech_zone_index = 0;
	ResetAetData();
}

void StateEx::ResetAetData()
{
	for (int i = 0; i < MaxHitEffectCount; i++)
		aet::Stop(&effect_buffer[i]);
	effect_index = 0;
	ui.ResetAllLayers();
}

void StateEx::Reset()
{
	target_references.clear();
	target_ex.clear();
	tech_zones.clear();
	tech_zone_index = 0;
	chance_time.first_target_index = -1;
	chance_time.last_target_index = -1;
	chance_time.targets_hit = 0;
}

bool StateEx::PushTarget(TargetStateEx* ex)
{
	for (TargetStateEx* p : target_references)
		if (p == ex)
			return false;

	target_references.push_back(ex);
	return true;
}

bool StateEx::PopTarget(TargetStateEx* ex)
{
	for (auto it = target_references.begin(); it != target_references.end();)
	{
		if (*it == ex)
		{
			it = target_references.erase(it);
			continue;
		}

		it++;
	}

	return false;
}

void StateEx::PlayRushHitEffect(const diva_nc::vec2& pos, float scale, bool pop)
{
	if (effect_index >= MaxHitEffectCount)
		effect_index = 0;

	uint32_t scene_id = pop ? AetSceneID : 3;
	const char* layer_name = pop ? "bal_hit_eff" : "hit_eff00";
	int32_t max_keep = pop ? 4 : 2;

	aet::Stop(&effect_buffer[effect_index]);
	effect_buffer[effect_index] = aet::PlayLayer(
		scene_id,
		8,
		0x20000,
		layer_name,
		&pos,
		0,
		nullptr,
		nullptr,
		-1.0f,
		-1.0f,
		0,
		nullptr
	);

	diva_nc::vec3 scl = { scale, scale, 1.0f };
	aet::SetScale(effect_buffer[effect_index], &scl);

	int32_t count = 0;
	int32_t start = effect_index > 0 ? effect_index - 1 : MaxHitEffectCount - 1;
	for (int i = start; i > 0; i--)
	{
		if (i == effect_index)
			break;

		if (count + 1 > max_keep)
			aet::Stop(&effect_buffer[i]);

		count++;

		if (i == 0)
			i = MaxHitEffectCount - 1;
	}

	effect_index++;
}

int32_t StateEx::GetScoreMode() const
{
	if (GetGameStyle() == GameStyle_Console)
		return ScoreMode_F2nd;
	return ScoreMode_Arcade;
}

int32_t StateEx::GetGameStyle() const
{
	if (nc_chart_entry.has_value())
		return nc_chart_entry.value().style;
	return GameStyle_Arcade;
}

int32_t StateEx::CalculateTotalBonusScore() const
{
	return score.ct_score_bonus + score.double_tap_bonus + score.sustain_bonus + score.link_bonus + score.rush_bonus;
}

TargetStateEx* GetTargetStateEx(int32_t index, int32_t sub_index)
{
	for (TargetStateEx& ex : state.target_ex)
		if (ex.target_index == index && ex.sub_index == sub_index)
			return &ex;
	return nullptr;
}

TargetStateEx* GetTargetStateEx(const PvGameTarget* org)
{
	if (org->multi_count < 0)
		return GetTargetStateEx(org->target_index, 0);

	int32_t sub_index = 0;
	for (PvGameTarget* prev = org->prev; prev && prev->multi_count == org->multi_count; prev = prev->prev)
		sub_index++;

	return GetTargetStateEx(org->target_index, sub_index);
}

extern "C" {
	StateEx* GetState()
	{
		return &state;
	}

	int32_t GetStateGameStyle()
	{
		return GetState()->GetGameStyle();
	}

	bool SetStateSong(int32_t pv, int32_t difficulty, int32_t edition, int32_t style) {
		if (const auto* entry = db::FindSongEntry(pv); entry != nullptr) {
			GetState()->nc_song_entry = *entry;
			if (const auto* chart = entry->FindChart(difficulty, edition, style); chart != nullptr) {
				GetState()->nc_chart_entry = *chart;
				return true;
			}
		}
		return false;
	}

	void ResetStateSong() {
		GetState()->nc_song_entry.reset();
		GetState()->nc_chart_entry.reset();
	}
}
