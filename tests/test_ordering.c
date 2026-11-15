#include <glib.h>

#include "app_view.h"
#include "chrono_model_private.h"

void register_ordering_tests(void);

static void
add_completed(ChronoStore *store, const char *id, gint64 created,
              gint64 completed, gint64 estimate)
{
    ChronoTask *task = chrono_task_new_loaded(id, id, estimate, estimate,
        1, estimate - 1, CHRONO_TASK_DONE, created, completed, 0);
    g_assert_nonnull(task);
    g_assert_true(chrono_store_adopt_loaded_task(store, task));
}

static void
test_order_cache_lifetime(void)
{
    ChronoStore *store = chrono_store_new();
    add_completed(store, "b", 1, 10, 60);
    add_completed(store, "a", 1, 10, 60);
    g_assert_cmpstr(chrono_task_get_id(chrono_store_get_completed_task_at_const(store, 0)), ==, "a");
    add_completed(store, "c", 2, 10, 60);
    g_assert_cmpstr(chrono_task_get_id(chrono_store_get_completed_task_at_const(store, 0)), ==, "c");
    g_assert_true(chrono_store_remove_task(store, "c"));
    g_assert_cmpstr(chrono_task_get_id(chrono_store_get_completed_task_at_const(store, 0)), ==, "a");
    ChronoTask *open = chrono_task_new("Completed last", 60, 1);
    g_assert_true(chrono_store_add_task(store, open));
    char *id = g_strdup(chrono_task_get_id(open));
    g_assert_true(chrono_store_complete_task(store, id, 1, NULL));
    g_assert_cmpstr(chrono_task_get_id(chrono_store_get_completed_task_at_const(store, 0)), ==, id);
    ChronoStore *copy = chrono_store_copy(store);
    g_assert_cmpstr(chrono_task_get_id(chrono_store_get_completed_task_at_const(copy, 0)), ==, id);
    g_assert_cmpuint(chrono_store_clear_completed(store), ==, 3);
    g_assert_null(chrono_store_get_completed_task_at_const(store, 0));
    chrono_store_free(store);
    g_assert_cmpstr(chrono_task_get_id(chrono_store_get_completed_task_at_const(copy, 0)), ==, id);
    chrono_store_free(copy);
    g_free(id);
}

static void
test_recent_snapshot_at_capacity(void)
{
    ChronoStore *store = chrono_store_new();
    for (guint i = 0; i < CHRONO_STORE_MAX_TASKS; ++i) {
        char *id = g_strdup_printf("task-%05u", i);
        add_completed(store, id, 1, CHRONO_STORE_MAX_TASKS + 1 - i, i + 1);
        g_free(id);
    }
    ChronoPreferences preferences;
    chrono_preferences_init_defaults(&preferences);
    ChronoDocument *document = chrono_document_new_take(store, &preferences);
    g_assert_nonnull(document);
    AppSessionState session = {.document = document, .ledger_offset = 7};
    AppViewSnapshot view;
    g_assert_true(app_view_snapshot_build(&view, &session, 1));
    g_assert_cmpuint(view.completed_task_count, ==, CHRONO_STORE_MAX_TASKS);
    g_assert_cmpuint(view.ledger_row_count, ==, CHRONO_VISIBLE_LEDGER_ROWS);
    for (guint i = 0; i < view.ledger_row_count; ++i) {
        const ChronoTask *clicked = chrono_store_get_completed_task_at_const(store, 7 + i);
        g_assert_cmpstr(view.ledger_rows[i].id, ==, chrono_task_get_id(clicked));
        g_assert_cmpint(view.ledger_rows[i].estimate_seconds, ==, 8 + i);
    }
    g_assert_cmpuint(view.ledger_chart_count, ==, APP_VIEW_LEDGER_CHART_MAX_TASKS);
    for (guint i = 0; i < view.ledger_chart_count; ++i)
        g_assert_cmpint(view.ledger_chart[i].estimate_seconds, ==,
                        APP_VIEW_LEDGER_CHART_MAX_TASKS - i);
    session.ledger_offset = CHRONO_STORE_MAX_TASKS - 1;
    g_assert_true(app_view_snapshot_build(&view, &session, 2));
    g_assert_cmpuint(view.ledger_row_count, ==, 1);
    g_assert_cmpint(view.ledger_rows[0].estimate_seconds, ==, CHRONO_STORE_MAX_TASKS);
    chrono_document_free(document);
}

void
register_ordering_tests(void)
{
    g_test_add_func("/model/completed-order-cache-lifetime", test_order_cache_lifetime);
    g_test_add_func("/view/recent-snapshot-at-capacity", test_recent_snapshot_at_capacity);
}
