#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <memory>
#include <list>
#include <stdint.h>
#include <cmath>
#include "lib.hpp"
#include "ModLoader.hpp"
#include "Types.hpp"


#define FUNCTION_PTR_H(returnType, function, ...) extern returnType (*function)(__VA_ARGS__)
#define FUNCTION_PTR(returnType, convention, function, offset, ...) \
	returnType (*function)(__VA_ARGS__) = (returnType(*)(__VA_ARGS__))(exl::util::GetMainModuleInfo().m_Total.m_Start + offset)


using string = prj::string;

template <typename T>
using vector = prj::vector<T>;

template <typename K, typename V>
using map = prj::map<K, V>;

template <typename T>
using list = prj::list<T>;


template <typename T>
struct _stringRangeBase {
	T *data;
	T *end;
	T *c_str () { return data; }

	_stringRangeBase () : data(0), end(0) {}
	_stringRangeBase (size_t length) {
		if (length <= 0) length = 8;

		data = (T*)GameOperatorNew(length * sizeof(T));
		end  = data + length;
		memset(data, 0, length * sizeof(T));
	}
};
using stringRange  = _stringRangeBase<char>;
using wstringRange = _stringRangeBase<wchar_t>;


enum HitState : int32_t
{
	HitState_Cool       = 0,
	HitState_Fine       = 1,
	HitState_Safe       = 2,
	HitState_Sad        = 3,
	HitState_WrongCool  = 4,
	HitState_WrongFine  = 5,
	HitState_WrongSafe  = 6,
	HitState_WrongSad   = 7,
	HitState_Worst      = 8,
	HitState_CoolDouble = 9,
	HitState_FineDouble = 10,
	HitState_SafeDouble = 11,
	HitState_SadDouble  = 12,
	HitState_CoolTriple = 13,
	HitState_FineTriple = 14,
	HitState_SafeTriple = 15,
	HitState_SadTriple  = 16,
	HitState_CoolQuad   = 17,
	HitState_FineQuad   = 18,
	HitState_SafeQuad   = 19,
	HitState_SadQuad    = 20,
	HitState_None       = 21
};

enum BasicHitState : int32_t
{
	BasicHitState_Cool  = 0,
	BasicHitState_Fine  = 1,
	BasicHitState_Safe  = 2,
	BasicHitState_Sad   = 3,
	BasicHitState_Worst = 4
};

enum InputDevice : int32_t
{
	InputDevice_Xbox      = 0,
	InputDevice_DualShock = 1,
	InputDevice_Switch    = 2,
	InputDevice_Steam     = 3,
	InputDevice_Keyboard  = 4,
	InputDevice_Unknown   = 5
};

enum GameFont : int32_t
{
	GameFont_Cmn10x16          = 0,
	GameFont_Num12x16          = 1,
	GameFont_Num14x18          = 2,
	GameFont_Num14x20          = 3,
	GameFont_Num20x26          = 4,
	GameFont_Num20x22          = 5,
	GameFont_Num22x22          = 6,
	GameFont_Num22x24          = 7,
	GameFont_Num24x30          = 8,
	GameFont_Num26x24          = 9,
	GameFont_Num28x40          = 10,
	GameFont_Num28x40_GOLD     = 11,
	GameFont_Num34x32          = 12,
	GameFont_Num40x52          = 13,
	GameFont_Num56x46          = 14,
	GameFont_Asc12x18          = 15,
	GameFont_Diva36x38x24      = 16,
	GameFont_Bold36x38x24      = 17,
	GameFont_Diva36x38         = 18,
	GameFont_Bold36x38         = 19,
	GameFont_CN36x38           = 20,
	GameFont_BoldCN36x38       = 21,
	GameFont_CN36x38_2         = 22,
	GameFont_BoldCN36x38_2     = 23,
	GameFont_KR36x38           = 24,
	GameFont_BoldKR36x38       = 25,
	GameFont_Latin36x38x24     = 26,
	GameFont_LatinBold36x38x24 = 27,
	GameFont_Latin36x38        = 28,
	GameFont_LatinBold36x38    = 29
};

// NOTE: Math types
//
namespace diva_nc
{
	struct vec2
	{
		float x;
		float y;

		vec2() { x = 0.0f; y = 0.0f; }
		vec2(float x, float y) { this->x = x; this->y = y; }

		inline float length() const { return sqrtf(x * x + y * y); }
		inline vec2 abs() const { return vec2(fabsf(x), fabsf(y)); }

		inline vec2 operator+(const vec2& right) const
		{
			return { this->x + right.x, this->y + right.y };
		}

		inline vec2& operator+=(const vec2& right)
		{
			this->x += right.x;
			this->y += right.y;
			return *this;
		}

		inline vec2 operator-() const
		{
			return { -this->x, -this->y };
		}

		inline vec2 operator-(const vec2& right) const
		{
			return { this->x - right.x, this->y - right.y };
		}

		inline vec2 operator*(float scalar) const
		{
			return { this->x * scalar, this->y * scalar };
		}

		inline vec2 operator*(const vec2& right) const
		{
			return { this->x * right.x, this->y * right.y };
		}

		inline vec2 operator/(float scalar) const
		{
			return { this->x / scalar, this->y / scalar };
		}

		inline vec2 operator/(const vec2& right) const
		{
			return { this->x / right.x, this->y / right.y };
		}

		inline bool operator>(const vec2& right) const
		{
			return x > right.x && y > right.y;
		}

		inline bool operator>=(const vec2& right) const
		{
			return x >= right.x && y >= right.y;
		}

		inline bool operator<(const vec2& right) const
		{
			return x < right.x && y < right.y;
		}

		inline bool operator<=(const vec2& right) const
		{
			return x <= right.x && y <= right.y;
		}

		inline vec2 rotated(float angle)
		{
			float c = cosf(angle);
			float s = sinf(angle);
			return {
				this->x * c - this->y * s,
				this->x * s - this->y * c
			};
		}
	};

	struct vec3
	{
		float x;
		float y;
		float z;

		vec3()
		{
			x = 0.0f;
			y = 0.0f;
			z = 0.0f;
		}

		vec3(const vec2& xy, float z)
		{
			x = xy.x;
			y = xy.y;
			this->z = z;
		}

		vec3(float x, float y, float z)
		{
			this->x = x;
			this->y = y;
			this->z = z;
		}

		inline vec2 xy() const { return vec2(x, y); }

		inline vec3 operator+(const vec3& right) const
		{
			return { this->x + right.x, this->y + right.y, this->z + right.z };
		}

		inline vec3 operator-(const vec3& right) const
		{
			return { this->x - right.x, this->y - right.y, this->z - right.z };
		}
	};

	struct vec4
	{
		float x;
		float y;
		float z;
		float w;
	};

	struct mat4
	{
		vec4 row0;
		vec4 row1;
		vec4 row2;
		vec4 row3;

		inline diva_nc::vec3 GetScale() const
		{
			// NOTE: Only works for 2D transformations
			float v = sqrtf(powf(row0.x, 2.0f) + powf(row1.x, 2.0f));
			float t = acosf(row0.x / v);
			return { t, t, 1.0f };
		}
	};

	struct Rect
	{
		float x;
		float y;
		float width;
		float height;
	};
}

struct SpriteVertex
{
	diva_nc::vec3 pos;
	diva_nc::vec2 uv;
	uint32_t color;
};

struct SprArgs
{
	uint32_t kind;                     // 0x00
	uint32_t id;                       // 0x04
	uint8_t color[4];                  // 0x08
	int32_t attr;                      // 0x0C
	int32_t blend;                     // 0x10
	int32_t index;                     // 0x14
	int32_t layer;                     // 0x18
	int32_t priority;                  // 0x1C
	int32_t resolution_mode_screen;    // 0x20
	int32_t resolution_mode_sprite;    // 0x24
	diva_nc::vec3 center;              // 0x28
	diva_nc::vec3 trans;               // 0x34
	diva_nc::vec3 scale;               // 0x40
	diva_nc::vec3 rot;                 // 0x4C
	diva_nc::vec2 skew_angle;          // 0x58
	diva_nc::mat4 mat;                 // 0x60
	void* texture;                     // 0xA0
	int32_t shader;                    // 0xA8
	int32_t field_AC;                  // 0xAC
	diva_nc::mat4 transform;           // 0xB0
	bool field_F0;                     // 0xF0
	uint8_t pad_vertex[7];
	SpriteVertex* vertex_array;        // 0xF8
	size_t num_vertex;                 // 0x100
	int32_t field_108;                 // 0x108
	void* field_110;                   // 0x110

	uint32_t flags;                    // 0x118

	diva_nc::vec2 sprite_size;         // 0x11C
	uint32_t field_124;

	diva_nc::vec2 texture_pos;         // 0x128
	diva_nc::vec2 texture_size;        // 0x130

	SprArgs* next;                     // 0x138
	void* tex;                         // 0x140
	SprArgs();
};

struct FontInfo
{
	int32_t font_id = -1;
	void* raw_font = nullptr;
	int32_t spr_id = -1;
	int32_t unk14 = 0;
	float unk18 = 0.0f;
	float unk1C = 0.0f;
	diva_nc::vec2 font_size;
	diva_nc::vec2 font_box_size;
	diva_nc::vec2 size;
	diva_nc::vec2 scale;
	diva_nc::vec2 spacing;

	FontInfo();
	~FontInfo() = default;

	static FontInfo CreateSpriteFont(int32_t sprite_id, int32_t width, int32_t height);

	void SetSize(float width, float height);
};

enum TextFlags : int32_t
{
	TextFlags_AlignLeft = 0x1,
	TextFlags_AlignRight = 0x2,
	TextFlags_AlignHCenter = 0x4,
	TextFlags_AutoAlignHCenter = 0x8,
	TextFlags_AlignVCenter = 0x10,
	TextFlags_AutoAlignVCenter = 0x20,
	TextFlags_Clip = 0x200,
	TextFlags_Outline = 0x10000,

	TextFlags_AutoAlign = TextFlags_AutoAlignHCenter | TextFlags_AutoAlignVCenter
};

#pragma pack(push, 1)
struct PrintWork
{
	int32_t color;
	int32_t fill_color;
	bool clip;
	int8_t gap9[3];
	diva_nc::vec4 clip_data;
	int32_t prio;
	int32_t layer;
	int32_t res_mode;
	uint32_t unk28;
	diva_nc::vec2 text_current;
	diva_nc::vec2 line_origin;
	size_t line_length;
	FontInfo* font;
	uint16_t empty_char;
	int8_t gap4E[2];

	inline void SetColor(uint32_t color, uint32_t fill)
	{
		color = color;
		fill_color = fill;
	}

	inline void SetOpacity(float opacity)
	{
		int32_t a = (opacity < 0.0f ? 0.0f : opacity > 1.0f ? 1.0f : opacity) * 255;
		color = (color & 0xFFFFFF) | (a << 24);
		fill_color = (fill_color & 0xFFFFFF) | (a << 24);
	}
};

// TODO: Clean-up this struct. I think the struct might be actually smaller than this.
struct TextArgs
{
	float max_width; // NOTE: Text will get squished to fit once it goes past this limit
	PrintWork print_work;
	uint32_t unk60;
	uint32_t unk64;
	uint32_t unk68;
	uint32_t unk6C;
	uint32_t unk70;
	uint32_t unk74;
	uint32_t unk78;
	uint32_t unk7C;

	TextArgs();
};
#pragma pack(pop)

enum AetAction : int32_t
{
	AetAction_None = 0,
	AetAction_InOnce = 1,
	AetAction_InLoop = 2,
	AetAction_Loop = 3,
	AetAction_OutOnce = 4,
	AetAction_SpecialOnce = 5,
	AetAction_SpecialLoop = 6
};

struct AetArgs
{
	uint32_t scene_id = 0;
	const char* layer_name = nullptr;
	libcxx_string start_marker;
	libcxx_string end_marker;
	libcxx_string loop_marker;
	float start_time = -1.0f;
	float end_time = -1.0f;
	int32_t flags = 0x0;
	int32_t index = 0;
	int32_t layer = 0;
	int32_t prio = 0;
	int32_t res_mode = 13;
	diva_nc::vec3 pos = { 0.0f, 0.0f, 0.0f };
	diva_nc::vec3 rot = { 0.0f, 0.0f, 0.0f };
	diva_nc::vec3 scale = { 1.0f, 1.0f, 1.0f };
	diva_nc::vec3 anchor = { 0.0f, 0.0f, 0.0f };
	float frame_speed = 1.0f;
	diva_nc::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };

	libcxx_map<libcxx_string, int32_t> layer_sprites;
	libcxx_string sound_path;
	libcxx_map<libcxx_string, libcxx_string> sound_replace;
	int32_t sound_queue_index = 0;
	libcxx_map<uint32_t, uint32_t> sprite_replace;
	libcxx_map<uint32_t, void*> sprite_texture;
	libcxx_map<uint32_t, uint32_t> sprite_discard;

	void* frame_rate_control = nullptr;
	bool sound_voice = false;
	int32_t dword154 = 0;
	int32_t dword158 = 0;
	int32_t dword15C = 0;
	int32_t id = 0;
	int32_t dword164 = 0;
	diva_nc::vec3 pos_2 = { 0.0f, 0.0f, 0.0f };

	void ClearContainers()
	{
		layer_sprites.clear();
		sound_replace.clear();
		sprite_replace.clear();
		sprite_texture.clear();
		sprite_discard.clear();
	}

	AetArgs() = default;
	AetArgs(uint32_t scene, const char* layer, int32_t prio, int32_t marker_mode);
	~AetArgs() = default;
};

struct AetLayout
{
	diva_nc::mat4 matrix;
	diva_nc::vec3 position;
	diva_nc::vec3 anchor;
	float width;
	float height;
	float opacity;
	uint32_t color;
	int32_t resolution_mode;
	uint32_t unk6C;
	int32_t unk70;
	uint8_t blendMode;
	uint8_t transferFlags;
	uint8_t trackMatte;
	int32_t unk78;
	int32_t unk7C;
};

using AetComposition = libcxx_map<libcxx_string, AetLayout>;
using AetHandle = int32_t;

// NOTE: Koren named this struct "struc_14" so I have no idea what it's real name is. But PvGameplayInfo sounds fitting.
struct PvGameplayInfo
{
	int32_t type;
	int32_t difficulty;
};

struct PvDscTarget
{
	int32_t type;
	diva_nc::vec2 target_pos;
	diva_nc::vec2 start_pos;
	float amplitude;
	int32_t frequency;
	bool slide_chain_start;
	bool slide_chain_end;
	bool slide_chain_left;
	bool slide_chain_right;
	bool slide_chain;
};

struct PvDscTargetGroup
{
	int32_t target_count;
	PvDscTarget targets[4];
	int32_t field_94;
	int64_t spawn_time;
	int64_t hit_time;
	bool slide_chain;
};

struct PVGameData;
struct PVGameArcade;

// NOTE: Some information were taken from ReDIVA. Thank you, Koren!
//		 https://github.com/korenkonder/ReDIVA/blob/master/src/ReDIVA/pv_game/pv_game_pv_data.hpp
//
struct PVGamePvData
{
	bool field_0;
	int32_t dsc_state;
	bool play;
	int script_buffer[45000];
	int script_pos;
	uint8_t gap2BF30[24];
	float float2BF48;
	uint8_t gap2BF4C[36];
	int int2BF70;
	int int2BF74;
	PVGameData* pv_game;
	uint8_t gap2BF80[24];
	float float2BF98;
	float float2BF9C;
	int32_t dword2BFA0;
	int32_t dword2BFA4;
	uint64_t script_time;
	uint8_t gap2BFA8[16];
	uint8_t byte2BFC0;
	uint8_t byte2BFC1;
	uint8_t gap2BFC8[8];
	int32_t dword2BFCC;
	int32_t dword2BFD0;
	uint8_t byte2BFD4;
	uint8_t byte2BFD5;
	uint8_t byte2BFD6;
	uint8_t gap2BFD7[961];
	uint8_t byte2C398;
	int32_t dword2C39C;
	uint8_t gap2C3A0[8];
	uint8_t byte2C3A8;
	int32_t dword2C3AC;
	uint8_t gap2C3B0[272];
	uint8_t byte2C4C0;
	int32_t dword2C4C4;
	float float2C4C8;
	float float2C4CC;
	float float2C4D0;
	float float2C4D4;
	float float2C4D8;
	float float2C4DC;
	INSERT_PADDING(0x40);
	bool has_dsc_signature;
	bool is_dsc_ac200_format;
	uint8_t pad2C4E2[2];
	int targets_remaining;
	prj::vector<PvDscTargetGroup> targets;
	size_t target_index;
	float float2C508;
	char char2C50C;
	uint8_t gap2C50D[63];
	uint8_t gap2C54C[8];
	int32_t dsc_branch_mode;
	int32_t first_challenge_target_index;
	int32_t last_challenge_target_index;
	uint8_t gap2C558[704];
	uint8_t byte2C820;
	int32_t dword2C824;
	int32_t dword2C828;
	float float2C82C;
};

struct PvGameTarget
{
	PvGameTarget* prev;
	PvGameTarget* next;
	int32_t dword10;
	int32_t target_type;
	float flying_time_remaining;
	float player_hit_time;
	float flying_time;
	float amplitude;
	float freq;
	float cur_freq;
	uint8_t gap30[4];
	int32_t dword34;
	uint8_t gap38[4];
	float dword3C;
	uint8_t gap40;
	bool slide_chain_start;
	bool slide_chain_end;
	bool slide_left;
	bool slide_right;
	bool slide_chain;
	uint8_t gap46[2];
	int64_t hit_time;
	uint8_t gap50[32];
	int32_t target_aet;
	int32_t button_aet;
	int32_t dword78;
	int32_t target_eff_aet;
	diva_nc::vec2 target_pos;
	diva_nc::vec2 button_pos;
	diva_nc::vec2 delta_pos;
	diva_nc::vec2 delta_pos_sq;
	diva_nc::vec2 vecA0;
	SpriteVertex kiseki[40];
	bool note_active;
	uint8_t gap469[3];
	float out_start_time;
	uint8_t gap470[4];
	int32_t hit_state;
	int32_t multi_count;
	int32_t dword47C;
	float float480;
	uint8_t gap484[12];
	uint8_t byte490;
	bool b491;
	bool b492;
	bool b493;
	float button_opacity;
	float target_opacity;
	float sudden_appear_frame;
	float scaling_end_time;
	uint8_t gap4A4[4];
	int32_t sprite_index;
	uint8_t gap4AC[4];
	int64_t appear_time;
	int16_t word4B8;
	uint8_t gap4BA[2];
	float kiseki_width;
	int32_t target_index;
	int32_t dword4C4;
};

struct PVGameArcade
{
	bool play;
	uint8_t gap1[7];
	void* ptr08;
	uint8_t gap[16];
	PvGameTarget* target;
	PvGameTarget target_buffer[64];
	uint8_t gap13228[64];
	float target_time;
	float cool_early_window;
	float cool_late_window;
	float fine_early_window;
	float fine_late_window;
	float safe_early_window;
	float safe_late_window;
	float sad_early_window;
	float sad_late_window;
	uint8_t byte1328C;
	uint8_t byte1328D;
	bool bool1328E;
	uint8_t byte1328F;
	float current_time;
	int32_t int13294;
	int32_t hit_effects[64];
	int32_t hit_effect_index;
	uint8_t gap1339C[80]; // switch specific
	int32_t dword133FC;
	int32_t dword13400[3];
	int32_t slide_hit_effects[64];
	int32_t slide_hit_effect_index;
	uint8_t gap13510[8];
	float fl13518;
	float fl1351C;
	float fl13520;
	float fl13524;
	float fl13528;
	float fl1352C;
	float fl13530;
	bool mute_slide_chime;
	uint8_t gap13535[7];
	bool bool1353C[4];
	uint8_t gap13540[14];
	int8_t char1354E[4];
	float fl13554[2];

	void EraseTarget(PvGameTarget* target);
	void RemoveTargetAet(PvGameTarget* target);
	void PlayHitEffect(int32_t index, const diva_nc::vec2& pos);
};

struct PVGameUI
{
	int32_t int00;                      // 0x00
	int32_t aet_list[95];               // 0x04 .. 0x180
	bool visibility[95];                // 0x180 .. 0x1DF
	uint8_t pad1DF;                     // 0x1DF
	float frame_bottom_offset[2];       // 0x1E0
	float frame_bottom_dt[2];           // 0x1E8
	float frame_top_offset[2];          // 0x1F0
	float frame_top_dt[2];              // 0x1F8
	float top_offset[2];                // 0x200
	float bottom_offset[2];             // 0x208
	bool frame_visibility[2];           // 0x210
	uint8_t pad212[2];                  // 0x212
	int32_t frame_action[2];            // 0x214
	float target_top_offset[2];         // 0x21C
	float target_bottom_offset[2];      // 0x224
	uint8_t gap228[4];                  // 0x228
	bool is_animating;                  // 0x230

	// FIX
	uint8_t gap231[0x394 - 0x231];

	int32_t life;                       // 0x394
	bool draw_combo_counter;            // 0x398
	uint8_t pad399[3];                  // 0x399
	int32_t chance_txt_state;           // 0x39C
	bool show_chance_txt;               // 0x3A0
	int32_t int3A4;                     // 0x3A4
	uint8_t gap3A8[8];                  // 0x3A8
	int32_t combo_counter_state;        // 0x3B0
	float combo_counter_time;           // 0x3B4
	uint8_t gap3B8[12];                 // 0x3B8
	int32_t combo_num;                  // 0x3C4
	int32_t int3C8;                     // 0x3C8
	diva_nc::vec2 combo_counter_pos;    // 0x3CC
	int32_t dword3D8;                   // 0x3D8

	void SetBonusText(int32_t value, const diva_nc::vec2& pos);
	void RemoveBonusText();
};

struct PVGameData
{
	bool loaded;                                 // 0x00
	bool paused;                                 // 0x01
	uint8_t byte2;                               // 0x02
	uint8_t byte3;                               // 0x03
	uint8_t gap4[196];                           // 0x04 -> 0xC8
	PVGamePvData pv_data;                        // 0xC8


	uint8_t gap_to_ui[0x2C940 - 0xC8 - sizeof(PVGamePvData)];

	PVGameUI ui;                                 // 0x2C940


	uint8_t gap2CCCC[1712 - sizeof(PVGameUI)];

	int32_t dword2D228;                          // 0x2CFF0
	uint8_t gap2D22C[8];                         // 0x2CFF4
	int32_t life;                                // 0x2CFFC
	int32_t score;                               // 0x2D000
	int32_t dword2D23C;                          // 0x2D004
	bool scoring_enabled;                        // 0x2D008
	uint8_t gap2D241[3];                         // 0x2D009
	int32_t total_challenge_bonus;               // 0x2D00C
	int32_t combo;                               // 0x2D010
	int32_t total_surv_challenge_bonus;          // 0x2D014
	uint8_t gap2D250[8];                         // 0x2D018
	int32_t challenge_combo;                     // 0x2D020
	int32_t max_combo;                           // 0x2D024
	int32_t judge_count[5];                      // 0x2D028
	int32_t judge_count_correct[5];              // 0x2D03C
	uint8_t gap2D288[16];                        // 0x2D050
	float float2D298;                            // 0x2D060
	uint8_t gap2D29C[12];                        // 0x2D064
	int32_t int2D2A8;                            // 0x2D070
	uint8_t gap2D2AC;                            // 0x2D074
	uint8_t byte2D2AD;                           // 0x2D075
	uint8_t pad_alignment[2];                    // 0x2D076
	float float2D2B0;                            // 0x2D078
	int16_t word2D2B4;                           // 0x2D07C
	uint8_t gap2D2B6[6];                         // 0x2D07E
	int32_t challenge_bonus;                     // 0x2D084
	int32_t dword2D2C0;                          // 0x2D088
	uint8_t gap2D2C4[8];                         // 0x2D08C
	int32_t reference_score;                     // 0x2D094
	int32_t reference_score_with_life;           // 0x2D098
	int32_t dword2D2D4;                          // 0x2D09C
	prj::vector<int32_t> target_reference_scores;// 0x2D0A0
	uint8_t gap2D2F0[8];                         // 0x2D0B8
	int64_t qword2D2F8;                          // 0x2D0C0
	uint8_t gap2D300[4];                         // 0x2D0C8
	float percentage;                            // 0x2D0CC
	float float2D308;                            // 0x2D0D0
	float float2D30C;                            // 0x2D0D4
	uint8_t gap2D310[13];                        // 0x2D0D8
	uint8_t byte2D31D;                           // 0x2D0E5
	uint8_t gap2D31E;                            // 0x2D0E6
	uint8_t byte2D31F;                           // 0x2D0E7
	int64_t challenge_time_start;                // 0x2D0E8
	int64_t challenge_time_end;                  // 0x2D0F0
	int64_t pv_end_time;                         // 0x2D0F8
	float pv_end_time_sec;                       // 0x2D100
	float float2D33C;                            // 0x2D104
	uint8_t gap2D340[4];                         // 0x2D108
	float float2D344;                            // 0x2D10C
	int64_t qword2D348;                          // 0x2D110
	float float2D350;                            // 0x2D118
	float float2D354;                            // 0x2D11C
	int32_t dword2D358;                          // 0x2D120
	int32_t dword2D35C;                          // 0x2D124
	uint8_t gap2D360[5];                         // 0x2D128
	bool has_success_note;                       // 0x2D12D
	uint8_t gap2D366[10];                        // 0x2D12E
	int32_t dword2D370;                          // 0x2D138
	uint8_t byte2D374;                           // 0x2D13C
	uint8_t byte2D375;                           // 0x2D13D
	uint8_t gap2D376[30];                        // 0x2D13E
	int32_t current_frame;                       // 0x2D15C
	int32_t dword2D398;                          // 0x2D160
	int32_t total_life_bonus;                    // 0x2D164
	int32_t dword2D3A0;                          // 0x2D168
	uint8_t gap2D3A4[4];                         // 0x2D16C
	uint8_t byte2D3A8;                           // 0x2D170
	uint8_t byte2D3A9;                           // 0x2D171
	uint8_t gap2D3AA;                            // 0x2D172
	uint8_t byte2D3AB;                           // 0x2D173
	uint8_t gap2D3AC[2];                         // 0x2D174
	uint8_t byte2D3AE;                           // 0x2D176
	bool is_success_branch;                      // 0x2D177
};


struct SoundEffect
{
	libcxx_string button;
	libcxx_string slide;
	libcxx_string slidechain;
	libcxx_string slide_button;
	libcxx_string slide_ok;
    libcxx_string slide_ng;
	libcxx_string chime;
};

struct AetSetInfo
{
	uint32_t id;
	const char* name;
	const char* ptr10;
	const char* file_name;
	const char* ptr20;
	int32_t index;
	uint32_t spr_set_id;
};

struct AetSceneInfo
{
	uint32_t id;
	const char* name;
	const char* str10;
	int32_t dword18;
	int32_t dword1C;
};

enum GameLocale : int32_t
{
	GameLocale_JP = 0,
	GameLocale_EN = 1,
	GameLocale_ZH = 2,
	GameLocale_TW = 3,
	GameLocale_KR = 4,
	GameLocale_FR = 5,
	GameLocale_IT = 6,
	GameLocale_DE = 7,
	GameLocale_SP = 8,
	GameLocale_Max
};

namespace diva_nc
{
	struct InputState
	{
		uint8_t _data[0x1240]; // switch specific

		// 0x08 - Cursor position X (1920x1080)
		// 0x09 - Cursor position Y (1920x1080)
		// 0x10 - Cursor position X (1280x720)
		// 0x11 - Cursor position Y (1280x720)
		// 0x12 - Cursor pos delta X
		// 0x13 - Cursor pos delta Y
		// 0x14 - LStick X axis
		// 0x15 - LStick Y axis
		// 0x16 - RStick X axis
		// 0x17 - RStick Y axis
		int32_t GetPosition(int32_t index);
		int32_t GetDevice();
		bool IsButtonDown(int32_t key);
		bool IsButtonTapped(int32_t key);
		bool IsButtonTappedAbs(int32_t key);
		bool IsButtonTappedOrRepeat(int32_t key);
		inline bool IsInputBlocked() { return *reinterpret_cast<bool*>(&_data[0x1A28]); }
	};

	inline FUNCTION_PTR(InputState*, __fastcall, GetInputState, 0x1fd450, int32_t index);
}

namespace aet
{
	// NOTE: Queues AetSet file to be loaded
	inline FUNCTION_PTR(void, __fastcall, LoadAetSet, 0x21f510, uint32_t id, libcxx_string* out);
	// NOTE: Returns true if AetSet file is still loading, false otherwise
	inline FUNCTION_PTR(bool, __fastcall, CheckAetSetLoading, 0x21f5c0, uint32_t id);
	// NOTE: Free AetSet file from memory
	inline FUNCTION_PTR(void, __fastcall, UnloadAetSet, 0x21f5f0, uint32_t id);
	// NOTE: Plays an Aet layer described by the `args` variable passed in.
	inline FUNCTION_PTR(int32_t, __fastcall, Play, 0x21f930, AetArgs* args, int32_t id);
	// NOTE: Returns the current from on an Aet object.
	inline FUNCTION_PTR(float, __fastcall, GetFrame, 0x220030, int32_t id);
	// NOTE: Returns whether an Aet object has ended playback. Will always return false if the loop flag is set.
	inline FUNCTION_PTR(bool, __fastcall, GetEnded, 0x220050, int32_t id);
	// NOTE: Retrieves layout data for specific Aet object ID.
	inline FUNCTION_PTR(void, __fastcall, GetComposition, 0x2201c0, AetComposition* comp, int32_t id);
	// NOTE: Returns the time where the specified marker is placed on a layer
	inline FUNCTION_PTR(float, __fastcall, GetMarkerTime, 0x21f7d0, uint32_t id, const char* layer, const char* marker);

	// NOTE: Plays a layer from AET_GAM_CMN. Resolution mode is HDTV720

	// PlayLayerImp(3,param_1,0x20000,param_3,param_4,0,0,0,0xbf800000,0xbf800000);
	inline FUNCTION_PTR(int32_t, __fastcall, PlayLayerImp, 0x1c1950, int32_t prio, int32_t flags, const char* layer, const diva_nc::vec2* pos);

	// NOTE: Sets the position of the layer object
	inline FUNCTION_PTR(void, __fastcall, SetPosition, 0x21fc00, int32_t id, const diva_nc::vec3* pos);
	// NOTE: Sets the scale of the layer object
	inline FUNCTION_PTR(void, __fastcall, SetScale, 0x21fc20, int32_t id, const diva_nc::vec3* scale);
	// NOTE: Sets the current frame of the layer objects
	inline FUNCTION_PTR(void, __fastcall, SetFrame, 0x21ffa0, int32_t id, float frame);

	// NOTE: Sets if the layer object should play (1) or pause (0)
	inline FUNCTION_PTR(void, __fastcall, SetPlay, 0x21fb20, int32_t id, bool play);
	inline FUNCTION_PTR(void, __fastcall, SetVisible, 0x21fb40, int32_t id, bool visible);
	inline FUNCTION_PTR(void, __fastcall, SetOpacity, 0x21fc60, int32_t, float opacity);

	void CreateAetArgs(AetArgs* args, uint32_t scene_id, const char* layer_name, int32_t prio);
	void CreateAetArgs(AetArgs* args, uint32_t scene_id, const char* layer_name, int32_t flags, int32_t layer, int32_t prio, const char* start_marker, const char* end_marker);

	// NOTE: Stops (removes, not pause!) an Aet layer object created by `diva_nc::aet::Play`
	void Stop(int32_t id);
	// NOTE: Same as the normal one, but sets id to 0 afterwards
	void Stop(int32_t* id);
	// NOTE: Stops if GetEnded() returns true and returns the value of GetEnded().
	bool StopOnEnded(int32_t* id);

	int32_t PlayLayer(uint32_t scene_id, int32_t prio, int32_t flags, const char* layer, const diva_nc::vec2* pos, int32_t index, const char* start_marker, const char* end_marker, float start_time, float end_time, int32_t a11, void* frmctl);
	int32_t PlayLayer(uint32_t scene, int32_t prio, int32_t flags, const char* layer, const diva_nc::vec2* pos, const char* start_marker, const char* end_marker);

	int32_t PlayLayer(uint32_t scene_id, int32_t prio, const char* layer, int32_t action);
}

namespace spr
{

	inline FUNCTION_PTR(TextArgs*, __fastcall, DefaultTextArgs, 0x2197a0, TextArgs* args);

	// NOTE: Queues SprSet file to be loaded
	inline FUNCTION_PTR(void, __fastcall, LoadSprSet, 0x611fa0, uint32_t id, prj::string_view* out);
	// NOTE: Returns true if SprSet file is still loading, false otherwise
	inline FUNCTION_PTR(bool, __fastcall, CheckSprSetLoading, 0x6120a0, uint32_t id);
	// NOTE: Free SprSet file from memory
	inline FUNCTION_PTR(void, __fastcall, UnloadSprSet, 0x612180, uint32_t id);

	// NOTE: Draw sprite to screen
	inline FUNCTION_PTR(SprArgs*, __fastcall, DrawSprite, 0x612310, SprArgs* args);

	// NOTE: Retrieves a font from the font list
	inline FUNCTION_PTR(FontInfo*, __fastcall, GetFont, 0x2194b0, FontInfo* font, int32_t font_id);
	inline FUNCTION_PTR(FontInfo*, __fastcall, GetLocaleFont, 0x2194e0, FontInfo* font, int32_t id, bool a3);
	inline FUNCTION_PTR(void, __fastcall, SetFontSize, 0x219670, FontInfo* font, float w, float h);
	// NOTE: Draw text to the screen (UTF-8, char*)
	inline FUNCTION_PTR(void, __fastcall, DrawTextA, 0x219cd0, TextArgs* params, uint32_t flags, const char* text);
	// inline FUNCTION_PTR(void**, __fastcall, DrawSimpleText, 0x14027B160, float x, float y, int32_t res, int32_t prio, const char* text, bool center, uint32_t color, const diva_nc::vec4* clip);  // inlined into  FUN_001bf9a0 / FUN_001bf8c0

	inline void FUN_1402c5450(void* args, const void* font) {
		if (args) {
			*(const void**)((uintptr_t)args + 0x48) = font;
		}
	}
	inline void FUN_1402c5650(void* args, int32_t style) {
		if (args) {
			*(int32_t*)((uintptr_t)args + 0x28) = style;
		}
	}
	inline void DrawSimpleText(
		float x, float y, int32_t style, uint32_t prio,
		const char* text, bool is_center_aligned,
		uint32_t color, const void* clip_rect
	) {
		if (!text || *text == '\0') return;

		FontInfo font;
		GetFont(&font, 0x12);

		TextArgs args;

		DefaultTextArgs(&args);


		*(const void**)((uintptr_t)&args + 0x48) = &font;


		*(int32_t*)((uintptr_t)&args + 0x28) = style;


		*(float*)((uintptr_t)&args + 0x30) = x;
		*(float*)((uintptr_t)&args + 0x34) = y;
		*(float*)((uintptr_t)&args + 0x38) = x;
		*(float*)((uintptr_t)&args + 0x3C) = y;


		*(uint32_t*)((uintptr_t)&args + 0x20) = prio;


		uint8_t color_bytes[4];
		color_bytes[2] = (uint8_t)(color);
		color_bytes[0] = (uint8_t)(color >> 16);
		color_bytes[3] = (uint8_t)(color >> 24);
		color_bytes[1] = (uint8_t)(color >> 8);
		*(uint32_t*)((uintptr_t)&args + 0x04) = *(uint32_t*)color_bytes;


		if (clip_rect != nullptr) {

			*(uint64_t*)((uintptr_t)&args + 0x10) = *(uint64_t*)clip_rect;
			*(uint64_t*)((uintptr_t)&args + 0x18) = *((uint64_t*)clip_rect + 1);
			*(bool*)((uintptr_t)&args + 0x0C) = true;
		}


		uint32_t align_flag = is_center_aligned ? 4 : 1;


		DrawTextA(&args, align_flag | 0x10000, text);
	}
}

namespace sound
{
	// NOTE: Plays a sound effect.
	//       `queue_index` can be a value between 1~5 (?)
	inline FUNCTION_PTR(int32_t, __fastcall, PlaySoundEffect, 0x607dc0, int32_t queue_index, const char* name, float volume);

	// NOTE: Releases a sound effect from the queue.
	//
	inline FUNCTION_PTR(void, __fastcall, ReleaseCue, 0x608d40, int32_t queue_index, const char* name, bool force_release);

	// NOTE: Releases every cue in a sound queue.
	//
	inline FUNCTION_PTR(void, __fastcall, ReleaseAllCues, 0x608f50, int32_t queue_index);

	// NOTE: Requests SoundWork to load a sound farc. Returns false if the queue is full.
	//
	inline FUNCTION_PTR(bool, __fastcall, RequestFarcLoad, 0x607340, const char* path);

	// NOTE: Returns true if SoundWork is still loading the farc, false otherwise.
	//
	inline FUNCTION_PTR(bool, __fastcall, IsFarcLoading, 0x607a50, const char* path);

	// NOTE: Unloads a sound farc from memory
	//
	inline FUNCTION_PTR(bool, __fastcall, UnloadFarc, 0x607bb0, const char* path);


	// inlined kinda, so we should imitate and wrap PlaySoundEffect at 00607db0 3 times with different strings, right?

	// inline FUNCTION_PTR(void, __fastcall, PlayEnterSE, 0x1401A7F00);
	// inline FUNCTION_PTR(void, __fastcall, PlayCancelSE, 0x1401A7F20);
	// inline FUNCTION_PTR(void, __fastcall, PlaySelectSE, 0x1401A7F60);

	inline FUNCTION_PTR(bool, __fastcall, PlaySoundEffectWrapped, 0x607db0, const char* path, float volume);

	inline void PlayEnterSE() {
		PlaySoundEffectWrapped("se_ft_sys_enter_01", 1.0f);
	}
	inline void PlayCancelSE() {
		PlaySoundEffectWrapped("se_ft_sys_cansel_01", 1.0f);
	}
	inline void PlaySelectSE() {
		PlaySoundEffectWrapped("se_ft_sys_select_01", 1.0f);
	}

// void PlaySoundEffect_Wrapped_1(void)
//
// {
//   PlaySoundEffect("se_ft_sys_enter_01",0x3f800000);
//   return;
// }
/*
void PlaySoundEffect_Wrapped(void)

{
  PlaySoundEffect("se_ft_sys_cansel_01",0x3f800000);
  return;
}*/


// void FUN_1401a7f60(void)
//
// {
//   PlaySoundEffect("se_ft_sys_select_01",0x3f800000);
//   return;
// }
//


}

namespace loc
{
	inline FUNCTION_PTR(const char*, __fastcall, GetStrArrayString, 0x15d380, int32_t id); // idk i have the func hooked in strarray.cpp
	inline std::string_view GetString(int32_t id)
	{
		if (const char* data = GetStrArrayString(id); data != nullptr)
			return std::string_view(data, strlen(data));
		return std::string_view();
	}
}

inline FUNCTION_PTR(PvGameplayInfo*, __fastcall, GetPvGameplayInfo, 0x1c6530);
inline FUNCTION_PTR(bool, __fastcall, IsPracticeMode, 0x167480);
/*inline FUNCTION_PTR(int32_t, __fastcall, FindNextCommand, 0x140257D50, PVGamePvData* pv_data, int32_t op, int32_t* time, int32_t* branch, int32_t head);*/// SKIP FOR NOW (better to recreate);

// cause of this, we'll add extra function
inline FUNCTION_PTR(int64_t, __fastcall, GetTargetFlyingTime, 0x17efa0, int *param_1, uint32_t param_2);


inline int32_t FindNextCommand(PVGamePvData* pv_data, int32_t op, int32_t* time, int32_t* branch, int32_t head) {
    if (!pv_data || head < 0) {
        return head;
    }

    int32_t pos = head;


    int32_t current_time = -1;


    while (pos < 45000) {
        int32_t cmd_op = pv_data->script_buffer[pos];


        if (cmd_op == 1) {
            if (pos > 0xAFC6) {
                return -1;
            }

            current_time = pv_data->script_buffer[pos + 1];
        }

        else if (cmd_op == 0x41) {
            if (pos > 0xAFC6) {
                return -1;
            }
            if (branch != nullptr) {
                *branch = pv_data->script_buffer[pos + 1];
            }
        }

        else if (cmd_op == op) {
            if (time != nullptr) {
                *time = current_time;
            }
            return pos;
        }

        else if (cmd_op == 0x3D || cmd_op == 0x00 || cmd_op == 0x20) {
            return -1;
        }

        int32_t step = (int32_t)GetTargetFlyingTime(&pv_data->script_buffer[pos], pv_data->is_dsc_ac200_format);
        pos += step + 1;
    }

    return -1;
}

// in case
//DAT_140dac350 == DAT_4d283990

inline FUNCTION_PTR(PVGameData*, __fastcall, GetPVGameData, 0x1a4540);
inline FUNCTION_PTR(bool, __fastcall, IsInSongResults, 0xe3040);
inline FUNCTION_PTR(int64_t, __fastcall, DrawTriangles, 0x612540, SpriteVertex* vertices, size_t vertex_count, int32_t res_mode, int32_t prio, uint32_t sprite_id, uint32_t a6);
inline FUNCTION_PTR(int32_t, __fastcall, GetGameLocale, 0x21c960);

inline FUNCTION_PTR(bool, __fastcall, IsSuddenEquipped, 0x17b9b0, PVGameData* pv_game);

inline FUNCTION_PTR(AetSetInfo*, __fastcall, GetAetSetInfoByName, 0x1e2580, void* a1, const libcxx_string& name);
inline FUNCTION_PTR(AetSceneInfo*, __fastcall, GetAetSceneInfoByName, 0x1e27b0, void* a1, const libcxx_string& name);

diva_nc::vec2 GetScaledPosition(const diva_nc::vec2& v);


namespace dsc
{
	struct OpcodeInfo
	{
		int32_t id;
		int32_t length_old;
		int32_t length;
		int32_t unk0C;
		const char* name;
	};

	// NOTE: Returns a pointer to a struct that describes DSC opcode <id>
	//inline FUNCTION_PTR(const OpcodeInfo*, __fastcall, GetOpcodeInfo, 0x14024DCA0, int32_t id); // skip, just do imitate

	inline uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
	inline uintptr_t DAT_4d283990 = base + 0x4d283990;

	inline const OpcodeInfo* GetOpcodeInfo(int param_1) {
		if ((uint64_t)param_1 < 0x6b) {
			return (OpcodeInfo*)(DAT_4d283990 + ((int64_t)param_1 * 0x18));
		}
		return nullptr;
	}

// undefined * thunk_FUN_1502a6090(int param_1)
//
// {
//   if ((ulonglong)(longlong)param_1 < 0x6b) {
//     return &DAT_140dac350 + (longlong)param_1 * 0x18;
//   }
//   return (undefined *)0x0;
// }

	inline bool IsCurrentDifficulty(int32_t bit) { return (bit & (1 << GetPvGameplayInfo()->difficulty)) != 0; }
}

namespace game
{
	struct GlobalPVInfo
	{
		int32_t difficulty; // NOTE: Extra-extreme is set here as 4, and not 3 with edition 1!
		int32_t pv_id;
	};

	struct PVLoadParam
	{
		struct Data
		{
			int32_t difficulty;
			int32_t edition;
			int32_t int8;
			uint8_t gap0[0x50];
			int32_t pv_id;
			uint8_t gap60[0x648];
		};

		Data data[4];
	};

	struct NoteSE
	{
		int32_t id;
		libcxx_string name;
		libcxx_string se_name;
	};

	inline bool IsFutureToneMode() {
		uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
		return *reinterpret_cast<bool*>(base + 0xcdf8ba);
	}
	inline bool IsPvMode() {
		return GetPvGameplayInfo()->type == 3 || GetPvGameplayInfo()->type == 6; }
	inline int32_t GetFramerate() {
		uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
		return *reinterpret_cast<int32_t*>(base + 0xcdf9b4);
	}

	// NOTE: The CustomizeSel doesn't have duplicated code for the Switch version
	//       like other UI screens, so this is valid for both Switch and PS4 mode.
	inline FUNCTION_PTR(bool, __fastcall, IsCustomizeSelTaskReady, 0x778700);

	inline FUNCTION_PTR(GlobalPVInfo*, __fastcall, GetGlobalPVInfo, 0xc6330);
	inline FUNCTION_PTR(PVLoadParam*, __fastcall, GetPVLoadParam, 0x3d41a0);

	inline FUNCTION_PTR(char*, __fastcall, GetSaveData, 0xc9590);
	inline FUNCTION_PTR(void*, __fastcall, GetConfigSet, 0xc91a0, void* save_data, int32_t pv, bool a3);

	inline FUNCTION_PTR(NoteSE*, __fastcall, GetButtonSE, 0x3b5ee0, int32_t id);

	int32_t GetGlobalPvID();
}

namespace pv_db
{
	struct PvDBDifficulty
	{
		int32_t difficulty;
		int32_t edition;
		int32_t extra;
		int32_t dwordC;
		int64_t attributes;
		int32_t gap18[2];
		libcxx_string script_file_name;
		int32_t level;
		int32_t level_sort_index;
		libcxx_string button_se;
		libcxx_string success_se;
		libcxx_string slide_se;
		libcxx_string slidechain_start_se;
		libcxx_string slidechain_se;
		libcxx_string slidechain_success_se;
		libcxx_string slidechain_failure_se;
		libcxx_string slide_touch_se;
		uint8_t gap148[792];
		int32_t version;
		int32_t script_format;
		int32_t high_speed_rate;
		float hidden_timing;
		float sudden_timing;
		bool chara_scale;
		uint8_t gap4F0[43];
	};

	struct PvDBEntry
	{
		int32_t pv_id;
		int32_t date;
		libcxx_string song_name;
		libcxx_string song_name_reading;
		int32_t unk48;
		int32_t bpm;
		libcxx_string song_file_name;
		uint8_t gap70[24];
		float sabi_start_time;
		float sabi_play_time;
		uint8_t gap90[32];
		prj::vector<PvDBDifficulty> difficulties[5];
		uint8_t gap128[664];
	};

	inline FUNCTION_PTR(void*, __fastcall, FindPVEntry, 0x4be4f0, int32_t pv_id);
	inline FUNCTION_PTR(PvDBDifficulty*, __fastcall, FindDifficulty, 0x4be700, void* entry, int32_t difficulty, int32_t edition);
}

// NOTE: File IO functions
typedef void* FileHandler;

inline FUNCTION_PTR(bool, __fastcall, FileRequestLoad, 0x1f3a70, FileHandler* file, const char* path, int32_t flags);
inline FUNCTION_PTR(bool, __fastcall, FileCheckExists, 0x1f4480, libcxx_string* path, libcxx_string* fixed);
inline FUNCTION_PTR(bool, __fastcall, FileCheckNotReady, 0x1f3930, FileHandler* file);
inline FUNCTION_PTR(void*, __fastcall, FileGetData, 0x1f4070, FileHandler* file);
inline FUNCTION_PTR(size_t, __fastcall, FileGetSize, 0x1f40a0, FileHandler* file);
inline FUNCTION_PTR(void, __fastcall, FileFree, 0x1f39e0, FileHandler* file);

inline bool ShouldUpdateTargets()
{
	PVGameData* pv_game = GetPVGameData();
	if (pv_game != nullptr)
		return !IsInSongResults() && !pv_game->paused && !pv_game->byte2;

	return false;
}
