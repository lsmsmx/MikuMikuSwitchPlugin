#pragma once
#include <stdint.h>

enum ScoreMode : int32_t
{
	ScoreMode_Arcade  = 0,
	ScoreMode_F2nd    = 1,
	ScoreMode_Franken = 2,
	ScoreMode_Max
};

enum GameStyle : int32_t
{
	GameStyle_Arcade  = 0,
	GameStyle_Console = 1,
	GameStyle_Mixed   = 2,
	GameStyle_Max
};

enum ModeSelect : int32_t
{
	ModeSelect_ChallengeStart = 1,
	ModeSelect_ChallengeEnd   = 3,
	ModeSelect_ChanceStart    = 4,
	ModeSelect_ChanceEnd      = 5,
	ModeSelect_TechZoneStart  = 8,
	ModeSelect_TechZoneEnd    = 9,
};
