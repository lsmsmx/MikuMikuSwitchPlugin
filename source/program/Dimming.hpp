#pragma once
#include <cstdint>
#include <map>

namespace Dimming {
    void init();
    void SetPvDim(int32_t pv_id, int32_t dim_percent);
}