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

static double
scene_history_half_life(float persistence)
{
    double amount = isfinite(persistence) ? CLAMP(persistence, 0.0f, 1.0f) : 0.0;
    return 0.025 + amount * amount * 0.62;
}

gint64
chrono_scene_history_deadline(float persistence, gint64 now_us)
{
    if (!isfinite(persistence) || persistence <= 0.0001f || now_us <= 0)
        return 0;
    gint64 tail_us = (gint64)ceil(10.0 * scene_history_half_life(persistence) *
                                 G_USEC_PER_SEC);
    return now_us > G_MAXINT64 - tail_us ? G_MAXINT64 : now_us + tail_us;
}

ChronoSceneHistoryStep
chrono_scene_history_step(gint64 *deadline_us,
                          gint64 previous_update_us,
                          gint64 now_us)
{
    g_return_val_if_fail(deadline_us != NULL, CHRONO_SCENE_HISTORY_IDLE);
    if (*deadline_us <= 0 || now_us <= 0)
        return CHRONO_SCENE_HISTORY_IDLE;
    if (now_us < *deadline_us) {
        /* Do not turn a 12Hz progress update into continuous full-window
         * CRT work. Cursor-only native frames keep using the cached base. */
        if (previous_update_us > 0 &&
            (now_us <= previous_update_us ||
             now_us - previous_update_us < CHRONO_PROGRESS_MIN_UPDATE_US))
            return CHRONO_SCENE_HISTORY_IDLE;
        return CHRONO_SCENE_HISTORY_DECAY;
    }
    *deadline_us = 0;
    return CHRONO_SCENE_HISTORY_CLEANUP;
}

float
chrono_scene_persistence_decay(float persistence, double elapsed_seconds)
{
    /* Temporal decay uses real elapsed time, independently of animation's
     * bounded integration step. A long idle interval must age old emission. */
    double delta = isnan(elapsed_seconds) ? 0.0 : MAX(elapsed_seconds, 0.0);
    return (float)exp(-0.6931471805599453 * delta /
                      scene_history_half_life(persistence));
}
