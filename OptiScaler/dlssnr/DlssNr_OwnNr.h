#pragma once

#include <Config.h>
#include "DlssNr_GameNrRuntime.h"

namespace DlssNr
{
// OptiScaler's own NR runs while enabled, and yields while the game runs its own NR (NGX feature 18), so the picture
// is never processed twice. The effect matches unticking "Enable Neural Rendering"; the saved setting is unchanged.
inline constexpr const char* GameNrActiveMessage =
    "The game is running its own Neural Rendering, so OptiScaler's NR is turned off. Turn the game's NR off to use "
    "OptiScaler's.";

inline bool OwnNrEnabled() { return Config::Instance()->DlssNrEnabled.value_or_default() && !GameNr::Active(); }
} // namespace DlssNr
