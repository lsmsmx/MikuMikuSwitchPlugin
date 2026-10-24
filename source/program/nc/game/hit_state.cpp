#include <tuple>
#include <chrono>
#include <cmath>
#include "../nc_state.hpp"
#include "target.hpp"
#include "hit_state.hpp"
#include "logger.hpp"

const float NormalWindow[5]  = { 0.03f, 0.07f, 0.1f,  0.13f };
const float StarWindow[5]    = { 0.03f, 0.12f, 0.13f, 0.13f };

extern int32_t s_current_hit_frame;
extern int32_t s_last_hit_frame;
extern int32_t s_face_tap_frame[4];
extern int32_t s_arrow_tap_frame[4];
extern bool s_face_consumed_by_single[4];
extern bool s_arrow_consumed_by_single[4];

namespace nc
{
	static bool CheckTiming(float time, int32_t hit_state, bool star)
	{
		float early = star ? StarWindow[hit_state] : NormalWindow[hit_state];
		float late = -early;
		return time >= late && time <= early;
	}

	static int32_t JudgeTiming(float time, bool star, bool wrong)
	{
		if (CheckTiming(time, HitState_Cool, star))
			return wrong ? HitState_WrongCool : HitState_Cool;
		else if (CheckTiming(time, HitState_Fine, star))
			return wrong ? HitState_WrongFine : HitState_Fine;
		else if (CheckTiming(time, HitState_Safe, star))
			return wrong ? HitState_WrongSafe : HitState_Safe;
		else if (CheckTiming(time, HitState_Sad, star))
			return wrong ? HitState_WrongSad : HitState_Sad;

		return HitState_None;
	}

	static int32_t GetTargetBaseType(int32_t target_type)
	{
		switch (target_type)
		{
		case TargetType_Triangle:
		case TargetType_TriangleLong:
		case TargetType_TriangleRush:
		case TargetType_UpW:
			return TargetType_Triangle;
		case TargetType_Circle:
		case TargetType_CircleLong:
		case TargetType_CircleRush:
		case TargetType_RightW:
			return TargetType_Circle;
		case TargetType_Cross:
		case TargetType_CrossLong:
		case TargetType_CrossRush:
		case TargetType_DownW:
			return TargetType_Cross;
		case TargetType_Square:
		case TargetType_SquareLong:
		case TargetType_SquareRush:
		case TargetType_LeftW:
			return TargetType_Square;
		case TargetType_Star:
		case TargetType_StarLong:
		case TargetType_StarRush:
		case TargetType_StarW:
		case TargetType_ChanceStar:
		case TargetType_LinkStar:
		case TargetType_LinkStarEnd:
			return TargetType_Star;
		}

		return TargetType_Max;
	}

	uint64_t GetButtonMaskWithArrows(int32_t target_type)
	{
		switch (GetTargetBaseType(target_type))
		{
		case TargetType_Triangle:
			return (GetButtonMask(Button_Triangle) | GetButtonMask(Button_Up));
		case TargetType_Circle:
			return (GetButtonMask(Button_Circle) | GetButtonMask(Button_Right));
		case TargetType_Cross:
			return (GetButtonMask(Button_Cross) | GetButtonMask(Button_Down));
		case TargetType_Square:
			return (GetButtonMask(Button_Square) | GetButtonMask(Button_Left));
		}

		return 0;
	}

	static std::tuple<uint64_t, uint64_t> GetButtonBitmasksAccountedForMultis(PvGameTarget* target)
	{
		uint64_t button_mask = GetButtonMaskWithArrows(target->target_type);
		uint64_t wrong_mask = GetButtonMask(Button_Max) & ~button_mask;

		if (target->multi_count < 0)
			return std::make_tuple(button_mask, wrong_mask);

		for (PvGameTarget* next = target->next; next && next->multi_count == target->multi_count; next = next->next)
			wrong_mask &= ~GetButtonMaskWithArrows(next->target_type);

		for (PvGameTarget* prev = target->prev; prev && prev->multi_count == target->multi_count; prev = prev->prev)
			wrong_mask &= ~GetButtonMaskWithArrows(prev->target_type);

		return std::make_tuple(button_mask, wrong_mask);
	}

	static int32_t GetHitStateInternal(PVGameArcade* data, PvGameTarget* target, TargetStateEx* ex, ButtonState** hold_button, bool* double_tapped, bool* no_success)
	{
		if (ex->force_hit_state != HitState_None)
			return ex->force_hit_state;

		if (target->flying_time_remaining < -NormalWindow[HitState_Sad])
			return HitState_Worst;

		// Note Queue Protection:
		// Do not process this target if any previous sequential note is still unhit.
		for (PvGameTarget* p = target->prev; p != nullptr; p = p->prev)
		{
			if (p->multi_count < 0 || p->multi_count != target->multi_count)
			{
				if (p->hit_state == HitState_None)
					return HitState_None;
			}
		}

		bool hit = false;
		bool wrong = false;
		auto [button_mask, wrong_mask] = GetButtonBitmasksAccountedForMultis(target);

		// Double Notes (W-notes)
		if (target->target_type >= TargetType_UpW && target->target_type <= TargetType_LeftW)
		{
			int32_t offset = target->target_type - TargetType_UpW;
			ButtonState& face = macro_state.buttons[Button_Triangle + offset];
			ButtonState& arrow = macro_state.buttons[Button_Up + offset];

			if (fabs(target->flying_time_remaining) <= NormalWindow[HitState_Sad])
			{
				bool double_hit = false;

				// Strictly read this note's face button and arrow button states
				bool face_down = face.IsDown();
				bool arrow_down = arrow.IsDown();

				// A fresh tap occurred strictly after the previous note was hit
				bool face_fresh = (face.IsTapped() || face.IsTappedInNearFrames()) && (s_last_hit_frame < 0 || s_face_tap_frame[offset] > s_last_hit_frame);
				bool arrow_fresh = (arrow.IsTapped() || arrow.IsTappedInNearFrames()) && (s_last_hit_frame < 0 || s_arrow_tap_frame[offset] > s_last_hit_frame);

				// Case 1: Fresh simultaneous tap on both buttons (AutoPlay, synced fingers, rapid double spam)
				if (face.IsTapped() && arrow.IsTapped())
				{
					double_hit = true;
				}
				// Case 2: Near frames desync between two fingers (both taps are fresh, neither stolen from single)
				else if (face_fresh && arrow_fresh && (face.IsTapped() || arrow.IsTapped() || face.IsTappedInNearFrames() || arrow.IsTappedInNearFrames()))
				{
					double_hit = true;
				}
				// Case 3: Arrow held (allowed from single), Face freshly tapped (must NOT be consumed by single)
				else if (face_fresh && arrow_down && !s_face_consumed_by_single[offset])
				{
					double_hit = true;
				}
				// Case 4: Face held (allowed from single), Arrow freshly tapped (must NOT be consumed by single)
				else if (arrow_fresh && face_down && !s_arrow_consumed_by_single[offset])
				{
					double_hit = true;
				}

				if (double_hit)
				{
					hit = true;
					wrong = false;
					*double_tapped = true;

					s_face_consumed_by_single[offset] = false;
					s_arrow_consumed_by_single[offset] = false;
				}
				else
				{
					// Register wrong press only when note is close to its hit window (<= 40ms)
					bool wrong_pressed = (macro_state.GetTappedBitfield() & wrong_mask) != 0;
					if (wrong_pressed && target->flying_time_remaining <= 0.04f)
					{
						hit = true;
						wrong = true;
					}
				}
			}
		}
		// Long Notes
		else if (target->target_type >= TargetType_TriangleLong && target->target_type <= TargetType_SquareLong)
		{
			int32_t offset = target->target_type - TargetType_TriangleLong;
			ButtonState& face  = macro_state.buttons[Button_Triangle + offset];
			ButtonState& arrow = macro_state.buttons[Button_Up + offset];

			if (!ex->long_end)
			{
				if (face.IsTapped() || arrow.IsTapped())
				{
					hit = true;
					wrong = false;
					*hold_button = face.IsTapped() ? &face : &arrow;
				}
				else if ((macro_state.GetTappedBitfield() & wrong_mask) != 0)
				{
					hit = true;
					wrong = true;
				}
			}
			else
			{
				bool face_down = face.IsDown() || macro_state.IsRawDown(Button_Triangle + offset);
				bool arrow_down = arrow.IsDown() || macro_state.IsRawDown(Button_Up + offset);
				bool is_holding_now = face_down || arrow_down;

				if (fabs(target->flying_time_remaining) <= NormalWindow[HitState_Sad])
				{
					if (!is_holding_now || face.IsReleased() || arrow.IsReleased())
					{
						hit = true;
						wrong = false;
					}
				}
			}
		}
		// Star Notes
		else if (ex->IsStarLikeNote())
		{
			hit = macro_state.GetStarHit();
			if (target->target_type == TargetType_ChanceStar)
				*no_success = macro_state.GetStarHitCancel();
		}
		// Double Star Notes
		else if (target->target_type == TargetType_StarW)
		{
			hit = macro_state.GetDoubleStarHit();
		}
		// Rush Notes
		else if (ex->IsRushNote())
		{
			if ((macro_state.GetTappedBitfield() & button_mask) != 0)
			{
				hit = true;
				wrong = false;
			}
			else if ((macro_state.GetTappedBitfield() & wrong_mask) != 0)
			{
				hit = true;
				wrong = true;
			}
		}

		if (hit)
			return nc::JudgeTiming(target->flying_time_remaining, ex->IsStarLikeNote() || ex->target_type == TargetType_StarW, wrong);

		return HitState_None;
	}
}

int32_t nc::JudgeNoteHit(PVGameArcade* game, PvGameTarget** group, TargetStateEx** extras, int32_t group_count, bool* success)
{
	if (group_count < 1)
		return HitState_None;

	for (int i = 0; i < group_count; i++)
	{
		PvGameTarget* target = group[i];
		TargetStateEx* ex = extras[i];

		ButtonState* hold_button = nullptr;
		bool double_tapped = false;
		bool no_success = false;
		int32_t hit_state = nc::GetHitStateInternal(game, target, ex, &hold_button, &double_tapped, &no_success);

		if (hit_state != HitState_None)
		{
			if (nc::IsHitCorrect(hit_state))
			{
				if (ex->IsLongNoteStart())
				{
					ex->hold_button = hold_button;
					ex->holding = true;
					ex->fix_long_kiseki = true;
					ex->kiseki_pos = ex->target_pos;
					ex->next->force_hit_state = HitState_None;
				}
				else if (ex->IsRushNote())
					ex->holding = true;
				else if (target->target_type == TargetType_ChanceStar)
				{
					if (nc::IsHitGreat(hit_state) && state.chance_time.GetFillRate() == 15)
					{
						*success = true;
						state.chance_time.successful = true;
						state.chance_time.chance_star_hit_time = game->current_time;
					}
				}
			}

			target->hit_state = hit_state;
			target->player_hit_time = target->flying_time_remaining;
			ex->hit_state = hit_state;
			ex->hit_time = target->flying_time_remaining;
			ex->double_tapped = double_tapped;
		}
	}
	return extras[0]->hit_state;
}

bool nc::CheckLongNoteHolding(TargetStateEx* ex)
{
	if (ex->target_type >= TargetType_TriangleLong && ex->target_type <= TargetType_SquareLong)
	{
		int32_t offset = ex->target_type - TargetType_TriangleLong;
		if (macro_state.IsRawDown(Button_Triangle + offset) || macro_state.IsRawDown(Button_Up + offset))
		{
			ex->holding = true;
			return true;
		}

		ButtonState& face  = macro_state.buttons[Button_Triangle + offset];
		ButtonState& arrow = macro_state.buttons[Button_Up + offset];
		if (face.IsDown() || arrow.IsDown())
		{
			ex->holding = true;
			return true;
		}
	}

	if (ex->hold_button != nullptr && ex->hold_button->IsDown())
	{
		ex->holding = true;
		return true;
	}

	ex->holding = false;
	return false;
}

bool nc::CheckRushNotePops(TargetStateEx* ex)
{
	ButtonState* button1 = nullptr;
	ButtonState* button2 = nullptr;

	switch (ex->target_type)
	{
	case TargetType_TriangleRush:
	case TargetType_CircleRush:
	case TargetType_CrossRush:
	case TargetType_SquareRush:
		button1 = &macro_state.buttons[ex->target_type - TargetType_TriangleRush + Button_Triangle];
		button2 = &macro_state.buttons[ex->target_type - TargetType_TriangleRush + Button_Up];
		break;
	case TargetType_StarRush:
		return macro_state.GetStarHit();
	}

	bool cond = false;
	if (button1 != nullptr)
		cond |= button1->IsTapped();

	if (button2 != nullptr)
		cond |= button2->IsTapped();

	return cond;
}
