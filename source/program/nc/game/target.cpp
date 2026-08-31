#include "lib.hpp"
#include "note_link.hpp"
#include "target.hpp"
#include "sound_effects.hpp"
#include "logger.hpp"
#include "hit_state.hpp"

static void PatchCommonKiseki(PvGameTarget* target);
static void UpdateLongNoteKiseki(PVGameArcade* data, TargetStateEx* ex, float dt);
static void DrawLongNoteKiseki(TargetStateEx* ex);
static void DrawBalloonEffect(TargetStateEx* ex);

extern bool g_input_updated_this_frame;

static constexpr const char* ButtonBaseNames[12] = {
	"sankaku_bal",  "maru_bal",  "batsu_bal", "shikaku_bal",
	"up_w",         "right_w",   "down_w",     "left_w",
	"sankaku_long", "maru_long", "batsu_long", "shikaku_long"
};

static constexpr const char* PlatformPrefixes[3] = { "", "nsw_", "xbox_" };

static constexpr bool ArrowSpriteLookup[13][4] = {
	{ false, false, false, false }, // T S X O (PS)
	{ true,  true,  false, false },
	{ false, true,  true,  false },
	{ true,  false, false, true  },
	{ true,  true,  true,  true  },
	{ false, false, false, false }, // X Y B A (NSW)
	{ true,  true,  false, false },
	{ false, true,  true,  false },
	{ true,  false, false, true  },
	{ false, false, false, false }, // Y X A B (XBOX)
	{ true,  true,  false, false },
	{ false, true,  true,  false },
	{ true,  false, false, true  }
};

static constexpr uint32_t ButtonSpriteIDs[3][5] = {
	{ 0x000000B6, 0x000000B7, 0x000000B4, 0x000000B5, 0x0FD08752 }, // T S X O (PS) (SPR_GAM_CMN)
	{ 0x89D6BC5D, 0x3688928E, 0xD3B2C230, 0x4EC68397, 0x0FD08752 }, // X Y B A (NSW)
	{ 0x367EBA07, 0x89D6BC5D, 0xA87BA497, 0x3E02AA52, 0x0FD08752 }  // Y X A B (XBOX)
};

static constexpr uint32_t ArrowSpriteIDs[3][5] = {
	{ 0x147EBFD4, 0xC27B4CA3, 0x06014095, 0x22CE0158, 0x00000000 },
	{ 0x93F28B09, 0xEC80CF72, 0xE5393DAA, 0x06F66090, 0x00000000 },
	{ 0x369BEB4D, 0x06F36B82, 0xCC908494, 0x06F66090, 0x00000000 }
};

static constexpr uint8_t KisekiColorLookup[3][4] = {
	// 0 - Green   1 - Pink   2 - Blue   3 - Red   4 - Yellow   5 - Orange
	{ 0, 1, 2, 3 },
	{ 2, 0, 5, 3 },
	{ 4, 2, 0, 3 }
};

static constexpr uint32_t KisekiSprites[6] = {
	1140037210, 2946182585, 1305045037, 2660966235, 3052385779, 224411231
};

static int32_t GetMelodyIconStyle()
{
	uintptr_t base_addr = exl::util::GetMainModuleInfo().m_Total.m_Start;

	// 1. Read pointer to structure at 0xcdf5c0
	uintptr_t game_info_ptr = *reinterpret_cast<const uintptr_t*>(base_addr + 0xcdf5c0);

	// Return 0 if structure is not yet initialized
	if (game_info_ptr == 0) {
		return 0;
	}

	// 2. Read melody icon style from structure offset +0x20
	return *reinterpret_cast<const int32_t*>(game_info_ptr + 0x20);
}

static int32_t GetMelodyIconPlatform()
{
	switch (GetMelodyIconStyle())
	{
	case 5:
	case 6:
	case 7:
	case 8:
		return 1;
	case 9:
	case 10:
	case 11:
	case 12:
		return 2;
	}

	return 0;
}

static int32_t GetTargetButtonKind(int32_t target_type)
{
	switch (target_type)
	{
	case TargetType_Triangle:
	case TargetType_UpW:
	case TargetType_TriangleRush:
	case TargetType_TriangleLong:
		return 0;
	case TargetType_Square:
	case TargetType_LeftW:
	case TargetType_SquareRush:
	case TargetType_SquareLong:
		return 1;
	case TargetType_Cross:
	case TargetType_DownW:
	case TargetType_CrossRush:
	case TargetType_CrossLong:
		return 2;
	case TargetType_Circle:
	case TargetType_RightW:
	case TargetType_CircleRush:
	case TargetType_CircleLong:
		return 3;
	case TargetType_Star:
	case TargetType_StarW:
	case TargetType_StarRush:
	case TargetType_StarLong:
	case TargetType_LinkStar:
	case TargetType_LinkStarEnd:
		return 4;
	}

	return 0;
}

static uint32_t GetKisekiSpriteID(int32_t target_type)
{
	return KisekiSprites[KisekiColorLookup[GetMelodyIconPlatform()][GetTargetButtonKind(target_type)]];
}

static bool CheckNoteUsesArrowSprite(int32_t index, int32_t type)
{
	int32_t base_type = -1;

	switch (type)
	{
	case TargetType_TriangleLong:
	case TargetType_CircleLong:
	case TargetType_CrossLong:
	case TargetType_SquareLong:
		base_type = type - TargetType_TriangleLong;
		break;
	case TargetType_TriangleRush:
	case TargetType_CircleRush:
	case TargetType_CrossRush:
	case TargetType_SquareRush:
		base_type = type - TargetType_TriangleRush;
		break;
	}

	if (base_type > -1)
		return ArrowSpriteLookup[index][base_type == 1 ? 3 : base_type == 3 ? 1 : base_type];
	return false;
}

static std::string GetNoteLayerName(int32_t type, int32_t kind)
{
	std::string kind_name = kind == 1 ? "button_" : "target_";
	std::string base_name = "";
	std::string prefix = "";
	std::string suffix = "";

	switch (type)
	{
	case TargetType_Star:
		base_name = "touch";
		break;
	case TargetType_StarW:
		base_name = "touch_w";
		break;
	case TargetType_ChanceStar:
		base_name = "touch_ch_miss";
		break;
	case TargetType_LinkStar:
	case TargetType_LinkStarEnd:
		base_name = "link";
		break;
	case TargetType_StarRush:
		base_name = "touch_bal";
		break;
	}

	if (type >= TargetType_TriangleRush && type <= TargetType_SquareLong)
	{
		base_name = ButtonBaseNames[type - TargetType_TriangleRush];
		prefix = PlatformPrefixes[GetMelodyIconPlatform()];

		if (CheckNoteUsesArrowSprite(GetMelodyIconStyle(), type))
			suffix = "_01";
	}

	return prefix + kind_name + base_name + suffix;
}

HOOK_DEFINE_TRAMPOLINE(CreateTargetAetLayersHook) {
	static uint64_t Callback(PvGameTarget* target) {
		TargetStateEx* ex = GetTargetStateEx(target);

		if (!ex) return Orig(target);

		ex->org = target;
		ex->target_pos = target->target_pos;
		ex->target_type = target->target_type;

		if (ex->IsRushNote()) {
			if (ex->length <= 0.0f) ex->length = 2.0f;
			ex->bal_max_hit_count = ex->length * 4.5f;
		}

		if (target->target_type < TargetType_Custom || target->target_type >= TargetType_Max) {
			return Orig(target);
		}

		int32_t type = target->target_type;
		target->target_type = TargetType_Triangle;

		uint64_t ret = Orig(target);

		target->target_type = type;

		aet::Stop(&target->target_aet);
		aet::Stop(&target->button_aet);
		aet::Stop(&target->target_eff_aet);
		aet::Stop(&target->dword78);

		std::string target_layer = GetNoteLayerName(target->target_type, 0);
		std::string button_layer = GetNoteLayerName(target->target_type, 1);
		diva_nc::vec2 target_pos = GetScaledPosition(target->target_pos);
		diva_nc::vec2 button_pos = GetScaledPosition(target->button_pos);

		target->target_aet = aet::PlayLayer(AetSceneID, 8, 0x20000, target_layer.c_str(), &target_pos, 0, nullptr, nullptr, 0.0f, -1.0f, 0, nullptr);
		target->button_aet = aet::PlayLayer(AetSceneID, 9, 0x20000, button_layer.c_str(), &button_pos, 0, nullptr, nullptr, 0.0f, -1.0f, 0, nullptr);

		if (ex->link_step) {
			if (!ex->link_start) aet::Stop(&target->button_aet);
			ex->target_aet = target->target_aet;
			ex->button_aet = target->button_aet;
			target->target_aet = 0;
			target->button_aet = 0;
		}

		if (ex->IsRushNote()) {
			ex->bal_time = aet::GetMarkerTime(AetSceneID, button_layer.c_str(), "bal");
			ex->bal_start_time = aet::GetMarkerTime(AetSceneID, button_layer.c_str(), "bal_start");
			ex->bal_end_time = aet::GetMarkerTime(AetSceneID, button_layer.c_str(), "bal_end");
			ex->bal_effect_aet = aet::PlayLayer(AetSceneID, 12, 0x10000, "target_balloon_eff", nullptr, 0, nullptr, nullptr, -1.0f, -1.0f, 0, nullptr);
			aet::SetPlay(ex->bal_effect_aet, false);
		}

		aet::SetOpacity(target->target_aet, target->target_opacity);
		aet::SetOpacity(target->button_aet, target->button_opacity);

		return ret;
	}
};

HOOK_DEFINE_TRAMPOLINE(UpdateTargetsHook) {
	static void Callback(PVGameArcade* data, float dt) {
		for (PvGameTarget* target = data->target; target != nullptr; target = target->next)
		{
			TargetStateEx* ex = GetTargetStateEx(target);
			if (ex->link_start || ex->IsLongNoteStart() || ex->IsRushNote())
				state.PushTarget(ex);

			if (ex->flying_time_max <= 0.0f)
			{
				ex->flying_time_max = target->flying_time;
				ex->flying_time_remaining = target->flying_time_remaining;
			}

			if (target->target_type == TargetType_ChanceStar)
			{
				if (state.chance_time.GetFillRate() == 15 && !ex->success)
				{
					diva_nc::vec2 target_pos = GetScaledPosition(target->target_pos);

					aet::Stop(&target->target_aet);
					target->target_aet = aet::PlayLayer(
						AetSceneID,
						8,
						0x20000,
						"target_touch_ch",
						&target_pos,
						0,
						nullptr,
						nullptr,
						-1.0f,
						-1.0f,
						0,
						nullptr
					);

					aet::Stop(&target->button_aet);
					target->button_aet = aet::PlayLayer(
						AetSceneID,
						9,
						0x20000,
						"button_touch_ch",
						&target->button_pos,
						0,
						nullptr,
						nullptr,
						-1.0f,
						-1.0f,
						0,
						nullptr
					);

					ex->success = true;
				}
			}
		}

		if (ShouldUpdateTargets())
		{
			for (TargetStateEx* tgt : state.target_references)
			{
				if (tgt->IsLinkNoteStart())
				{
					UpdateLinkStar(data, tgt, dt);
					UpdateLinkStarKiseki(data, tgt, dt);
				}
				else if (tgt->IsLongNoteStart() && tgt->holding)
				{
					bool is_in_zone = false;
					if (tgt->next && tgt->next->org)
					{
						float time = tgt->next->org->flying_time_remaining;
						is_in_zone = (time >= data->sad_late_window && time <= data->sad_early_window);
					}

					float time_to_tail = (tgt->next && tgt->next->org) ? tgt->next->org->flying_time_remaining : 999.0f;
					bool in_tail_judgment = (time_to_tail <= (data->sad_early_window + 0.05f));

					if (!nc::CheckLongNoteHolding(tgt) && !is_in_zone && !in_tail_judgment)
					{
						tgt->holding = false;
						tgt->StopAet();
						se_mgr.EndLongSE(true);
						GetPVGameData()->ui.RemoveBonusText();
						if (tgt->next)
							tgt->next->force_hit_state = HitState_Worst;
						continue;
					}

					UpdateLongNoteKiseki(data, tgt, dt);
				}
				if (tgt->IsLongNoteStart() && tgt->holding)
				{
					tgt->length_remaining = fmaxf(tgt->length_remaining - dt, 0.0f);
					tgt->sustain_bonus_time += dt;
				}
				else if (tgt->IsRushNote() && tgt->holding)
				{
					if (tgt->length_remaining <= 0.0f)
					{
						tgt->holding = false;
						if (tgt->bal_hit_count >= tgt->bal_max_hit_count)
						{
							diva_nc::vec2 pos = GetScaledPosition(tgt->target_pos);
							state.PlayRushHitEffect(pos, 1.0f + tgt->bal_scale, true);
							se_mgr.EndRushBackSE(true);
						}
						else
							se_mgr.EndRushBackSE(false);

						tgt->StopAet();
					}
					else if (tgt->length_remaining <= tgt->flying_time_max)
					{
						if (tgt->flying_time_max > 0.0f)
						{
							float progress = (tgt->flying_time_max - tgt->length_remaining) / tgt->flying_time_max;
							float frame = tgt->bal_start_time + (tgt->bal_end_time - tgt->bal_start_time) * progress;
							aet::SetFrame(tgt->button_aet, frame);
						}
					}

					float scale = 1.0f + tgt->bal_scale;
					diva_nc::vec3 scale_vec = { scale, scale, 1.0f };
					aet::SetScale(tgt->button_aet, &scale_vec);

					tgt->length_remaining = fmaxf(tgt->length_remaining - dt, 0.0f);
				}

				for (TargetStateEx* chain = tgt; chain != nullptr; chain = chain->next)
				{
					if (chain->flying_time_max >= 0.0f)
						chain->flying_time_remaining -= dt;
				}
			}
		}

		if (state.chance_time.successful) {
			if (data->current_time >= state.chance_time.chance_star_hit_time && data->current_time < state.chance_time.chance_star_hit_time + 0.1) {
				bool hit = macro_state.GetDoubleStarHit();
				if (hit) {
					GetPVGameData()->is_success_branch = false;
				}
			}
		}

		for (PvGameTarget* target = data->target; target != nullptr; target = target->next) {
			if (target->target_type >= TargetType_Custom) {
				target->target_type = TargetType_Triangle;
			}
		}

		Orig(data, dt);

		for (PvGameTarget* target = data->target; target != nullptr; target = target->next) {
			TargetStateEx* ex = GetTargetStateEx(target);
			if (ex && ex->target_type >= TargetType_Custom) {
				target->target_type = ex->target_type;
			}
		}
	}
};

HOOK_DEFINE_TRAMPOLINE(UpdateKisekiHook) {
	static void Callback(PVGameArcade* data, PvGameTarget* target, float dt) {
		TargetStateEx* ex = GetTargetStateEx(target);
		if (ex->IsLongNote())
		{
			ex->kiseki_pos = target->button_pos;
			ex->kiseki_dir = target->delta_pos_sq;
			UpdateLongNoteKiseki(data, ex, dt);
		}
		else
		{
			int32_t original_type = target->target_type;
			if (target->target_type >= TargetType_Custom) {
				target->target_type = TargetType_Triangle;
			}

			Orig(data, target, dt);

			target->target_type = original_type;
			PatchCommonKiseki(target);
		}
	}
};

HOOK_DEFINE_TRAMPOLINE(DrawKisekiHook) {
	static void Callback(PvGameTarget* target) {
		TargetStateEx* ex = GetTargetStateEx(target);
		if (ex->IsLongNote()) {
			DrawLongNoteKiseki(ex);
			return;
		}
		else if (ex->IsLinkNote() && !ex->IsLinkNoteStart()) {
			return;
		}

		int32_t original_type = target->target_type;
		if (target->target_type >= TargetType_Custom) {
			target->target_type = TargetType_Triangle;
		}

		Orig(target);

		target->target_type = original_type;
	}
};

HOOK_DEFINE_TRAMPOLINE(DrawArcadeGameHook) {
	static void Callback(PVGameArcade* data) {
		for (TargetStateEx* tgt : state.target_references)
		{
			if (tgt->IsLongNoteStart() && tgt->holding)
				DrawLongNoteKiseki(tgt);
			else if (tgt->IsLinkNoteStart())
			{
				for (TargetStateEx* ex = tgt; ex != nullptr; ex = ex->next)
				{
					if (ex->vertex_count_max != 0)
					{
						if (!tgt->IsChainSucessful())
							DrawTriangles(ex->kiseki.data(), ex->vertex_count_max, 13, 7, 1457444052, 0);
						else
							DrawTriangles(ex->kiseki.data(), ex->vertex_count_max, 13, 7, 1964935274, 0);
					}
				}
			}
			else if (tgt->IsRushNote() && tgt->holding)
				DrawBalloonEffect(tgt);
		}

		state.tz_disp.Disp();

		for (PvGameTarget* target = data->target; target != nullptr; target = target->next) {
			if (target->target_type >= TargetType_Custom) {
				target->target_type = TargetType_Triangle;
			}
		}

		Orig(data);

		for (PvGameTarget* target = data->target; target != nullptr; target = target->next) {
			TargetStateEx* ex = GetTargetStateEx(target);
			if (ex && ex->target_type >= TargetType_Custom) {
				target->target_type = ex->target_type;
			}
		}
	}
};

static void PatchCommonKiseki(PvGameTarget* target)
{
	float r, g, b;
	TargetStateEx* ex = GetTargetStateEx(target);

	switch (target->target_type)
	{
	case TargetType_TriangleRush:
	case TargetType_UpW:
		r = 0.799f;
		g = 1.0f;
		b = 0.5401;
		break;
	case TargetType_CircleRush:
	case TargetType_RightW:
		r = 0.9372f;
		g = 0.2705f;
		b = 0.2901f;
		break;
	case TargetType_CrossRush:
	case TargetType_DownW:
		r = 0.7098f;
		g = 1.0f;
		b = 1.0f;
		break;
	case TargetType_SquareRush:
	case TargetType_LeftW:
		r = 1.0f;
		g = 0.8117f;
		b = 1.0f;
		break;
	case TargetType_Star:
	case TargetType_StarW:
	case TargetType_LinkStar:
	case TargetType_LinkStarEnd:
	case TargetType_StarRush:
		r = 0.9f;
		g = 0.9f;
		b = 0.1f;
		break;
	case TargetType_ChanceStar:
		if (ex != nullptr && ex->success)
		{
			r = 0.9f;
			g = 0.9f;
			b = 0.1f;
		}
		else
		{
			r = 0.65f;
			g = 0.65f;
			b = 0.65f;
		}
		break;
	default:
		r = 0.0f;
		g = 0.0f;
		b = 0.0f;
		break;
	}

	if (state.chance_time.CheckTargetInRange(target->target_index))
	{
		for (int i = 0; i < 40; i++)
		{
			int32_t alpha = 0xFF000000;
			if (i >= 20)
			{
				if (i % 2 == 0)
					alpha = static_cast<int32_t>((1.0f - static_cast<float>(i - 20 + 2) / 20.0f) * 255) << 24;
				else
					alpha = target->kiseki[i - 1].color & 0xFF000000;
			}

			target->kiseki[i].uv.y = i % 2 != 0 ? 64.0f : 128.0f;
			target->kiseki[i].color = alpha | 0x00FFFFFF;
		}
	}
	else if (target->target_type >= TargetType_Custom)
	{
		uint32_t color = (uint8_t)(r * 255) |
			((uint8_t)(g * 255) << 8) |
			((uint8_t)(b * 255) << 16);

		for (int i = 0; i < 40; i++)
			target->kiseki[i].color = (target->kiseki[i].color & 0xFF000000) | (color & 0x00FFFFFF);
	}
}

static diva_nc::vec2 GetLongKisekiOffset(TargetStateEx* ex)
{
	return ex->target_type == TargetType_TriangleLong ? diva_nc::vec2(0.0f, 2.0f) : diva_nc::vec2(0.0f, 0.0f);
}

static void CreateLongNoteKisekiBuffer(TargetStateEx* ex)
{
	ex->vertex_count_max = ex->length * KisekiRate * 2 + 2;
	if (ex->vertex_count_max % 2 != 0)
		ex->vertex_count_max += 3;

	ex->kiseki.resize(ex->vertex_count_max);

	float uv_width  = 256.0f;
	float uv_height = 128.0f;
	float uv_pos_y  = uv_height / 2.0f;
	uint32_t color = IsSuddenEquipped(GetPVGameData()) ? 0x00FFFFFF : 0xFFFFFFFF;

	for (size_t i = 0; i < ex->vertex_count_max; i += 2)
	{
		diva_nc::vec2 offset = GetLongKisekiOffset(ex);

		ex->kiseki[i].pos       = diva_nc::vec3(GetScaledPosition(ex->kiseki_pos + offset), 0.0f);
		ex->kiseki[i].uv        = diva_nc::vec2(uv_width, uv_pos_y);
		ex->kiseki[i].color     = color;
		ex->kiseki[i + 1].pos   = diva_nc::vec3(GetScaledPosition(ex->kiseki_pos + offset), 0.0f);
		ex->kiseki[i + 1].uv    = diva_nc::vec2(uv_width, uv_height);
		ex->kiseki[i + 1].color = color;
	}
}

static void UpdateLongNoteKiseki(PVGameArcade* data, TargetStateEx* ex, float dt)
{
	if (!ex->IsLongNoteStart() || ex->length <= 0.0f || !data || !ex)
		return;

	if (ex->kiseki.size() == 0)
		CreateLongNoteKisekiBuffer(ex);

	if (ex->vertex_count_max < 4)
		return;

	ex->kiseki_time += dt;
	size_t push_count = 0;

	while (ex->kiseki_time >= KisekiInterval)
	{
		for (size_t i = ex->vertex_count_max - 3; i > 0; i--)
			ex->kiseki[i + 2] = ex->kiseki[i];
		ex->kiseki[2] = ex->kiseki[0];

		push_count++;
		ex->kiseki_time -= KisekiInterval;
	}

	if (ex->vertex_count_max > 0) {
		size_t max_safe_pushes = (ex->vertex_count_max / 2);
		if (max_safe_pushes > 0 && push_count >= max_safe_pushes) {
			push_count = max_safe_pushes - 1;
		}
	} else {
		push_count = 0;
	}

	if (ex->fix_long_kiseki)
	{
		if (ex->hit_time < 0.0f)
		{
			push_count += static_cast<size_t>(fabsf(ex->hit_time) / KisekiInterval);
			ex->fix_long_kiseki = false;
		}
	}

	const float uv_width = 256.0f;
	const float px_width = 7.2f;
	const diva_nc::vec2 offset = GetLongKisekiOffset(ex);

	if (ex->org != nullptr && ex->flying_time_remaining > 0.0f)
		ex->alpha = ex->org->button_opacity;

	for (size_t i = 0; i < push_count; i++)
	{
		diva_nc::vec2 size = ex->kiseki_dir * px_width;
		diva_nc::vec2 left = GetScaledPosition(ex->kiseki_pos + offset + -size);
		diva_nc::vec2 right = GetScaledPosition(ex->kiseki_pos + offset + size);
		uint32_t color = 0x00FFFFFF | (static_cast<int32_t>(ex->alpha * 255.0f) << 24);

		ex->kiseki[i * 2].pos       = diva_nc::vec3(right, 0.0f);
		ex->kiseki[i * 2].color     = color;
		ex->kiseki[i * 2 + 1].pos   = diva_nc::vec3(left, 0.0f);
		ex->kiseki[i * 2 + 1].color = color;
	}

	for (size_t i = 0; i < ex->vertex_count_max; i++)
		ex->kiseki[i].uv.x += dt * uv_width;
}

static void DrawLongNoteKiseki(TargetStateEx* ex)
{
	if (!ex->IsLongNote() || ex->vertex_count_max < 1)
		return;

	uint32_t sprite_id = GetKisekiSpriteID(ex->target_type);
	DrawTriangles(ex->kiseki.data(), ex->vertex_count_max, 13, 7, sprite_id, 0);
}

static uint32_t GetBalloonSpriteId(const TargetStateEx* ex)
{
	if (CheckNoteUsesArrowSprite(GetMelodyIconStyle(), ex->target_type))
		return ArrowSpriteIDs[GetMelodyIconPlatform()][GetTargetButtonKind(ex->target_type)];
	return ButtonSpriteIDs[GetMelodyIconPlatform()][GetTargetButtonKind(ex->target_type)];
}

static void DrawBalloonEffectNote(uint32_t id, const diva_nc::vec2& pos, const diva_nc::vec2& scale, float opacity)
{
	SprArgs args;
	args.id = id;
	args.trans.x = pos.x;
	args.trans.y = pos.y;
	args.trans.z = 0.0f;
	args.scale.x = scale.x;
	args.scale.y = scale.y;
	args.scale.z = 1.0f;
	args.resolution_mode_sprite = 13;
	args.resolution_mode_sprite = 13;
	args.index = 0;
	args.layer = 0;
	args.priority = 12;
	args.attr = 0x400000;
	args.color[0] = 0xFF;
	args.color[1] = 0xFF;
	args.color[2] = 0xFF;
	args.color[3] = fminf(fmaxf(opacity * 255.0f, 0.0f), 255.0f);
	spr::DrawSprite(&args);
}

static void DrawBalloonEffect(TargetStateEx* ex)
{
	if (ex->bal_effect_aet == 0)
		return;

	AetComposition comp;
	aet::GetComposition(&comp, ex->bal_effect_aet);

	float scale = 1.0f + ex->bal_scale;

	for (const auto& [name, layout] : comp)
	{
		diva_nc::vec2 pos = GetScaledPosition(ex->target_pos);
		pos.x += layout.position.x * scale;
		pos.y += layout.position.y * scale;

		diva_nc::vec2 spr_scale = { layout.matrix.row0.x, layout.matrix.row1.y };

		DrawBalloonEffectNote(
			GetBalloonSpriteId(ex),
			pos,
			spr_scale,
			layout.opacity
		);
	}
}

void InstallTargetHooks()
{
	CreateTargetAetLayersHook::InstallAtOffset(0x1b3150);
	UpdateTargetsHook::InstallAtOffset(0x1b06f0);
	UpdateKisekiHook::InstallAtOffset(0x1b2be0);
	DrawKisekiHook::InstallAtOffset(0x1b4a70);
	DrawArcadeGameHook::InstallAtOffset(0x1b4f70);
}
