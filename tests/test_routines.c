#include <glib.h>
#include <glib/gstdio.h>
#include "chrono_document.h"
#include "chrono_persistence.h"

static gint64 local_time(int year, int month, int day, int hour, int minute)
{
    GDateTime *date = g_date_time_new_local(year, month, day, hour, minute, 0);
    g_assert_nonnull(date);
    gint64 result = g_date_time_to_unix(date);
    g_date_time_unref(date);
    return result;
}

static ChronoRoutine scheduled(gint64 due)
{
    return (ChronoRoutine){.title = "Instrument check", .enabled = TRUE,
        .scheduled = TRUE, .clock_minute = 9 * 60, .window_minutes = 10, .due = due};
}

static void test_confirmation_lifecycle(void)
{
    gint64 trigger = local_time(2026, 11, 16, 9, 0);
    ChronoRoutine r = scheduled(trigger);
    g_assert_false(chrono_routine_confirm(&r, trigger - 1));
    g_assert_false(chrono_routine_maintain(&r, trigger - 1));
    g_assert_true(chrono_routine_maintain(&r, trigger));
    g_assert_true(chrono_routine_due(&r, trigger));
    g_assert_cmpint(r.deadline, ==, trigger + 600);
    g_assert_false(chrono_routine_maintain(&r, trigger + 599));
    g_assert_false(r.overdue);
    g_assert_true(chrono_routine_maintain(&r, trigger + 600));
    g_assert_true(r.overdue);
    // Daily triggers and sleep cannot replace the outstanding original deadline.
    g_assert_false(chrono_routine_maintain(&r, trigger + 5 * 86400));
    g_assert_cmpint(r.due, ==, trigger);
    g_assert_cmpint(r.deadline, ==, trigger + 600);
    // Rollback does not hide a latched item or make it timely again.
    g_assert_false(chrono_routine_maintain(&r, trigger - 86400));
    g_assert_true(chrono_routine_due(&r, trigger - 86400));
    g_assert_true(r.overdue);
    r.enabled = FALSE;
    g_assert_true(chrono_routine_confirm(&r, trigger - 86400));
    g_assert_cmpint(r.deadline, ==, 0);
    g_assert_false(r.overdue);
    g_assert_cmpint(r.due, ==, local_time(2026, 11, 17, 9, 0));
    g_assert_false(chrono_routine_due(&r, trigger));
    r.enabled = TRUE;
    g_assert_true(chrono_routine_confirm(&r, local_time(2026, 11, 20, 15, 42)));
    g_assert_cmpint(r.due, ==, local_time(2026, 11, 21, 9, 0));
    // Late confirmation before today's clock time chooses today, with no drift.
    g_assert_true(chrono_routine_confirm(&r, local_time(2026, 11, 23, 8, 0)));
    g_assert_cmpint(r.due, ==, local_time(2026, 11, 23, 9, 0));
    g_assert_true(chrono_routine_confirm(&r, r.due));
    g_assert_cmpint(r.due, ==, local_time(2026, 11, 24, 9, 0));
    r.enabled = FALSE;
    g_assert_false(chrono_routine_maintain(&r, r.due + 86400));
    r.enabled = TRUE;
    g_assert_true(chrono_routine_maintain(&r, r.due + 86400));
    g_assert_true(r.overdue);
}

static void test_confirmation_persistence(void)
{
    GError *error = NULL;
    char *dir = g_dir_make_tmp("chrono-confirmation-XXXXXX", &error);
    g_assert_no_error(error);
    char *path = g_build_filename(dir, "ledger.json", NULL);
    ChronoDocument *document = chrono_document_new();
    ChronoPreferences *p = chrono_document_get_preferences(document);
    p->routine_count = 1;
    p->routines[0] = scheduled(local_time(2026, 11, 16, 9, 0));
    gint64 due = p->routines[0].due;
    for (int stage = 0; stage < 3; stage++) {
        if (stage > 0) chrono_routine_maintain(&p->routines[0], due + (stage == 1 ? 0 : 600));
        g_assert_true(chrono_persistence_save(document, path, g_get_monotonic_time(), &error));
        g_assert_no_error(error);
        ChronoDocument *loaded = chrono_persistence_load(path, &error);
        g_assert_no_error(error);
        ChronoRoutine *r = &chrono_document_get_preferences(loaded)->routines[0];
        g_assert_true(r->scheduled);
        g_assert_cmpuint(r->clock_minute, ==, 540);
        g_assert_cmpuint(r->window_minutes, ==, 10);
        g_assert_cmpint(r->due, ==, due);
        g_assert_cmpint(r->deadline, ==, stage == 0 ? 0 : due + 600);
        g_assert_cmpint(r->overdue, ==, stage == 2);
        // Restart reconciliation after several sleeping days keeps the old window.
        chrono_routine_maintain(r, due + 4 * 86400);
        g_assert_cmpint(r->deadline, ==, due + 600);
        g_assert_true(r->overdue);
        chrono_document_free(loaded);
    }
    chrono_document_free(document);
    g_remove(path); g_rmdir(dir); g_free(path); g_free(dir);
}

static JsonNode *local_state(ChronoPreferences *p)
{
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    chrono_preferences_write_local(builder, p);
    json_builder_end_object(builder);
    JsonNode *root = json_builder_get_root(builder);
    g_object_unref(builder);
    return root;
}

static void test_confirmation_validation_and_tabs(void)
{
    ChronoPreferences p, q;
    chrono_preferences_init_defaults(&p);
    p.routine_count = 1;
    p.routines[0] = scheduled(1700000000);
    JsonNode *root = local_state(&p);
    JsonObject *o = json_node_get_object(root);
    JsonObject *local = json_object_get_object_member(o, "local_state");
    // Legacy semantic tab IDs survive the new visual/keyboard order in both directions.
    for (int tab = 0; tab < 4; tab++) {
        json_object_set_int_member(local, "tab", tab);
        chrono_preferences_init_defaults(&q);
        g_assert_true(chrono_preferences_read_local(o, &q));
        g_assert_cmpint(q.selected_tab, ==, tab == 2 ? CHRONO_MODE_CALIBRATION :
            tab == 3 ? CHRONO_MODE_ROUTINES : tab);
        JsonNode *written = local_state(&q);
        JsonObject *saved = json_object_get_object_member(json_node_get_object(written), "local_state");
        g_assert_cmpint(json_object_get_int_member(saved, "tab"), ==, tab);
        json_node_free(written);
    }
    JsonObject *r = json_array_get_object_element(json_object_get_array_member(local, "routines"), 0);
    JsonObject *c = json_object_get_object_member(r, "confirmation");
    const char *keys[] = {"minute", "window", "deadline"};
    const gint64 bad[] = {1440, 0, 1700000601};
    for (guint i = 0; i < G_N_ELEMENTS(keys); i++) {
        gint64 previous = json_object_get_int_member(c, keys[i]);
        json_object_set_int_member(c, keys[i], bad[i]);
        g_assert_false(chrono_preferences_read_local(o, &q));
        json_object_set_int_member(c, keys[i], previous);
    }
    json_object_set_boolean_member(c, "overdue", TRUE);
    g_assert_false(chrono_preferences_read_local(o, &q));
    json_object_set_int_member(c, "deadline", 1700000600);
    g_assert_true(chrono_preferences_read_local(o, &q));
    json_object_set_string_member(c, "overdue", "true");
    g_assert_false(chrono_preferences_read_local(o, &q));
    // Older routines keep their interval/midnight behavior with absent optional fields.
    json_object_remove_member(r, "confirmation");
    g_assert_true(chrono_preferences_read_local(o, &q));
    g_assert_false(q.routines[0].scheduled);
    g_assert_cmpint(q.routines[0].deadline, ==, 0);
    json_node_free(root);
}

static void test_confirmation_calendar(void)
{
    char *previous = g_strdup(g_getenv("TZ"));
    g_setenv("TZ", "America/New_York", TRUE);
    // Fixed local time spans a 23-hour spring day and a 25-hour fall day.
    gint64 now = local_time(2026, 3, 7, 9, 0);
    gint64 next = chrono_routine_next_clock(540, now);
    g_assert_cmpint(next, ==, local_time(2026, 3, 8, 9, 0));
    g_assert_cmpint(next - now, ==, 23 * 3600);
    now = local_time(2026, 10, 31, 9, 0);
    next = chrono_routine_next_clock(540, now);
    g_assert_cmpint(next - now, ==, 25 * 3600);
    // GLib normalizes the missing 02:30 to 03:00, and selects standard 01:30.
    next = chrono_routine_next_clock(150, local_time(2026, 3, 8, 0, 0));
    g_assert_cmpint(next, ==, local_time(2026, 3, 8, 3, 0));
    next = chrono_routine_next_clock(90, local_time(2026, 11, 1, 0, 0));
    GDateTime *date = g_date_time_new_from_unix_local(next);
    g_assert_false(g_date_time_is_daylight_savings(date));
    g_date_time_unref(date);
    // Timezone changes preserve an already booked instant; the next uses the new zone.
    ChronoRoutine r = scheduled(next);
    g_setenv("TZ", "Asia/Tokyo", TRUE);
    chrono_routine_maintain(&r, next + 600);
    g_assert_cmpint(r.deadline, ==, next + 600);
    g_assert_true(chrono_routine_confirm(&r, next + 600));
    date = g_date_time_new_from_unix_local(r.due);
    g_assert_cmpint(g_date_time_get_hour(date), ==, 9);
    g_assert_cmpint(g_date_time_get_minute(date), ==, 0);
    g_date_time_unref(date);
    if (previous) g_setenv("TZ", previous, TRUE); else g_unsetenv("TZ");
    g_free(previous);
}

void register_routine_tests(void)
{
    g_test_add_func("/routines/confirmation-lifecycle", test_confirmation_lifecycle);
    g_test_add_func("/routines/confirmation-persistence", test_confirmation_persistence);
    g_test_add_func("/routines/confirmation-validation-tabs", test_confirmation_validation_and_tabs);
    g_test_add_func("/routines/confirmation-calendar", test_confirmation_calendar);
}
