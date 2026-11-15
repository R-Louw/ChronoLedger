#include "chrono_model.h"
#include "chrono_model_private.h"

#include <string.h>

struct _ChronoTask {
    char *id;
    char *title;
    gint64 estimate_seconds;
    gint64 allocated_seconds;
    gint64 elapsed_seconds;
    gint64 elapsed_remainder_us;
    gint64 remaining_seconds;
    ChronoTaskStatus status;
    gint64 active_started_monotonic_us;
    gint64 created_at_unix;
    gint64 completed_at_unix;
};

struct _ChronoStore {
    GPtrArray *tasks;
    GPtrArray *completed_tasks; /* Borrowed task pointers; no element destructor. */
    gboolean completed_order_dirty;
};

static gboolean
task_identity_is_valid(const ChronoTask *task)
{
    return task != NULL &&
           task->id != NULL && *task->id != '\0' &&
           strlen(task->id) <= CHRONO_TASK_ID_MAX_BYTES &&
           g_utf8_validate(task->id, -1, NULL) &&
           task->title != NULL && *task->title != '\0' &&
           g_utf8_validate(task->title, -1, NULL) &&
           g_utf8_strlen(task->title, -1) <= CHRONO_TASK_TITLE_MAX_CHARS;
}

static gboolean
task_allocation_is_valid(const ChronoTask *task)
{
    if (task == NULL ||
        task->estimate_seconds <= 0 ||
        task->allocated_seconds <= 0 ||
        task->allocated_seconds < task->estimate_seconds ||
        task->elapsed_seconds < 0 ||
        task->remaining_seconds < 0 ||
        task->elapsed_remainder_us < 0 ||
        task->elapsed_remainder_us >= G_USEC_PER_SEC ||
        (task->remaining_seconds == 0 && task->elapsed_remainder_us != 0) ||
        task->elapsed_seconds > task->allocated_seconds ||
        task->remaining_seconds > task->allocated_seconds ||
        task->allocated_seconds - task->elapsed_seconds !=
            task->remaining_seconds ||
        task->status < CHRONO_TASK_READY ||
        task->status > CHRONO_TASK_DONE)
        return FALSE;

    switch (task->status) {
    case CHRONO_TASK_READY:
    case CHRONO_TASK_PAUSED:
        return task->remaining_seconds > 0 &&
               task->active_started_monotonic_us == 0 &&
               task->completed_at_unix == 0;
    case CHRONO_TASK_ACTIVE:
        return task->remaining_seconds > 0 &&
               task->active_started_monotonic_us > 0 &&
               task->completed_at_unix == 0;
    case CHRONO_TASK_EXPIRED:
        return task->remaining_seconds == 0 &&
               task->active_started_monotonic_us == 0 &&
               task->completed_at_unix == 0;
    case CHRONO_TASK_DONE:
        return task->active_started_monotonic_us == 0 &&
               task->completed_at_unix > 0;
    }
    return FALSE;
}

static gboolean
store_has_task_array(const ChronoStore *store)
{
    return store != NULL && store->tasks != NULL;
}

static gboolean
store_tasks_are_consistent(const ChronoStore *store)
{
    if (!store_has_task_array(store) ||
        store->tasks->len > CHRONO_STORE_MAX_TASKS)
        return FALSE;

    gboolean valid = TRUE;
    guint active_count = 0;
    GHashTable *ids = g_hash_table_new(g_str_hash, g_str_equal);
    for (guint i = 0; i < store->tasks->len; ++i) {
        const ChronoTask *task = g_ptr_array_index(store->tasks, i);
        if (!task_identity_is_valid(task) ||
            !task_allocation_is_valid(task) ||
            task->created_at_unix < 0 || task->completed_at_unix < 0 ||
            (task->completed_at_unix > 0 &&
             task->completed_at_unix < task->created_at_unix) ||
            g_hash_table_contains(ids, task->id)) {
            valid = FALSE;
            break;
        }
        g_hash_table_add(ids, task->id);
        if (task->status == CHRONO_TASK_ACTIVE && ++active_count > 1) {
            valid = FALSE;
            break;
        }
    }
    g_hash_table_unref(ids);
    return valid;
}

GQuark
chrono_task_error_quark(void)
{
    return g_quark_from_static_string("chronoledger-task-error");
}

static char *
normalize_title(const char *title)
{
    if (title == NULL || !g_utf8_validate(title, -1, NULL))
        return NULL;

    char *normalized = g_strdup(title);
    g_strstrip(normalized);
    if (*normalized == '\0' ||
        g_utf8_strlen(normalized, -1) > CHRONO_TASK_TITLE_MAX_CHARS) {
        g_free(normalized);
        return NULL;
    }
    return normalized;
}

ChronoStore *
chrono_store_new(void)
{
    ChronoStore *store = g_new0(ChronoStore, 1);
    store->tasks = g_ptr_array_new_with_free_func((GDestroyNotify)chrono_task_free);
    store->completed_tasks = g_ptr_array_new();
    store->completed_order_dirty = TRUE;
    return store;
}

ChronoStore *
chrono_store_copy(const ChronoStore *store)
{
    g_return_val_if_fail(store != NULL, NULL);

    if (!store_tasks_are_consistent(store))
        return NULL;

    ChronoStore *copy = chrono_store_new();
    for (guint i = 0; i < store->tasks->len; ++i) {
        ChronoTask *task_copy = chrono_task_copy(
            g_ptr_array_index(store->tasks, i));
        if (task_copy == NULL) {
            chrono_store_free(copy);
            return NULL;
        }
        /* The source store was validated once above, so direct append avoids
         * revalidating an ever-growing prefix for every copied task. */
        g_ptr_array_add(copy->tasks, task_copy);
    }
    return copy;
}

void
chrono_store_free(ChronoStore *store)
{
    if (store == NULL)
        return;
    g_clear_pointer(&store->completed_tasks, g_ptr_array_unref);
    g_clear_pointer(&store->tasks, g_ptr_array_unref);
    g_free(store);
}

guint
chrono_store_get_task_count(const ChronoStore *store)
{
    return store_has_task_array(store) ? store->tasks->len : 0;
}

ChronoTask *
chrono_store_get_task_at(ChronoStore *store, guint index)
{
    if (!store_has_task_array(store) || index >= store->tasks->len)
        return NULL;
    return g_ptr_array_index(store->tasks, index);
}

const ChronoTask *
chrono_store_get_task_at_const(const ChronoStore *store, guint index)
{
    if (!store_has_task_array(store) || index >= store->tasks->len)
        return NULL;
    return g_ptr_array_index(store->tasks, index);
}

gboolean
chrono_task_is_valid(const ChronoTask *task)
{
    return task_identity_is_valid(task) &&
           task_allocation_is_valid(task) &&
           task->created_at_unix >= 0 &&
           task->completed_at_unix >= 0 &&
           (task->completed_at_unix == 0 ||
            task->completed_at_unix >= task->created_at_unix);
}

static gint
compare_completions(gconstpointer left, gconstpointer right)
{
    const ChronoTask *a = *(const ChronoTask *const *)left;
    const ChronoTask *b = *(const ChronoTask *const *)right;
    if (a->completed_at_unix != b->completed_at_unix)
        return a->completed_at_unix > b->completed_at_unix ? -1 : 1;
    if (a->created_at_unix != b->created_at_unix)
        return a->created_at_unix > b->created_at_unix ? -1 : 1;
    return g_strcmp0(a->id, b->id);
}

const ChronoTask *
chrono_store_get_completed_task_at_const(const ChronoStore *store, guint index)
{
    if (!store_has_task_array(store))
        return NULL;
    if (store->completed_order_dirty) {
        /* Logical constness: rebuilding an index never changes task values.
         * Invalidate before freeing tasks; stale pointers are only cleared,
         * never dereferenced, when rebuilding this borrowed-pointer cache. */
        ChronoStore *cache = (ChronoStore *)store;
        g_ptr_array_set_size(cache->completed_tasks, 0);
        for (guint i = 0; i < store->tasks->len; ++i) {
            ChronoTask *task = g_ptr_array_index(store->tasks, i);
            if (task->status == CHRONO_TASK_DONE)
                g_ptr_array_add(cache->completed_tasks, task);
        }
        g_ptr_array_sort(cache->completed_tasks, compare_completions);
        cache->completed_order_dirty = FALSE;
    }
    return index < store->completed_tasks->len
        ? g_ptr_array_index(store->completed_tasks, index) : NULL;
}

ChronoTask *
chrono_store_get_completed_task_at(ChronoStore *store, guint index)
{
    return (ChronoTask *)chrono_store_get_completed_task_at_const(store, index);
}

gboolean
chrono_store_is_consistent(const ChronoStore *store)
{
    return store_tasks_are_consistent(store);
}

gboolean
chrono_store_adopt_loaded_task(ChronoStore *store, ChronoTask *task)
{
    if (!store_has_task_array(store) ||
        store->tasks->len >= CHRONO_STORE_MAX_TASKS ||
        !chrono_task_is_valid(task))
        return FALSE;
    g_ptr_array_add(store->tasks, task);
    store->completed_order_dirty = TRUE;
    return TRUE;
}

ChronoTask *
chrono_task_new(const char *title, gint64 estimate_seconds, gint64 created_at_unix)
{
    if (estimate_seconds <= 0 || created_at_unix < 0)
        return NULL;

    char *normalized = normalize_title(title);
    if (normalized == NULL)
        return NULL;

    ChronoTask *task = g_new0(ChronoTask, 1);
    task->id = g_uuid_string_random();
    task->title = normalized;
    task->estimate_seconds = estimate_seconds;
    task->allocated_seconds = estimate_seconds;
    task->remaining_seconds = estimate_seconds;
    task->status = CHRONO_TASK_READY;
    task->created_at_unix = created_at_unix;
    return task;
}

ChronoTask *
chrono_task_copy(const ChronoTask *task)
{
    if (!task_identity_is_valid(task) || !task_allocation_is_valid(task) ||
        task->created_at_unix < 0 || task->completed_at_unix < 0 ||
        (task->completed_at_unix > 0 &&
         task->completed_at_unix < task->created_at_unix))
        return NULL;

    ChronoTask *copy = g_new0(ChronoTask, 1);
    *copy = *task;
    copy->id = g_strdup(task->id);
    copy->title = g_strdup(task->title);
    return copy;
}

void
chrono_task_free(ChronoTask *task)
{
    if (task == NULL)
        return;
    g_free(task->id);
    g_free(task->title);
    g_free(task);
}

const char *
chrono_task_get_id(const ChronoTask *task)
{
    return task != NULL ? task->id : NULL;
}

const char *
chrono_task_get_title(const ChronoTask *task)
{
    return task != NULL ? task->title : NULL;
}

gint64
chrono_task_get_estimate_seconds(const ChronoTask *task)
{
    return task != NULL ? task->estimate_seconds : 0;
}

gint64
chrono_task_get_allocated_seconds(const ChronoTask *task)
{
    return task != NULL ? task->allocated_seconds : 0;
}

gint64
chrono_task_get_elapsed_seconds(const ChronoTask *task)
{
    return task != NULL ? task->elapsed_seconds : 0;
}

gint64
chrono_task_get_remaining_seconds(const ChronoTask *task)
{
    return task != NULL ? task->remaining_seconds : 0;
}

ChronoTaskStatus
chrono_task_get_status(const ChronoTask *task)
{
    return task != NULL ? task->status : CHRONO_TASK_READY;
}

gint64
chrono_task_get_active_started_monotonic_us(const ChronoTask *task)
{
    return task != NULL ? task->active_started_monotonic_us : 0;
}

gint64
chrono_task_get_created_at_unix(const ChronoTask *task)
{
    return task != NULL ? task->created_at_unix : 0;
}

gint64
chrono_task_get_completed_at_unix(const ChronoTask *task)
{
    return task != NULL ? task->completed_at_unix : 0;
}

ChronoTask *
chrono_task_new_loaded(const char *id,
                       const char *title,
                       gint64 estimate_seconds,
                       gint64 allocated_seconds,
                       gint64 elapsed_seconds,
                       gint64 remaining_seconds,
                       ChronoTaskStatus status,
                       gint64 created_at_unix,
                       gint64 completed_at_unix,
                       gint64 elapsed_remainder_us)
{
    if (id == NULL || title == NULL)
        return NULL;

    char *normalized_title = normalize_title(title);
    if (normalized_title == NULL)
        return NULL;

    ChronoTask *task = g_new0(ChronoTask, 1);
    task->id = g_strdup(id);
    task->title = normalized_title;
    task->estimate_seconds = estimate_seconds;
    task->allocated_seconds = allocated_seconds;
    task->elapsed_seconds = elapsed_seconds;
    task->elapsed_remainder_us = elapsed_remainder_us;
    task->remaining_seconds = remaining_seconds;
    task->status = status;
    task->created_at_unix = created_at_unix;
    task->completed_at_unix = completed_at_unix;

    if (!chrono_task_is_valid(task)) {
        chrono_task_free(task);
        return NULL;
    }
    return task;
}

const char *
chrono_task_status_to_string(ChronoTaskStatus status)
{
    switch (status) {
    case CHRONO_TASK_READY: return "ready";
    case CHRONO_TASK_ACTIVE: return "active";
    case CHRONO_TASK_PAUSED: return "paused";
    case CHRONO_TASK_EXPIRED: return "expired";
    case CHRONO_TASK_DONE: return "done";
    }
    return "ready";
}

gboolean
chrono_task_status_from_string(const char *text, ChronoTaskStatus *out_status)
{
    g_return_val_if_fail(out_status != NULL, FALSE);

    if (g_strcmp0(text, "ready") == 0 || g_strcmp0(text, "todo") == 0)
        *out_status = CHRONO_TASK_READY;
    else if (g_strcmp0(text, "active") == 0)
        *out_status = CHRONO_TASK_ACTIVE;
    else if (g_strcmp0(text, "paused") == 0)
        *out_status = CHRONO_TASK_PAUSED;
    else if (g_strcmp0(text, "expired") == 0)
        *out_status = CHRONO_TASK_EXPIRED;
    else if (g_strcmp0(text, "done") == 0)
        *out_status = CHRONO_TASK_DONE;
    else
        return FALSE;
    return TRUE;
}

gboolean
chrono_store_add_task(ChronoStore *store, ChronoTask *task)
{
    if (!store_tasks_are_consistent(store) ||
        !task_identity_is_valid(task) ||
        !task_allocation_is_valid(task) ||
        task->created_at_unix < 0 || task->completed_at_unix < 0 ||
        (task->completed_at_unix > 0 &&
         task->completed_at_unix < task->created_at_unix) ||
        store->tasks->len >= CHRONO_STORE_MAX_TASKS)
        return FALSE;

    gboolean adding_active = task->status == CHRONO_TASK_ACTIVE;
    for (guint i = 0; i < store->tasks->len; ++i) {
        const ChronoTask *existing = g_ptr_array_index(store->tasks, i);
        if (g_strcmp0(existing->id, task->id) == 0 ||
            (adding_active && existing->status == CHRONO_TASK_ACTIVE))
            return FALSE;
    }

    g_ptr_array_add(store->tasks, task);
    store->completed_order_dirty = TRUE;
    return TRUE;
}

gboolean
chrono_store_remove_task(ChronoStore *store, const char *task_id)
{
    g_return_val_if_fail(store_has_task_array(store), FALSE);
    g_return_val_if_fail(task_id != NULL, FALSE);
    if (!store_tasks_are_consistent(store))
        return FALSE;

    for (guint i = 0; i < store->tasks->len; ++i) {
        ChronoTask *task = g_ptr_array_index(store->tasks, i);
        if (task != NULL && g_strcmp0(task->id, task_id) == 0) {
            store->completed_order_dirty = TRUE;
            g_ptr_array_remove_index(store->tasks, i);
            return TRUE;
        }
    }
    return FALSE;
}

guint
chrono_store_clear_completed(ChronoStore *store)
{
    g_return_val_if_fail(store_has_task_array(store), 0);
    if (!store_tasks_are_consistent(store))
        return 0;
    guint removed = 0;
    for (guint i = store->tasks->len; i > 0; --i) {
        ChronoTask *task = g_ptr_array_index(store->tasks, i - 1);
        if (task != NULL && task->status == CHRONO_TASK_DONE) {
            store->completed_order_dirty = TRUE;
            g_ptr_array_remove_index(store->tasks, i - 1);
            ++removed;
        }
    }
    return removed;
}

const ChronoTask *
chrono_store_find_task_const(const ChronoStore *store, const char *task_id)
{
    g_return_val_if_fail(store_has_task_array(store), NULL);
    g_return_val_if_fail(task_id != NULL, NULL);

    for (guint i = 0; i < store->tasks->len; ++i) {
        const ChronoTask *task = g_ptr_array_index(store->tasks, i);
        if (task != NULL && g_strcmp0(task->id, task_id) == 0 &&
            chrono_task_is_valid(task))
            return task;
    }
    return NULL;
}

ChronoTask *
chrono_store_find_task(ChronoStore *store, const char *task_id)
{
    return (ChronoTask *)chrono_store_find_task_const(store, task_id);
}

static const ChronoTask *
find_unique_active_task_const(const ChronoStore *store,
                              gboolean *consistent_out)
{
    if (consistent_out != NULL)
        *consistent_out = FALSE;
    if (!store_has_task_array(store) ||
        store->tasks->len > CHRONO_STORE_MAX_TASKS)
        return NULL;

    const ChronoTask *active = NULL;
    for (guint i = 0; i < store->tasks->len; ++i) {
        const ChronoTask *task = g_ptr_array_index(store->tasks, i);
        if (!chrono_task_is_valid(task))
            return NULL;
        if (task->status != CHRONO_TASK_ACTIVE)
            continue;
        if (active != NULL)
            return NULL;
        active = task;
    }
    if (consistent_out != NULL)
        *consistent_out = TRUE;
    return active;
}

static ChronoTask *
find_unique_active_task(ChronoStore *store, gboolean *consistent_out)
{
    return (ChronoTask *)find_unique_active_task_const(store, consistent_out);
}

const ChronoTask *
chrono_store_get_active_task_const(const ChronoStore *store)
{
    gboolean consistent = FALSE;
    const ChronoTask *active = find_unique_active_task_const(store, &consistent);
    return consistent ? active : NULL;
}

ChronoTask *
chrono_store_get_active_task(ChronoStore *store)
{
    return (ChronoTask *)chrono_store_get_active_task_const(store);
}

gboolean
chrono_store_has_active_task(const ChronoStore *store)
{
    if (!store_tasks_are_consistent(store))
        return FALSE;
    for (guint i = 0; i < store->tasks->len; ++i) {
        const ChronoTask *task = g_ptr_array_index(store->tasks, i);
        if (task->status == CHRONO_TASK_ACTIVE)
            return TRUE;
    }
    return FALSE;
}

const ChronoTask *
chrono_store_first_expired_task_const(const ChronoStore *store)
{
    g_return_val_if_fail(store_has_task_array(store), NULL);
    for (guint i = 0; i < store->tasks->len; ++i) {
        const ChronoTask *task = g_ptr_array_index(store->tasks, i);
        if (task != NULL && task->status == CHRONO_TASK_EXPIRED &&
            chrono_task_is_valid(task))
            return task;
    }
    return NULL;
}

ChronoTask *
chrono_store_first_expired_task(ChronoStore *store)
{
    return (ChronoTask *)chrono_store_first_expired_task_const(store);
}

typedef struct {
    gint64 seconds;
    gint64 remainder_us;
} TaskElapsed;

static TaskElapsed
effective_elapsed(const ChronoTask *task, gint64 now_monotonic_us)
{
    if (!task_allocation_is_valid(task))
        return (TaskElapsed){0};
    TaskElapsed elapsed = {task->elapsed_seconds, task->elapsed_remainder_us};
    if (task->status != CHRONO_TASK_ACTIVE ||
        now_monotonic_us <= task->active_started_monotonic_us)
        return elapsed;
    gint64 delta_us = now_monotonic_us - task->active_started_monotonic_us;
    /* Split before addition: neither large allocations nor monotonic deltas
     * need to be multiplied by a million or added to a near-limit integer. */
    gint64 fraction = elapsed.remainder_us + delta_us % G_USEC_PER_SEC;
    gint64 seconds = delta_us / G_USEC_PER_SEC + fraction / G_USEC_PER_SEC;
    if (seconds >= task->remaining_seconds)
        return (TaskElapsed){task->allocated_seconds, 0};
    elapsed.seconds += seconds;
    elapsed.remainder_us = fraction % G_USEC_PER_SEC;
    return elapsed;
}

gint64
chrono_task_effective_remaining(const ChronoTask *task, gint64 now_monotonic_us)
{
    g_return_val_if_fail(task != NULL, 0);
    if (!task_allocation_is_valid(task))
        return 0;
    return task->allocated_seconds - effective_elapsed(task, now_monotonic_us).seconds;
}

gint64
chrono_task_effective_elapsed(const ChronoTask *task, gint64 now_monotonic_us)
{
    g_return_val_if_fail(task != NULL, 0);
    if (!task_allocation_is_valid(task))
        return 0;
    return effective_elapsed(task, now_monotonic_us).seconds;
}

gint64
chrono_task_effective_elapsed_remainder_us(const ChronoTask *task,
                                          gint64 now_monotonic_us)
{
    return effective_elapsed(task, now_monotonic_us).remainder_us;
}

double
chrono_task_progress_fraction(const ChronoTask *task, gint64 now_monotonic_us)
{
    g_return_val_if_fail(task != NULL, 0.0);
    if (task->allocated_seconds <= 0)
        return 0.0;
    TaskElapsed elapsed = effective_elapsed(task, now_monotonic_us);
    double fraction = ((double)elapsed.seconds +
                       (double)elapsed.remainder_us / G_USEC_PER_SEC) /
                      (double)task->allocated_seconds;
    return CLAMP(fraction, 0.0, 1.0);
}

static void
commit_active_delta(ChronoTask *task, gint64 now_monotonic_us)
{
    TaskElapsed elapsed = effective_elapsed(task, now_monotonic_us);
    task->elapsed_seconds = elapsed.seconds;
    task->elapsed_remainder_us = elapsed.remainder_us;
    task->remaining_seconds = task->allocated_seconds - elapsed.seconds;
    task->active_started_monotonic_us = 0;
}

ChronoTask *
chrono_store_update_expired(ChronoStore *store, gint64 now_monotonic_us)
{
    g_return_val_if_fail(store_has_task_array(store), NULL);
    gboolean consistent = FALSE;
    ChronoTask *active = find_unique_active_task(store, &consistent);
    if (!consistent || active == NULL || !task_allocation_is_valid(active) ||
        active->active_started_monotonic_us <= 0 ||
        chrono_task_effective_remaining(active, now_monotonic_us) > 0)
        return NULL;

    /* Duplicate IDs require a hash-table pass, so defer that relatively
     * expensive check until an expiry would actually mutate the store. */
    if (!store_tasks_are_consistent(store))
        return NULL;

    commit_active_delta(active, now_monotonic_us);
    active->remaining_seconds = 0;
    active->elapsed_seconds = active->allocated_seconds;
    active->status = CHRONO_TASK_EXPIRED;
    return active;
}

static gboolean
pause_task_if_active(ChronoTask *task, gint64 now_monotonic_us)
{
    if (task == NULL || task->status != CHRONO_TASK_ACTIVE ||
        !task_allocation_is_valid(task) ||
        now_monotonic_us < task->active_started_monotonic_us)
        return FALSE;
    commit_active_delta(task, now_monotonic_us);
    task->status = task->remaining_seconds == 0
        ? CHRONO_TASK_EXPIRED
        : CHRONO_TASK_PAUSED;
    return TRUE;
}

gboolean
chrono_store_pause_active(ChronoStore *store, gint64 now_monotonic_us)
{
    g_return_val_if_fail(store_has_task_array(store), FALSE);
    if (!store_tasks_are_consistent(store))
        return FALSE;
    gboolean consistent = FALSE;
    ChronoTask *active = find_unique_active_task(store, &consistent);
    if (!consistent || active == NULL)
        return FALSE;
    return pause_task_if_active(active, now_monotonic_us);
}

gboolean
chrono_store_start_task(ChronoStore *store,
                        const char *task_id,
                        gint64 now_monotonic_us,
                        GError **error)
{
    g_return_val_if_fail(store_has_task_array(store), FALSE);
    g_return_val_if_fail(task_id != NULL, FALSE);

    if (now_monotonic_us <= 0) {
        g_set_error(error, CHRONO_TASK_ERROR, CHRONO_TASK_ERROR_INVALID_ARGUMENT,
                    "Monotonic timestamp must be positive");
        return FALSE;
    }
    if (!store_tasks_are_consistent(store)) {
        g_set_error(error, CHRONO_TASK_ERROR, CHRONO_TASK_ERROR_INVALID_STATE,
                    "The ledger contains inconsistent task data");
        return FALSE;
    }
    ChronoTask *task = chrono_store_find_task(store, task_id);
    if (task == NULL) {
        g_set_error(error, CHRONO_TASK_ERROR, CHRONO_TASK_ERROR_NOT_FOUND,
                    "Task not found");
        return FALSE;
    }
    if (!task_allocation_is_valid(task)) {
        g_set_error(error, CHRONO_TASK_ERROR, CHRONO_TASK_ERROR_INVALID_STATE,
                    "Task allocation data is inconsistent");
        return FALSE;
    }
    if (task->status == CHRONO_TASK_DONE) {
        g_set_error(error, CHRONO_TASK_ERROR, CHRONO_TASK_ERROR_INVALID_STATE,
                    "Completed entries cannot be restarted");
        return FALSE;
    }
    if (task->remaining_seconds <= 0 || task->status == CHRONO_TASK_EXPIRED) {
        g_set_error(error, CHRONO_TASK_ERROR, CHRONO_TASK_ERROR_INVALID_STATE,
                    "This entry has no remaining allocation");
        return FALSE;
    }
    gboolean active_state_consistent = FALSE;
    ChronoTask *active = find_unique_active_task(store,
                                                  &active_state_consistent);
    if (!active_state_consistent) {
        g_set_error(error, CHRONO_TASK_ERROR,
                    CHRONO_TASK_ERROR_INVALID_STATE,
                    "Multiple tasks are marked active");
        return FALSE;
    }
    if (active != NULL && !task_allocation_is_valid(active)) {
        g_set_error(error, CHRONO_TASK_ERROR,
                    CHRONO_TASK_ERROR_INVALID_STATE,
                    "The current active task contains inconsistent data");
        return FALSE;
    }
    if (task->status == CHRONO_TASK_ACTIVE) {
        if (active != task ||
            now_monotonic_us < task->active_started_monotonic_us) {
            g_set_error(error, CHRONO_TASK_ERROR,
                        CHRONO_TASK_ERROR_INVALID_ARGUMENT,
                        "Monotonic timestamp predates the active interval");
            return FALSE;
        }
        return TRUE;
    }

    if (active != NULL && active != task) {
        if (now_monotonic_us < active->active_started_monotonic_us) {
            g_set_error(error, CHRONO_TASK_ERROR,
                        CHRONO_TASK_ERROR_INVALID_ARGUMENT,
                        "Monotonic timestamp predates the active interval");
            return FALSE;
        }
        if (!pause_task_if_active(active, now_monotonic_us)) {
            g_set_error(error, CHRONO_TASK_ERROR,
                        CHRONO_TASK_ERROR_INVALID_STATE,
                        "The current active task contains inconsistent timing data");
            return FALSE;
        }
    }

    task->status = CHRONO_TASK_ACTIVE;
    task->active_started_monotonic_us = now_monotonic_us;
    return TRUE;
}

gboolean
chrono_store_complete_task(ChronoStore *store,
                           const char *task_id,
                           gint64 now_monotonic_us,
                           GError **error)
{
    g_return_val_if_fail(store_has_task_array(store), FALSE);
    g_return_val_if_fail(task_id != NULL, FALSE);

    if (now_monotonic_us <= 0) {
        g_set_error(error, CHRONO_TASK_ERROR, CHRONO_TASK_ERROR_INVALID_ARGUMENT,
                    "Monotonic timestamp must be positive");
        return FALSE;
    }
    if (!store_tasks_are_consistent(store)) {
        g_set_error(error, CHRONO_TASK_ERROR, CHRONO_TASK_ERROR_INVALID_STATE,
                    "The ledger contains inconsistent task data");
        return FALSE;
    }
    ChronoTask *task = chrono_store_find_task(store, task_id);
    if (task == NULL) {
        g_set_error(error, CHRONO_TASK_ERROR, CHRONO_TASK_ERROR_NOT_FOUND,
                    "Task not found");
        return FALSE;
    }
    if (!task_allocation_is_valid(task)) {
        g_set_error(error, CHRONO_TASK_ERROR, CHRONO_TASK_ERROR_INVALID_STATE,
                    "Task allocation data is inconsistent");
        return FALSE;
    }
    gboolean active_state_consistent = FALSE;
    ChronoTask *active = find_unique_active_task(store,
                                                  &active_state_consistent);
    if (!active_state_consistent ||
        (task->status == CHRONO_TASK_ACTIVE && active != task)) {
        g_set_error(error, CHRONO_TASK_ERROR,
                    CHRONO_TASK_ERROR_INVALID_STATE,
                    "The active task state is inconsistent");
        return FALSE;
    }
    if (task->status == CHRONO_TASK_DONE)
        return TRUE;
    if (task->status == CHRONO_TASK_ACTIVE) {
        if (now_monotonic_us < task->active_started_monotonic_us) {
            g_set_error(error, CHRONO_TASK_ERROR,
                        CHRONO_TASK_ERROR_INVALID_ARGUMENT,
                        "Monotonic timestamp predates the active interval");
            return FALSE;
        }
        commit_active_delta(task, now_monotonic_us);
    }

    task->status = CHRONO_TASK_DONE;
    store->completed_order_dirty = TRUE;
    task->active_started_monotonic_us = 0;
    task->completed_at_unix = MAX(
        MAX(g_get_real_time() / G_USEC_PER_SEC, 1),
        task->created_at_unix);
    return TRUE;
}

gboolean
chrono_store_add_time(ChronoStore *store,
                      const char *task_id,
                      gint64 seconds,
                      GError **error)
{
    g_return_val_if_fail(store_has_task_array(store), FALSE);
    g_return_val_if_fail(task_id != NULL, FALSE);

    if (seconds <= 0) {
        g_set_error(error, CHRONO_TASK_ERROR, CHRONO_TASK_ERROR_INVALID_ARGUMENT,
                    "Added time must be positive");
        return FALSE;
    }
    if (!store_tasks_are_consistent(store)) {
        g_set_error(error, CHRONO_TASK_ERROR, CHRONO_TASK_ERROR_INVALID_STATE,
                    "The ledger contains inconsistent task data");
        return FALSE;
    }
    ChronoTask *task = chrono_store_find_task(store, task_id);
    if (task == NULL) {
        g_set_error(error, CHRONO_TASK_ERROR, CHRONO_TASK_ERROR_NOT_FOUND,
                    "Task not found");
        return FALSE;
    }
    if (task->status == CHRONO_TASK_DONE) {
        g_set_error(error, CHRONO_TASK_ERROR, CHRONO_TASK_ERROR_INVALID_STATE,
                    "Completed entries cannot receive more time");
        return FALSE;
    }
    if (!task_allocation_is_valid(task)) {
        g_set_error(error, CHRONO_TASK_ERROR, CHRONO_TASK_ERROR_INVALID_STATE,
                    "Task allocation data is inconsistent");
        return FALSE;
    }
    if (seconds > G_MAXINT64 - task->remaining_seconds ||
        seconds > G_MAXINT64 - task->allocated_seconds) {
        g_set_error(error, CHRONO_TASK_ERROR, CHRONO_TASK_ERROR_INVALID_ARGUMENT,
                    "Added time is too large");
        return FALSE;
    }

    task->remaining_seconds += seconds;
    task->allocated_seconds += seconds;
    if (task->status == CHRONO_TASK_EXPIRED)
        task->status = CHRONO_TASK_PAUSED;
    return TRUE;
}

void
chrono_store_get_completed_stats(const ChronoStore *store, ChronoStats *out_stats)
{
    g_return_if_fail(store_has_task_array(store));
    g_return_if_fail(out_stats != NULL);
    *out_stats = (ChronoStats){0};
    gint64 remainder_us = 0;

    for (guint i = 0; i < store->tasks->len; ++i) {
        const ChronoTask *task = g_ptr_array_index(store->tasks, i);
        if (task == NULL || task->status != CHRONO_TASK_DONE)
            continue;
        if (!task_allocation_is_valid(task))
            continue;
        if (out_stats->completed_count < G_MAXUINT)
            ++out_stats->completed_count;
        out_stats->total_estimate_seconds =
            out_stats->total_estimate_seconds >
                    G_MAXINT64 - task->estimate_seconds
                ? G_MAXINT64
                : out_stats->total_estimate_seconds + task->estimate_seconds;
        remainder_us += task->elapsed_remainder_us;
        gint64 elapsed = task->elapsed_seconds + remainder_us / G_USEC_PER_SEC;
        remainder_us %= G_USEC_PER_SEC;
        out_stats->total_elapsed_seconds =
            out_stats->total_elapsed_seconds >
                    G_MAXINT64 - elapsed
                ? G_MAXINT64
                : out_stats->total_elapsed_seconds + elapsed;
    }
}
