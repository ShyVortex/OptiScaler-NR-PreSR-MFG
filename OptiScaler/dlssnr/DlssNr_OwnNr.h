#pragma once

#include <Config.h>
#include "DlssNr_GameNrRuntime.h"

namespace DlssNr
{
// OptiScaler's own NR runs while enabled, and yields while the game runs its own NR (NGX feature 18), so the picture
// is never processed twice. The effect matches unticking "Enable Neural Rendering"; the saved setting is unchanged.
inline constexpr const char* GameNrPausedMessage =
    "The game is running Neural Rendering itself, so OptiScaler's NR is paused. It resumes when the game turns its "
    "NR off.";

inline bool OwnNrEnabled() { return Config::Instance()->DlssNrEnabled.value_or_default() && !GameNr::Active(); }
} // namespace DlssNr
