#include "note_link.hpp"

static bool CalculateDirection(TargetStateEx* ex)
{
	if (!ex->next)
		return false;

	diva_nc::vec2 delta = ex->next->target_pos - ex->kiseki_pos;
	ex->kiseki_dir_norot = delta / delta.length();
	ex->kiseki_dir = ex->kiseki_dir_norot.rotated(1.570796);
	return true;
}

void UpdateLinkStar(PVGameArcade* data, TargetStateEx* chain, float dt)
{
	TargetStateEx* current = nullptr;
	TargetStateEx* next = nullptr;

	for (TargetStateEx* ex = chain; ex != nullptr; ex = ex->next)
	{
		ex->current_step = false;
		ex->kiseki_pos = ex->target_pos;
		CalculateDirection(ex);

		if (ex->flying_time_max > 0.0f)
		{
			if (ex->link_start && ex->flying_time_remaining > 0.0f && ex->org != nullptr)
			{
				diva_nc::vec3 pos = diva_nc::vec3(GetScaledPosition(ex->org->button_pos), 0.0f);
				aet::SetPosition(ex->button_aet, &pos);
			}

			if (ex->flying_time_remaining <= data->cool_late_window && ex->target_aet != 0)
			{
				float scale = 1.0f - (ex->flying_time_remaining / data->sad_late_window);
				if (scale >= 0.0f)
				{
					diva_nc::vec3 v = { scale, scale, 1.0f };
					aet::SetScale(ex->target_aet, &v);
				}
				else
					aet::Stop(&ex->target_aet);
			}

			if (ex->target_aet != 0)
			{
				float frame = (ex->flying_time_max - ex->flying_time_remaining) / ex->flying_time_max * 360.0f;
				frame = fminf(fmaxf(frame, 0.0f), 360.0f);
				aet::SetFrame(ex->target_aet, frame);
				aet::SetPlay(ex->target_aet, false);
			}

			if (ex->flying_time_remaining <= 0.0f)
			{
				current = ex;
				next = ex->next;
			}

			const float appear_fade_length = 0.3f;
			const float glow_fade_length = 0.2f;
			const float glow_length = 0.3f;

			switch (ex->step_state)
			{
			case LinkStepState_None:
				if (ex->next != nullptr && ex->next->org != nullptr)
				{
					ex->kiseki_time = appear_fade_length;
					ex->alpha = 0.0f;
					ex->step_state = LinkStepState_Normal;
				}
				break;

			case LinkStepState_Normal:
				ex->alpha = 1.0f - ex->kiseki_time / appear_fade_length;
				ex->kiseki_time -= dt;

				if (ex->alpha >= 1.0f)
				{
					ex->alpha = 1.0f;
					ex->kiseki_time = 0.0f;
					ex->step_state = LinkStepState_Wait;
				}
				break;

			case LinkStepState_GlowStart:
				ex->alpha = 1.0f - (ex->kiseki_time / glow_fade_length);
				ex->kiseki_time -= dt;

				if (ex->kiseki_time <= 0.0f)
				{
					ex->kiseki_time = glow_length;
					ex->alpha = 1.0f;
					ex->step_state = LinkStepState_Glow;
				}
				break;

			case LinkStepState_Glow:
				if (ex->kiseki_time <= 0.0f)
				{
					ex->kiseki_time = glow_fade_length;
					ex->alpha = 1.0f;
					ex->step_state = LinkStepState_GlowEnd;
				}

				ex->kiseki_time -= dt;
				break;

			case LinkStepState_GlowEnd:
				ex->alpha = ex->kiseki_time / glow_fade_length;
				ex->kiseki_time -= dt;

				if (ex->kiseki_time <= 0.0f)
				{
					ex->kiseki_time = 0.0f;
					ex->alpha = 0.0f;
					ex->step_state = LinkStepState_Idle;
				}
				break;

			case LinkStepState_Wait:
				if (chain->IsChainSucessful())
				{
					ex->kiseki_time = glow_fade_length;
					ex->alpha = 0.0f;
					ex->step_state = LinkStepState_GlowStart;
				}
				else if (ex->next != nullptr)
				{
					if (ex->next->flying_time_max > 0.0f && ex->next->flying_time_remaining <= 0.0f)
					{
						ex->kiseki_time = 0.0f;
						ex->alpha = 0.0f;
					}
				}
				break;
			}
		}
	}

	if (current != nullptr)
	{
		if (!current->link_end && next != nullptr)
		{
			if (current->delta_time_max <= 0.0f)
			{
				current->delta_time_max = next->flying_time_remaining;
				current->delta_time = current->delta_time_max;
			}

			current->current_step = true;

			float percentage = (current->delta_time_max - current->delta_time) / current->delta_time_max;
			diva_nc::vec2 delta = next->target_pos - current->target_pos;
			diva_nc::vec2 pos = current->target_pos + delta * percentage;

			current->kiseki_pos = pos;
			CalculateDirection(current);

			diva_nc::vec3 pos_3 = diva_nc::vec3(GetScaledPosition(pos), 0.0f);
			aet::SetPosition(chain->button_aet, &pos_3);
			aet::SetFrame(chain->button_aet, 0.0f);

			current->delta_time -= dt;
		}
		else if (current->link_end)
		{
			if (!current->link_ending)
			{
				if (current->flying_time_remaining <= data->cool_late_window)
				{
					aet::SetFrame(chain->button_aet, 360.0f);
					current->link_ending = true;
				}
			}
			else
				aet::StopOnEnded(&chain->button_aet);
		}
	}
}

void UpdateLinkStarKiseki(PVGameArcade* data, TargetStateEx* chain, float dt)
{
	const float width = 12.0f;
	const float edge  = width * 0.375f;
	const float tex_padding   = 12.0f;
	const diva_nc::vec2 tex_size = { 144.0f, 32.0f };

	for (TargetStateEx* ex = chain; ex != nullptr; ex = ex->next)
	{
		if (ex->link_end || ex->next->org == nullptr)
			continue;

		ex->vertex_count_max = 20;
		ex->kiseki.resize(ex->vertex_count_max);

		uint32_t color = 0x00FFFFFF;
		color |= static_cast<uint32_t>(ex->alpha * 255.0f) << 24;

		diva_nc::vec2 start_pos = ex->kiseki_pos - ex->kiseki_dir_norot * edge;
		diva_nc::vec2 end_pos = ex->next->target_pos + ex->kiseki_dir_norot * edge;
		diva_nc::vec2 delta = (end_pos - start_pos) / 18.0f;

		diva_nc::vec2 uv_offset = { 0.0f, 0.0f };
		float uv_delta_x = (tex_size.x - tex_padding * 2.0f) / 18.0f;

		for (int i = 0; i < 20; i += 2)
		{
			float step = i;

			if (i == 2 || i == 18)
				uv_offset.x += 12.0f;

			diva_nc::vec2 right = GetScaledPosition({
				start_pos.x + delta.x * step + ex->kiseki_dir.x * width,
				start_pos.y + delta.y * step + ex->kiseki_dir.y * width
			});

			diva_nc::vec2 left = GetScaledPosition({
				start_pos.x + delta.x * step - ex->kiseki_dir.x * width,
				start_pos.y + delta.y * step - ex->kiseki_dir.y * width
			});

			ex->kiseki[i].pos.x = right.x;
			ex->kiseki[i].pos.y = right.y;
			ex->kiseki[i].pos.z = 0.0f;
			ex->kiseki[i].uv.x = uv_offset.x + uv_delta_x * step;
			ex->kiseki[i].uv.y = uv_offset.y + tex_size.y;
			ex->kiseki[i].color = color;

			ex->kiseki[i + 1].pos.x = left.x;
			ex->kiseki[i + 1].pos.y = left.y;
			ex->kiseki[i + 1].pos.z = 0.0f;
			ex->kiseki[i + 1].uv.x = uv_offset.x + uv_delta_x * step;
			ex->kiseki[i + 1].uv.y = uv_offset.y;
			ex->kiseki[i + 1].color = color;
		}
	}
}
