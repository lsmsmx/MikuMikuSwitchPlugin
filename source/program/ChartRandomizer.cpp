#include "ChartRandomizer.hpp"
#include "nc/game/dsc.hpp"
#include "lib.hpp"
#include <nn/os.hpp>
#include <random>
#include <algorithm>
#include <array>
#include <vector>
#include <unordered_map>

namespace ChartRandomizer {

    static RandomMode s_mode   = Mode_Off;
    static int32_t    s_chance = 100; // 0..100%

    RandomMode GetMode() { return s_mode; }
    void SetMode(RandomMode mode) { s_mode = mode; }

    void CycleMode() {
        if (s_mode == Mode_Off) s_mode = Mode_Partial;
        else if (s_mode == Mode_Partial) s_mode = Mode_Full;
        else s_mode = Mode_Off;
    }

    int32_t GetChance() { return s_chance; }
    void SetChance(int32_t chance) {
        s_chance = std::clamp(chance, 0, 100);
    }
    void AdjustChance(int32_t delta) {
        SetChance(s_chance + delta);
    }

    bool IsEnabled() {
        return s_mode != Mode_Off;
    }

    struct LongNoteTracker {
        bool active = false;
        int32_t assigned_type = -1;
    };

    void Randomize(std::map<int32_t, DscFrame>& chart_data)
    {
        if (s_mode == Mode_Off)
            return;

        // Switch seed
        uint64_t tick = nn::os::GetSystemTick().GetInt64Value();
        std::mt19937 rng(static_cast<uint32_t>(tick ^ (tick >> 32)));

        std::array<int32_t, 4> button_map = { 0, 1, 2, 3 };
        do {
            std::shuffle(button_map.begin(), button_map.end(), rng);
        } while (button_map[0] == 0 || button_map[1] == 1 || button_map[2] == 2 || button_map[3] == 3);

        std::array<LongNoteTracker, 4> long_tracker = {};

        for (auto& [time, frame] : chart_data)
        {
            for (int branch = 0; branch < 3; branch++)
            {
                std::vector<int32_t> pool = { 0, 1, 2, 3 };
                std::shuffle(pool.begin(), pool.end(), rng);
                size_t pool_idx = 0;

                for (auto& cmd : frame.branch_modes[branch])
                {
                    if (cmd.opcode != 6) // DscOp_Target
                        continue;

                    int32_t& type = cmd.data[0];

                    if (type >= 33 && type <= 36)
                    {
                        int32_t base = type - 33; // 0: Triangle, 1: Circle, 2: Cross, 3: Square

                        if (!long_tracker[base].active)
                        {
                            long_tracker[base].active = true;

                            bool should_change = (s_chance >= 100) || (static_cast<int32_t>(rng() % 100) < s_chance);

                            if (should_change) {
                                if (s_mode == Mode_Partial) {
                                    long_tracker[base].assigned_type = 33 + button_map[base];
                                } else { // Mode_Full
                                    int32_t picked = pool[pool_idx % pool.size()];
                                    pool_idx++;
                                    long_tracker[base].assigned_type = 33 + picked;
                                }
                            } else {
                                long_tracker[base].assigned_type = type;
                            }

                            type = long_tracker[base].assigned_type;
                        }
                        else
                        {
                            type = long_tracker[base].assigned_type;

                            long_tracker[base].active = false;
                            long_tracker[base].assigned_type = -1;
                        }

                        continue;
                    }

                    bool should_change = (s_chance >= 100) || (static_cast<int32_t>(rng() % 100) < s_chance);
                    if (!should_change)
                        continue;

                    if (s_mode == Mode_Partial)
                    {
                        if (type >= 0 && type <= 3)         type = button_map[type];
                        else if (type >= 4 && type <= 7)    type = 4 + button_map[type - 4];
                        else if (type == 12)                type = 13;
                        else if (type == 13)                type = 12;
                        else if (type >= 29 && type <= 32)  type = 29 + button_map[type - 29];
                        else if (type >= 25 && type <= 28)  type = 25 + button_map[type - 25];
                    }
                    else if (s_mode == Mode_Full)
                    {
                        int32_t picked = pool[pool_idx % pool.size()];
                        pool_idx++;

                        if (type >= 0 && type <= 3)         type = picked;
                        else if (type >= 4 && type <= 7)    type = 4 + picked;

                        else if (type == 12 || type == 13)
                        {
                            type = (rng() % 2 == 0) ? 12 : 13;
                        }

                        else if (type >= 29 && type <= 32)  type = 29 + picked;
                        else if (type >= 25 && type <= 28)  type = 25 + picked;
                    }
                }
            }
        }
    }

} // namespace ChartRandomizer
