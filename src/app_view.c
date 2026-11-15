#include "app_view.h"

#include <string.h>

static void
task_view_capture(AppTaskView *view,
                  const ChronoTask *task,
                  gint64 monotonic_us)
{
    g_return_if_fail(view != NULL);
    g_return_if_fail(task != NULL);

    *view = (AppTaskView){
        .id = chrono_task_get_id(task),
        .title = chrono_task_get_title(task),
        .estimate_seconds = chrono_task_get_estimate_seconds(task),
        .allocated_seconds = chrono_task_get_allocated_seconds(task),
        .elapsed_seconds = chrono_task_get_elapsed_seconds(task),
        .remaining_seconds = chrono_task_get_remaining_seconds(task),
        .effective_elapsed_seconds =
            chrono_task_effective_elapsed(task, monotonic_us),
        .effective_remaining_seconds =
            chrono_task_effective_remaining(task, monotonic_us),
        .status = chrono_task_get_status(task),
        .created_at_unix = chrono_task_get_created_at_unix(task),
        .completed_at_unix = chrono_task_get_completed_at_unix(task),
        .progress_fraction =
            chrono_task_progress_fraction(task, monotonic_us),
    };
}

static void
capture_focus_tasks(AppViewSnapshot *snapshot,
                    const AppSessionState *session,
                    const ChronoStore *store)
{
    const ChronoTask *active = chrono_store_get_active_task_const(store);
    const ChronoTask *expired = chrono_store_first_expired_task_const(store);
    const ChronoTask *focus = active != NULL ? active : expired;
    if (focus == NULL && session->selected_task_id != NULL)
        focus = chrono_store_find_task_const(store, session->selected_task_id);

    if (active != NULL) {
        task_view_capture(&snapshot->active_task, active,
                          snapshot->monotonic_us);
        snapshot->has_active_task = TRUE;
    }
    if (expired != NULL) {
        task_view_capture(&snapshot->expired_task, expired,
                          snapshot->monotonic_us);
        snapshot->has_expired_task = TRUE;
    }
    if (focus != NULL) {
        task_view_capture(&snapshot->focus_task, focus,
                          snapshot->monotonic_us);
        snapshot->has_focus_task = TRUE;
    }
}

static void
capture_task_collections(AppViewSnapshot *snapshot,
                         const AppSessionState *session,
                         const ChronoStore *store)
{
    guint open_seen = 0;
    guint completed_seen = 0;
    guint task_count = chrono_store_get_task_count(store);

    for (guint i = 0; i < task_count; ++i) {
        const ChronoTask *task = chrono_store_get_task_at_const(store, i);
        if (task == NULL)
            continue;
        if (chrono_task_get_status(task) == CHRONO_TASK_DONE) {
            if (completed_seen < G_MAXUINT)
                ++completed_seen;
            continue;
        }
        if (open_seen >= session->queue_offset &&
            snapshot->queue_row_count < CHRONO_VISIBLE_QUEUE_ROWS) {
            task_view_capture(
                &snapshot->queue_rows[snapshot->queue_row_count++],
                task, snapshot->monotonic_us);
        }
        if (open_seen < G_MAXUINT)
            ++open_seen;
    }
    snapshot->open_task_count = open_seen;
    snapshot->completed_task_count = completed_seen;

    for (guint i = session->ledger_offset; i < completed_seen &&
         snapshot->ledger_row_count < CHRONO_VISIBLE_LEDGER_ROWS; ++i) {
        const ChronoTask *task = chrono_store_get_completed_task_at_const(store, i);
        task_view_capture(&snapshot->ledger_rows[snapshot->ledger_row_count++],
                          task, snapshot->monotonic_us);
    }

    guint chart_count = MIN(completed_seen, APP_VIEW_LEDGER_CHART_MAX_TASKS);
    for (guint i = chart_count; i > 0; --i) {
        const ChronoTask *task = chrono_store_get_completed_task_at_const(store, i - 1);
        guint chart_index = snapshot->ledger_chart_count++;
        snapshot->ledger_chart[chart_index] = (AppLedgerChartEntry){
            .estimate_seconds = chrono_task_get_estimate_seconds(task),
            .elapsed_seconds = chrono_task_get_elapsed_seconds(task),
        };
    }
}

gboolean
app_view_snapshot_build(AppViewSnapshot *snapshot,
                        const AppSessionState *session,
                        gint64 monotonic_us)
{
    g_return_val_if_fail(snapshot != NULL, FALSE);
    memset(snapshot, 0, sizeof(*snapshot));
    if (session == NULL || session->document == NULL || monotonic_us <= 0)
        return FALSE;

    const ChronoStore *store = chrono_document_get_store_const(session->document);
    const ChronoPreferences *preferences =
        chrono_document_get_preferences_const(session->document);
    if (store == NULL || preferences == NULL)
        return FALSE;

    snapshot->monotonic_us = monotonic_us;
    chrono_preferences_copy(&snapshot->preferences, preferences);
    capture_focus_tasks(snapshot, session, store);
    capture_task_collections(snapshot, session, store);
    chrono_store_get_completed_stats(store, &snapshot->completed_stats);
    return TRUE;
}
