#pragma once

#include <stdint.h>

// Switch port of DivaModLoader's ThumbnailLoader: preloads the sprite sets that
// hold modded per-PV thumbnails (SPR_SEL_PVTMB_xxx / _EX), because the engine
// only keeps the vanilla thumbnail set loaded.
namespace ThumbnailLoader {

    // Installs the per-PV sprite-id hook. Call from exl_main, before nnMain runs.
    void init();

    // Erases finished loads from the pending list. Call once per PvDb task tick
    // (see shared_hooks.cpp).
    void OnPvDbTask();

}