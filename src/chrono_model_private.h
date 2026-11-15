#pragma once

#include "chrono_model.h"

G_BEGIN_DECLS

/* Constructs a fully populated task from persisted data. All strings are
 * copied and every model invariant is validated before the task is returned. */
ChronoTask *chrono_task_new_loaded(const char *id,
                                   const char *title,
                                   gint64 estimate_seconds,
                                   gint64 allocated_seconds,
                                   gint64 elapsed_seconds,
                                   gint64 remaining_seconds,
                                   ChronoTaskStatus status,
                                   gint64 created_at_unix,
                                   gint64 completed_at_unix,
                                   gint64 elapsed_remainder_us);

/* Persistence-only fast path. The task must already be fully validated.
 * Ownership transfers on success and remains with the caller on failure. */
gboolean chrono_store_adopt_loaded_task(ChronoStore *store, ChronoTask *task);

gint64 chrono_task_get_active_started_monotonic_us(
    const ChronoTask *task);
gint64 chrono_task_effective_elapsed_remainder_us(const ChronoTask *task,
                                                  gint64 now_monotonic_us);

gboolean chrono_task_is_valid(const ChronoTask *task);
gboolean chrono_store_is_consistent(const ChronoStore *store);

G_END_DECLS
