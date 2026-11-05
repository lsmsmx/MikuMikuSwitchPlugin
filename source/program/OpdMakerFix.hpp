#pragma once

#include <stdint.h>

namespace OpdMakerFix {

    // Makes the game's own OPD baker (debug menu -> OSAGE PLAY DATA MAKE MODE)
    // record frame 0 from a settled physics state.  See the top of the .cpp for
    // what the baker does today and why that is wrong for ported motions.
    void init();

} // namespace OpdMakerFix