#include "power_policy.h"

ChronoPowerMode
chrono_power_choose_mode(ChronoPowerInputs inputs)
{
    if (inputs.renderer_ready && inputs.window_mapped && inputs.window_active &&
        !inputs.window_suspended)
        return CHRONO_POWER_INTERACTIVE;

    /* Background timing is semantic state, not animation. It needs only a
     * one-second maintenance wake-up for clock text, expiry and checkpoints. */
    if (inputs.timer_active)
        return CHRONO_POWER_BACKGROUND_CLOCK;

    return CHRONO_POWER_SLEEP;
}
