#pragma once

#include <stdbool.h>

typedef enum {
    CHRONO_POWER_INTERACTIVE,
    CHRONO_POWER_BACKGROUND_CLOCK,
    CHRONO_POWER_SLEEP,
} ChronoPowerMode;

typedef struct {
    bool renderer_ready;
    bool window_mapped;
    bool window_active;
    bool window_suspended;
    bool timer_active;
} ChronoPowerInputs;

ChronoPowerMode chrono_power_choose_mode(ChronoPowerInputs inputs);
