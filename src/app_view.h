#pragma once

#include "app_state.h"

G_BEGIN_DECLS

typedef struct _AppViewSnapshot AppViewSnapshot;

#define APP_VIEW_LEDGER_CHART_MAX_TASKS 18u

typedef struct {
    const char *id;
    const char *title;
    gint64 estimate_seconds;
    gint64 allocated_seconds;
    gint64 elapsed_seconds;
    gint64 remaining_seconds;
    gint64 effective_elapsed_seconds;
    gint64 effective_remaining_seconds;
    ChronoTaskStatus status;
    gint64 created_at_unix;
    gint64 completed_at_unix;
    double progress_fraction;
} AppTaskView;

typedef struct {
    gint64 estimate_seconds;
    gint64 elapsed_seconds;
} AppLedgerChartEntry;

struct _AppViewSnapshot {
    gint64 monotonic_us;
    ChronoPreferences preferences;

    gboolean has_active_task;
    gboolean has_expired_task;
    gboolean has_focus_task;
    AppTaskView active_task;
    AppTaskView expired_task;
    AppTaskView focus_task;

    guint open_task_count;
    guint completed_task_count;
    guint queue_row_count;
    AppTaskView queue_rows[CHRONO_VISIBLE_QUEUE_ROWS];
    guint ledger_row_count;
    AppTaskView ledger_rows[CHRONO_VISIBLE_LEDGER_ROWS];

    ChronoStats completed_stats;
    guint ledger_chart_count;
    AppLedgerChartEntry ledger_chart[APP_VIEW_LEDGER_CHART_MAX_TASKS];
};

/* Captures the task and preference values required by one synchronous render
 * dispatch. String pointers are borrowed from the opaque model and remain
 * valid while the owning document is unchanged on the GTK main thread. */
gboolean app_view_snapshot_build(AppViewSnapshot *snapshot,
                                  const AppSessionState *session,
                                  gint64 monotonic_us);

G_END_DECLS
