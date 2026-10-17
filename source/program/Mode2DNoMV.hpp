#pragma once

#include <cstdint>
#include "nc/diva_nc.hpp"

namespace Mode2DNoMV {
    extern bool is3D;
    extern bool hasMovie;

    extern bool g_force2D;
    extern bool g_noMovie;
    extern bool g_isBgSubstituted;

    void init();
    bool ShouldBlockMovie();
    bool ShouldApply2D();
    void RestoreOriginalFields();
    void CleanDscPostProcess(PVGamePvData* pv_data);
    void ProcessDsc(PVGamePvData* pv_data);
}
