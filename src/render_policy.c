#include "render_policy.h"

#include <math.h>

int
chrono_progress_visual_step(double progress_fraction)
{
    if (!isfinite(progress_fraction))
        progress_fraction = 0.0;
    progress_fraction = CLAMP(progress_fraction, 0.0, 1.0);
    return CLAMP((int)floor(progress_fraction *
                            CHRONO_PROGRESS_VISUAL_STEPS),
                 0,
                 CHRONO_PROGRESS_VISUAL_STEPS);
}

gboolean
chrono_progress_update_due(int previous_step,
                           gint64 previous_update_us,
                           int next_step,
                           gint64 now_us)
{
    next_step = CLAMP(next_step, 0, CHRONO_PROGRESS_VISUAL_STEPS);
    if (previous_step < 0)
        return TRUE;
    if (next_step == previous_step)
        return FALSE;
    if (next_step >= CHRONO_PROGRESS_VISUAL_STEPS)
        return TRUE;
    if (previous_update_us <= 0 || now_us <= previous_update_us)
        return TRUE;
    return now_us - previous_update_us >= CHRONO_PROGRESS_MIN_UPDATE_US;
}

gboolean
chrono_render_request_needed(gboolean force_every_tick,
                             gboolean scene_or_cache_dirty,
                             gboolean pointer_changed,
                             gboolean trail_active)
{
    return force_every_tick || scene_or_cache_dirty ||
           pointer_changed || trail_active;
}
