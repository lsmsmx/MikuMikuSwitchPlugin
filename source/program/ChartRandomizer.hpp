#pragma once

#include <cstdint>
#include <map>

struct DscFrame;

namespace ChartRandomizer {

    enum RandomMode : int32_t {
        Mode_Off     = 0,
        Mode_Partial = 1,
        Mode_Full    = 2
    };

    RandomMode GetMode();
    void SetMode(RandomMode mode);
    void CycleMode();

    int32_t GetChance();
    void SetChance(int32_t chance);
    void AdjustChance(int32_t delta);

    bool GetRerollOnRetry();
    void SetRerollOnRetry(bool enable);
    void ToggleRerollOnRetry();

    bool IsEnabled();

    void Randomize(std::map<int32_t, DscFrame>& chart_data);

} // namespace ChartRandomizer
