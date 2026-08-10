#pragma once

#include <glib.h>

G_BEGIN_DECLS

/* The timer ring is a finite-resolution instrument. Rebuilding its clean
 * scene and selective-AA targets more frequently than this cannot produce a
 * meaningful visible change on a 1080p display. */
#define CHRONO_PROGRESS_VISUAL_STEPS 1440
#define CHRONO_PROGRESS_MAX_UPDATE_HZ 12
#define CHRONO_PROGRESS_MIN_UPDATE_US \
    (G_USEC_PER_SEC / CHRONO_PROGRESS_MAX_UPDATE_HZ)

int chrono_progress_visual_step(double progress_fraction);

gboolean chrono_progress_update_due(int previous_step,
                                     gint64 previous_update_us,
                                     int next_step,
                                     gint64 now_us);

/* Interactive presentation is driven exclusively by GTK's native frame
 * clock. Full-window CRT work is event-driven; only a short profile morph can
 * deliberately request every native tick. */

gboolean chrono_render_request_needed(gboolean force_every_tick,
                                       gboolean scene_or_cache_dirty,
                                       gboolean pointer_changed,
                                       gboolean trail_active);

G_END_DECLS
