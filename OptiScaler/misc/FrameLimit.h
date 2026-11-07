#pragma once
#include "SysUtils.h"
#include "FrameLimitPolicy.h"

class FrameLimit
{
    static uint64_t get_timestamp();
    static int timer_sleep(int64_t hundred_ns);
    static int busywait_sleep(int64_t ns);
    static int combined_sleep(int64_t ns);

  public:
    // outputMultiplier: presented frames per call (1 without frame generation; see FrameLimitPolicy::OutputMultiplier).
    static void sleep(uint32_t outputMultiplier);
};
