#pragma once
#include <string>
#include <vector>
#include <stdint.h>

struct Config {
    static bool enableMods;
    static bool enableDebug;
    static std::string modsDirectoryPath;
    static std::vector<std::string> priorityPaths;

    // Feature Master Toggles
    static bool enableFtUi;
    static bool enableNewClassics;

    // Gameplay
    static std::string challengeTimeMode;
    static bool removeWatermarks;
    static bool disableHandScaling;
    static bool disableLyrics;
    static bool forceJapanese;
    static bool forceFtUI;
    static bool ExPatch;
    static bool enableTouch;
    static bool enableKeyboard;

    // Graphics
    static bool disableAdp;
    static bool enableSss;
    static bool cstmMenuFtStyle;
    static bool extraFtGraphics;
    static std::string antiAliasing;
    static std::string magFilterMode;
    static std::string ssaaMode;

    // Core Graphics Values
    static float exposure;
    static float gamma;

    // FXAA Settings
    static float fxaaQualitySubpix;
    static float fxaaQualityEdgeThreshold;
    static float fxaaQualityEdgeThresholdMin;

    // Force Disables
    static bool disableReflections;
    static bool disableShadows;
    static bool disableSelfShadow;
    static bool disableDOF;

    // Preset Toggles
    static bool ftShadows;
    static bool ftReflect;
    static bool ftRefract;
    static bool advancedGraphics;

    // Optimisations
    static bool force30fps;
    static float resScaler;
    static float shadowIntensity;
    static float reflectionQuality;

    // Advanced Custom Resolutions
    static uint32_t shadowMap1W;
    static uint32_t shadowMap1H;
    static uint32_t shadowViewportW;
    static uint32_t shadowViewportH;
    static uint32_t shadowTex1W;
    static uint32_t shadowTex1H;
    static uint32_t shadowTex2W;
    static uint32_t shadowTex2H;
    static uint32_t shadowMap3W;
    static uint32_t shadowMap3H;
    static uint32_t shadowMap4W;
    static uint32_t shadowMap4H;
    static uint32_t shadowMap5W;
    static uint32_t shadowMap5H;
    static uint32_t shadowMap6W;
    static uint32_t shadowMap6H;
    static uint32_t reflectW;
    static uint32_t reflectH;
    static uint32_t refractW;
    static uint32_t refractH;

    static bool init();
};
