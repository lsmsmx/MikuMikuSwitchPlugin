#pragma once

#include <cstdint>

namespace Hints {

    void Init();
    void SetPvSelActive(bool active);
    void SetSongConfirmActive(bool active);
    void SetCustomizeSceneActive();
    bool IsVisible();
    void ProcessInput();
    void Draw();

} // namespace Hints
