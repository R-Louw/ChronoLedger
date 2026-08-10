#pragma once

#include <glib.h>

G_BEGIN_DECLS

#define CHRONO_TASK_TITLE_MAX_CHARS 200
#define CHRONO_TASK_ID_MAX_BYTES 128
#define CHRONO_STORE_MAX_TASKS 10000u

typedef enum {
    CHRONO_TASK_ERROR_NOT_FOUND,
    CHRONO_TASK_ERROR_INVALID_STATE,
    CHRONO_TASK_ERROR_INVALID_ARGUMENT,
} ChronoTaskError;

#define CHRONO_TASK_ERROR (chrono_task_error_quark())
GQuark chrono_task_error_quark(void);

typedef enum {
    CHRONO_TASK_READY,
    CHRONO_TASK_ACTIVE,
    CHRONO_TASK_PAUSED,
    CHRONO_TASK_EXPIRED,
    CHRONO_TASK_DONE,
} ChronoTaskStatus;

typedef struct _ChronoTask ChronoTask;
typedef struct _ChronoStore ChronoStore;

typedef struct {
    guint completed_count;
    gint64 total_estimate_seconds;
    gint64 total_elapsed_seconds;
} ChronoStats;

ChronoStore *chrono_store_new(void);
ChronoStore *chrono_store_copy(const ChronoStore *store);
void chrono_store_free(ChronoStore *store);

guint chrono_store_get_task_count(const ChronoStore *store);
ChronoTask *chrono_store_get_task_at(ChronoStore *store, guint index);
const ChronoTask *chrono_store_get_task_at_const(const ChronoStore *store,
                                                  guint index);

ChronoTask *chrono_task_new(const char *title,
                            gint64 estimate_seconds,
                            gint64 created_at_unix);
ChronoTask *chrono_task_copy(const ChronoTask *task);
void chrono_task_free(ChronoTask *task);

const char *chrono_task_get_id(const ChronoTask *task);
const char *chrono_task_get_title(const ChronoTask *task);
gint64 chrono_task_get_estimate_seconds(const ChronoTask *task);
gint64 chrono_task_get_allocated_seconds(const ChronoTask *task);
gint64 chrono_task_get_elapsed_seconds(const ChronoTask *task);
gint64 chrono_task_get_remaining_seconds(const ChronoTask *task);
ChronoTaskStatus chrono_task_get_status(const ChronoTask *task);
gint64 chrono_task_get_created_at_unix(const ChronoTask *task);
gint64 chrono_task_get_completed_at_unix(const ChronoTask *task);

const char *chrono_task_status_to_string(ChronoTaskStatus status);
gboolean chrono_task_status_from_string(const char *text,
                                         ChronoTaskStatus *out_status);

/* On success, ownership of task transfers to store.  On failure, the caller
 * retains ownership and the store is unchanged. */
gboolean chrono_store_add_task(ChronoStore *store, ChronoTask *task);
gboolean chrono_store_remove_task(ChronoStore *store, const char *task_id);
guint chrono_store_clear_completed(ChronoStore *store);
ChronoTask *chrono_store_find_task(ChronoStore *store, const char *task_id);
const ChronoTask *chrono_store_find_task_const(const ChronoStore *store,
                                                const char *task_id);
ChronoTask *chrono_store_get_active_task(ChronoStore *store);
const ChronoTask *chrono_store_get_active_task_const(const ChronoStore *store);
gboolean chrono_store_has_active_task(const ChronoStore *store);
ChronoTask *chrono_store_first_expired_task(ChronoStore *store);
const ChronoTask *chrono_store_first_expired_task_const(
    const ChronoStore *store);

gint64 chrono_task_effective_remaining(const ChronoTask *task,
                                        gint64 now_monotonic_us);
gint64 chrono_task_effective_elapsed(const ChronoTask *task,
                                      gint64 now_monotonic_us);
double chrono_task_progress_fraction(const ChronoTask *task,
                                     gint64 now_monotonic_us);
ChronoTask *chrono_store_update_expired(ChronoStore *store,
                                        gint64 now_monotonic_us);

gboolean chrono_store_start_task(ChronoStore *store,
                                  const char *task_id,
                                  gint64 now_monotonic_us,
                                  GError **error);
gboolean chrono_store_pause_active(ChronoStore *store,
                                    gint64 now_monotonic_us);
gboolean chrono_store_complete_task(ChronoStore *store,
                                     const char *task_id,
                                     gint64 now_monotonic_us,
                                     GError **error);
gboolean chrono_store_add_time(ChronoStore *store,
                               const char *task_id,
                               gint64 seconds,
                               GError **error);

void chrono_store_get_completed_stats(const ChronoStore *store,
                                       ChronoStats *out_stats);

G_END_DECLS
