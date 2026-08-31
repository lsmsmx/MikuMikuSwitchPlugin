#include "../nc_state.hpp"
#include "../save_data.hpp"
#include "hit_state.hpp"
#include "tech_zone.hpp"
#include "logger.hpp"

enum TechZoneAction : int32_t
{
	TechZoneAction_Idle    = 0,
	TechZoneAction_InWait  = 1,
	TechZoneAction_Loop    = 2,
	TechZoneAction_EndWait = 3,
};

bool TechZoneState::IsValid() const
{
	return first_target_index != -1 && last_target_index != -1;
}

bool TechZoneState::IsSuccessful() const
{
	return IsValid() && !failed && GetRemainingCount() == 0;
}

int32_t TechZoneState::GetTargetCount() const
{
	if (!IsValid())
		return 0;
	return last_target_index - first_target_index + 1;
}

int32_t TechZoneState::GetRemainingCount() const
{
	if (!IsValid())
		return 0;
	return GetTargetCount() - targets_hit;
}

bool TechZoneState::PushNewHitState(int32_t target_index, int32_t hit_state)
{
	if (!IsValid())
		return false;

	if (target_index >= first_target_index && target_index <= last_target_index)
	{
		if (nc::IsHitGreat(hit_state) && !failed)
			targets_hit++;
		else
			failed = true;
		return true;
	}
	return false;
}

void TechZoneState::ResetPlayState()
{
	targets_hit = 0;
	failed = false;
}

void TechZoneDispState::Reset()
{
	data = nullptr;
	scene = 0;
	layer_name.clear();
	state = 0;
	end = false;
}

void TechZoneDispState::Ctrl()
{
	if (!data)
		return;

	if (scene == 0 || layer_name.empty())
	{
		int32_t style = nc::GetSharedData().tech_zone_style;

		if (style == TechZoneStyle_Match)
			style = game::IsFutureToneMode() ? TechZoneStyle_FT : TechZoneStyle_M39;

		switch (style)
		{
		case TechZoneStyle_F:
			layer_name = "bonus_zone";
			scene = 14010081;
			break;
		case TechZoneStyle_F2nd:
			layer_name = "bonus_zone";
			scene = 14010082;
			break;
		case TechZoneStyle_X:
			layer_name = "bonus_zone";
			scene = 14010083;
			break;
		case TechZoneStyle_FT:
			layer_name = "bonus_zone";
			scene = 14010084;
			break;
		case TechZoneStyle_M39:
			layer_name = "bonus_zone";
			scene = 14010085;
			break;
		}
	}

	std::shared_ptr<AetElement>& tz = ::state.ui.GetLayer(LayerUI_BonusZone);
	std::shared_ptr<AetElement>& txt = ::state.ui.GetLayer(LayerUI_BonusZoneText);
	tz->SetScene(scene);
	txt->SetScene(scene);

	switch (this->state)
	{
	case TechZoneAction_Idle:
		if (data)
		{
			tz->SetLayer(layer_name, prio, 14, AetAction_InOnce);
			txt->SetVisible(true);
			txt->SetLayer("bonus_start_txt", prio, 14, AetAction_None);
			fail_in = false;
			state = TechZoneAction_InWait;
		}
		break;
	case TechZoneAction_InWait:
		if (tz->Ended())
		{
			tz->SetMarkers("st_lp", "ed_lp", true);
			state = TechZoneAction_Loop;
		}
		break;
	case TechZoneAction_Loop:
		if (data->failed)
		{
			if (!fail_in)
			{
				tz->SetMarkers("st_fail_in", "ed_fail_in", false);
				fail_in = true;
			}
			else if (fail_in && tz->Ended())
				tz->SetMarkers("st_fail_lp", "ed_fail_lp", true);
		}

		if (end)
		{
			if (data->failed)
			{
				tz->SetMarkers("st_fail_out", "ed_fail_out", false);
				txt->SetVisible(true);
				txt->SetLayer("bonus_end_txt", prio, 14, AetAction_None);
			}
			else if (data->IsSuccessful())
			{
				tz->SetMarkers("st_clear", "ed_clear", false);
				txt->SetVisible(true);
				txt->SetLayer("bonus_complete_txt", prio, 14, AetAction_None);
			}
			else
				tz->SetMarkers("st_out", "ed_out", false);

			state = TechZoneAction_EndWait;
		}
		break;
	case TechZoneAction_EndWait:
		if (tz->Ended())
		{
			data = nullptr;
			state = TechZoneAction_Idle;
		}
		break;
	}

	if (txt->Ended())
		txt->SetVisible(false);
}

static int32_t GetMaxNumber(int32_t max_digits)
{
	int32_t dec = 1;
	int32_t num = 0;
	for (int i = 0; i < max_digits; i++)
	{
		num += 9 * dec;
		dec *= 10;
	}

	return num;
}

struct NumberDrawArgs
{
	int32_t value = 0;
	int32_t max_digits = 3;
	int32_t priority = 11;
	int32_t color = 0xFFFFFFFF;
	uint32_t sprite_id = 0;
	diva_nc::vec2 glyph_size = {};
	diva_nc::vec2 size = {};
	diva_nc::vec3 pos = {};
};

static void DrawNumberWithSpriteFont(const NumberDrawArgs& args)
{
	int32_t value = args.value;
	if (int32_t max = GetMaxNumber(args.max_digits); value > max)
		value = max;

	for (int i = 0; i < args.max_digits; i++)
	{
		int32_t digit = value - value / 10 * 10;
		value /= 10;

		// NOTE: Discard leading zeroes
		if (digit == 0 && value == 0 && i > 0)
			break;

		SprArgs spr_args = { };
		spr_args.id = args.sprite_id;
		spr_args.resolution_mode_screen = 14;
		spr_args.resolution_mode_sprite = 14;
		spr_args.priority = args.priority;
		spr_args.attr = 0x100000; // SPR_ATTR_CTR_RT
		memcpy(spr_args.color, &args.color, 4);
		spr_args.trans = { args.pos.x - args.size.x * i, args.pos.y, 0.0f };
		spr_args.flags = 0x03;
		spr_args.texture_pos = { args.glyph_size.x * digit, 0.0f };
		spr_args.texture_size = { args.glyph_size.x, args.glyph_size.y };
		spr_args.sprite_size = { args.size.x, args.size.y };
		spr::DrawSprite(&spr_args);
	}
}

void TechZoneDispState::Disp() const
{
	if (!data)
		return;

	std::shared_ptr<AetElement>& tz = ::state.ui.GetLayer(LayerUI_BonusZone);

	NumberDrawArgs args = {};
	args.value = data->GetRemainingCount();
	args.color = data->failed ? 0xFF7F7F7F : 0xFFFFFFFF;
	args.priority = prio + 1;

	if (auto layout = tz->GetLayout("p_notes_num_rt"); layout.has_value())
	{
		args.sprite_id = 0x2E488F92;
		args.glyph_size = diva_nc::vec2(60.0f, 64.0f);
		args.size = diva_nc::vec2(45.0f, 48.0f);
		args.pos = layout.value().position;
	}
	else if (auto layout = tz->GetLayout("p_notes_num_ft_rt"); layout.has_value())
	{
		args.sprite_id = 0x4B60732B;
		args.glyph_size = diva_nc::vec2(33.0f, 42.0f);
		args.size = diva_nc::vec2(33.0f, layout.value().height);
		args.pos = layout.value().position;
	}
	else if (auto layout = tz->GetLayout("p_notes_num_mm_rt"); layout.has_value())
	{
		args.sprite_id = 0x3F4C00A5;
		args.glyph_size = diva_nc::vec2(37.0f, 41.0f);
		args.size = diva_nc::vec2(37.0f, layout.value().height);
		args.pos = layout.value().position;
	}

	DrawNumberWithSpriteFont(args);
}
