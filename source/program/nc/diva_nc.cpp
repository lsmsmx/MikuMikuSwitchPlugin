#include <string.h>
#include "diva_nc.hpp"
#include "logger.hpp"

using namespace diva_nc;

FUNCTION_PTR(AetArgs*, __fastcall, CreateAetArgsOrg, 0x1db4c0, AetArgs*, uint32_t, const char*, int32_t, int32_t);
FUNCTION_PTR(void, __fastcall, StopAetOrg, 0x21fab0, int32_t* id);
FUNCTION_PTR(FontInfo*, __fastcall, DefaultFontInfo, 0x219340, FontInfo* font);
FUNCTION_PTR(SprArgs*, __fastcall, DefaultSprArgs, 0x615240, SprArgs* args);
FUNCTION_PTR(AetArgs*, __fastcall, DefaultAetArgs, 0x78320, AetArgs* args);

static FUNCTION_PTR(int32_t, __fastcall, PlayLayerAetArgs, 0x21f930, AetArgs* args, int32_t id);
static FUNCTION_PTR(void, __fastcall, CreateSpriteNumberFont, 0x219690, FontInfo* font, int32_t id, int32_t w, int32_t h);

FontInfo::FontInfo()
{
	memset(this, 0, sizeof(FontInfo));
	DefaultFontInfo(this);
}

FontInfo FontInfo::CreateSpriteFont(int32_t sprite_id, int32_t width, int32_t height)
{
	FontInfo font = { };
	CreateSpriteNumberFont(&font, sprite_id, width, height);
	return font;
}

void FontInfo::SetSize(float width, float height)
{
	size = vec2(width, height);
	scale = size / font_size;
}

AetArgs::AetArgs(uint32_t scene, const char* layer, int32_t prio, int32_t marker_mode)
{
	CreateAetArgsOrg(this, scene, layer, prio, marker_mode);
}

SprArgs::SprArgs()
{
	memset(this, 0, sizeof(SprArgs));
	DefaultSprArgs(this);
}

TextArgs::TextArgs()
{
	memset(this, 0, sizeof(TextArgs));
	spr::DefaultTextArgs(this);
}

void aet::CreateAetArgs(AetArgs* args, uint32_t scene_id, const char* layer_name, int32_t prio)
{
	CreateAetArgsOrg(args, scene_id, layer_name, prio, 0);
}

void aet::CreateAetArgs(AetArgs* args, uint32_t scene_id, const char* layer_name, int32_t flags, int32_t layer, int32_t prio, const char* start_marker, const char* end_marker)
{
	CreateAetArgs(args, scene_id, layer_name, prio);
	args->flags = flags;
	args->layer = layer;
	args->start_marker = start_marker;
	args->end_marker = end_marker;
}

void aet::Stop(int32_t id)
{
	if (id != 0)
		StopAetOrg(&id);
}

void aet::Stop(int32_t* id)
{
	if (id != nullptr)
	{
		if (*id != 0)
			StopAetOrg(id);
		*id = 0;
	}
}

bool aet::StopOnEnded(int32_t* id)
{
	if (id != nullptr && *id != 0)
	{
		bool ended = aet::GetEnded(*id);
		if (ended)
			aet::Stop(id);
		return ended;
	}

	return true;
}

int32_t aet::PlayLayer(uint32_t scene_id, int32_t prio, int32_t flags, const char* layer, const vec2* pos, int32_t index, const char* start_marker, const char* end_marker, float start_time, float end_time, int32_t a11, void* frmctl)
{
	AetArgs args;

	// Use DefaultAetArgs to avoid applying internal AET anchor offsets
	DefaultAetArgs(&args);

	args.scene_id = scene_id;
	args.layer_name = layer;
	args.prio = prio;

	args.flags |= flags;
	args.res_mode = 13;   // 720p base resolution
	args.index = index;

	if (pos != nullptr) {
		args.pos.x = pos->x;
		args.pos.y = pos->y;
		args.pos.z = 0.0f;
	}

	if (start_marker != nullptr) args.start_marker = start_marker;
	if (end_marker != nullptr) args.end_marker = end_marker;
	args.start_time = start_time;
	args.end_time = end_time;
	args.frame_rate_control = frmctl;

	return PlayLayerAetArgs(&args, 0);
}

int32_t aet::PlayLayer(uint32_t scene, int32_t prio, int32_t flags, const char* layer, const vec2* pos, const char* start_marker, const char* end_marker)
{
	return PlayLayer(scene, prio, flags, layer, pos, 0, start_marker, end_marker, -1.0f, -1.0f, 0, nullptr);
}

int32_t aet::PlayLayer(uint32_t scene_id, int32_t prio, const char* layer, int32_t action)
{
	AetArgs args;
	CreateAetArgsOrg(&args, scene_id, layer, prio, action);
	return PlayLayerAetArgs(&args, 0);
}

int32_t game::GetGlobalPvID()
{
	int32_t pv_id = game::GetGlobalPVInfo()->pv_id;
	int32_t prev = pv_id;
	if (pv_id == -2)
	{
		int32_t param_index = game::GetPVLoadParam()->data[0].int8;
		if (param_index < 4)
			pv_id = game::GetPVLoadParam()->data[param_index].pv_id;
	}

	if (pv_id < 0)
		return 1;
	return pv_id;
}

// NOTE: InputState implementation (adapted for Switch)
static FUNCTION_PTR(int32_t, __fastcall, IS_GetPosition, 0x1fd170, diva_nc::InputState* t, int32_t index);
static FUNCTION_PTR(bool, __fastcall, IS_IsButtonTapped, 0x1fc910, diva_nc::InputState* t, int32_t key, int32_t mod);
static FUNCTION_PTR(bool, __fastcall, IS_IsButtonRepeat, 0x1fcbb0, diva_nc::InputState* t, int32_t key, int32_t mod);
static FUNCTION_PTR(bool, __fastcall, IS_IsButtonDown, 0x1fce70, diva_nc::InputState* t, int32_t key, int32_t mod);

int32_t InputState::GetPosition(int32_t index) { return IS_GetPosition(this, index); }
int32_t InputState::GetDevice() { return 0; } // Always 0 on Switch (gamepad)
bool InputState::IsButtonTapped(int32_t key) { return IS_IsButtonTapped(this, key, 0); }
bool InputState::IsButtonTappedAbs(int32_t key) { return IS_IsButtonTapped(this, key, 0); }
bool InputState::IsButtonDown(int32_t key) { return IS_IsButtonDown(this, key, 0); }
bool InputState::IsButtonTappedOrRepeat(int32_t key) { return IS_IsButtonRepeat(this, key, 0); }

// NOTE: PVGameArcade implementation
static FUNCTION_PTR(void, __fastcall, PVGAC_EraseTarget, 0x1ad3b0, PVGameArcade* data, PvGameTarget* target);
inline FUNCTION_PTR(void, __fastcall, FUN_001b9b70, 0x1b9b70, uint32_t *param_1);

inline void PVGAC_FinishTargetAet(PvGameTarget* target, PVGameArcade* data) {
    if (!target || !data) return;

    if (data->play) {
        if (target->target_aet != 0 || target->button_aet != 0) {
            int32_t* remaining_count_ptr = (int32_t*)((uintptr_t)data + 0x18);
            if (*remaining_count_ptr != 0) {
                *remaining_count_ptr -= 1;
            }

            FUN_001b9b70((uint32_t*)((uintptr_t)target + 0x70));
            FUN_001b9b70((uint32_t*)((uintptr_t)target + 0x74));
            FUN_001b9b70((uint32_t*)((uintptr_t)target + 0x78));
            FUN_001b9b70((uint32_t*)((uintptr_t)target + 0x7c));
        }
    }
}

static FUNCTION_PTR(void, __fastcall, PVGAC_PlayHitEffect, 0x1b24a0, PVGameArcade* data, int32_t effect, const vec2* pos);

void PVGameArcade::EraseTarget(PvGameTarget* target) { PVGAC_EraseTarget(this, target); }
void PVGameArcade::RemoveTargetAet(PvGameTarget* target) { PVGAC_FinishTargetAet(target, this); }
void PVGameArcade::PlayHitEffect(int32_t index, const vec2& pos) { PVGAC_PlayHitEffect(this, index, &pos); }

// NOTE: PVGameUI implementation
static FUNCTION_PTR(void, __fastcall, PGUI_SetBonusText, 0x1bc2a0, PVGameUI*, int32_t, float, float);

void PVGameUI::SetBonusText(int32_t value, const vec2& pos)
{
	if (value > 0)
		PGUI_SetBonusText(this, value, pos.x, pos.y);
}

void PVGameUI::RemoveBonusText() { aet::Stop(&aet_list[5]); }

// NOTE: Misc
static FUNCTION_PTR(void, __fastcall, DSC_ScalePosition, 0x1a4500, const vec2* in, vec2* out);

vec2 GetScaledPosition(const vec2& v)
{
	vec2 scaled = { };
	DSC_ScalePosition(&v, &scaled);
	return scaled;
}
