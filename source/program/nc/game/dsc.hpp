#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <initializer_list>

struct DscFrame
{
    struct DscCommand
    {
        int32_t length;
        int32_t opcode;
        int32_t data[30];

        DscCommand() = default;
        DscCommand(int32_t opcode, std::initializer_list<int32_t> params);
        DscCommand(int32_t opcode, const int32_t* params, size_t count);
    };

    std::array<std::vector<DscCommand>, 3> branch_modes;
};

struct PVGamePvData;
void RerollDscOnReset(PVGamePvData& pv_data);

void InstallDSCHooks();

namespace dsc {
    inline void init() {
         InstallDSCHooks();
    }
}


