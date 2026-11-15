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
 * clock. Full-window CRT work is event-driven; decay-only frames use the
 * progress cadence, while a short profile morph may request every tick. */

gboolean chrono_render_request_needed(gboolean force_every_tick,
                                       gboolean scene_or_cache_dirty,
                                       gboolean pointer_changed,
                                       gboolean trail_active);

typedef enum {
    CHRONO_SCENE_HISTORY_IDLE,
    CHRONO_SCENE_HISTORY_DECAY,
    CHRONO_SCENE_HISTORY_CLEANUP,
} ChronoSceneHistoryStep;

/* Charge only on a scene change, never on a decay-only frame. After ten
 * half-lives, a final clean render removes quantized history and stops work. */
gint64 chrono_scene_history_deadline(float persistence, gint64 now_us);
ChronoSceneHistoryStep chrono_scene_history_step(gint64 *deadline_us,
                                                gint64 previous_update_us,
                                                gint64 now_us);
float chrono_scene_persistence_decay(float persistence, double elapsed_seconds);

G_END_DECLS
