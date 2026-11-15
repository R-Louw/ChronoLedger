#include <glib.h>
#include <glib/gstdio.h>

#include "chrono_model_private.h"
#include "chrono_persistence.h"

void register_timing_tests(void);

static ChronoTask *
new_task(ChronoStore *store, const char *title, gint64 seconds)
{
    ChronoTask *task = chrono_task_new(title, seconds, 1);
    g_assert_nonnull(task);
    g_assert_true(chrono_store_add_task(store, task));
    return task;
}

static void
test_fractional_intervals(void)
{
    ChronoStore *store = chrono_store_new();
    ChronoTask *task = new_task(store, "Fractional", 60);
    const char *id = chrono_task_get_id(task);
    for (guint i = 0; i < 10; ++i) {
        gint64 start = G_USEC_PER_SEC + i * 2 * G_USEC_PER_SEC;
        g_assert_true(chrono_store_start_task(store, id, start, NULL));
        g_assert_true(chrono_store_pause_active(store, start + 900000));
    }
    g_assert_cmpint(chrono_task_get_elapsed_seconds(task), ==, 9);
    g_assert_cmpint(chrono_task_get_remaining_seconds(task), ==, 51);
    g_assert_cmpint(chrono_task_effective_elapsed_remainder_us(task, 1), ==, 0);
    chrono_store_free(store);
}

static void
test_fractional_switch_and_stats(void)
{
    ChronoStore *store = chrono_store_new();
    ChronoTask *first = new_task(store, "First", 60);
    ChronoTask *second = new_task(store, "Second", 60);
    g_assert_true(chrono_store_start_task(store, chrono_task_get_id(first), 1000000, NULL));
    g_assert_true(chrono_store_start_task(store, chrono_task_get_id(second), 1600000, NULL));
    g_assert_cmpint(chrono_task_effective_elapsed_remainder_us(first, 1), ==, 600000);
    g_assert_true(chrono_store_complete_task(store, chrono_task_get_id(second), 2200000, NULL));
    g_assert_true(chrono_store_complete_task(store, chrono_task_get_id(first), 2200000, NULL));
    ChronoStats stats;
    chrono_store_get_completed_stats(store, &stats);
    g_assert_cmpuint(stats.completed_count, ==, 2);
    g_assert_cmpint(stats.total_elapsed_seconds, ==, 1);
    chrono_store_free(store);
}

static void
test_fractional_round_trip(void)
{
    ChronoDocument *document = chrono_document_new();
    ChronoStore *store = chrono_document_get_store(document);
    ChronoTask *task = new_task(store, "Saved fraction", 2);
    g_assert_true(chrono_store_start_task(store, chrono_task_get_id(task), 1000000, NULL));
    g_assert_true(chrono_store_pause_active(store, 1900000));
    ChronoStore *copy = chrono_store_copy(store);
    g_assert_cmpint(chrono_task_effective_elapsed_remainder_us(
        chrono_store_get_task_at_const(copy, 0), 1), ==, 900000);
    chrono_store_free(copy);
    g_assert_true(chrono_store_start_task(store, chrono_task_get_id(task), 2000000, NULL));

    GError *error = NULL;
    char *directory = g_dir_make_tmp("chronoledger-fractions-XXXXXX", &error);
    g_assert_no_error(error);
    char *path = g_build_filename(directory, "ledger.json", NULL);
    g_assert_true(chrono_persistence_save(document, path, 2050000, &error));
    g_assert_no_error(error);
    g_assert_cmpint(chrono_task_effective_elapsed_remainder_us(task, 2000000), ==, 900000);
    chrono_document_free(document);

    document = chrono_persistence_load(path, &error);
    g_assert_no_error(error);
    g_assert_nonnull(document);
    store = chrono_document_get_store(document);
    task = chrono_store_get_task_at(store, 0);
    g_assert_cmpint(chrono_task_get_status(task), ==, CHRONO_TASK_PAUSED);
    g_assert_cmpint(chrono_task_effective_elapsed_remainder_us(task, 1), ==, 950000);
    g_assert_true(chrono_store_start_task(store, chrono_task_get_id(task), 3000000, NULL));
    g_assert_cmpint(chrono_task_effective_elapsed(task, 3050000), ==, 1);
    g_assert_null(chrono_store_update_expired(store, 4049999));
    g_assert_true(chrono_store_update_expired(store, 4050000) == task);
    g_assert_cmpint(chrono_task_get_elapsed_seconds(task), ==, 2);
    g_assert_cmpint(chrono_task_effective_elapsed_remainder_us(task, 4050000), ==, 0);
    chrono_document_free(document);
    g_assert_cmpint(g_remove(path), ==, 0);
    g_assert_cmpint(g_rmdir(directory), ==, 0);
    g_free(path);
    g_free(directory);
}

static void
test_fractional_limits(void)
{
    g_assert_null(chrono_task_new_loaded("x", "Invalid", 1, 1, 0, 1,
        CHRONO_TASK_PAUSED, 1, 0, -1));
    g_assert_null(chrono_task_new_loaded("x", "Invalid", 1, 1, 0, 1,
        CHRONO_TASK_PAUSED, 1, 0, G_USEC_PER_SEC));
    g_assert_null(chrono_task_new_loaded("x", "Invalid", 1, 1, 1, 0,
        CHRONO_TASK_EXPIRED, 1, 0, 1));
    ChronoStore *store = chrono_store_new();
    ChronoTask *task = chrono_task_new_loaded("limit", "Limit", G_MAXINT64,
        G_MAXINT64, G_MAXINT64 - 1, 1, CHRONO_TASK_PAUSED, 1, 0, 999999);
    g_assert_nonnull(task);
    g_assert_true(chrono_store_add_task(store, task));
    g_assert_true(chrono_store_start_task(store, "limit", 1, NULL));
    g_assert_true(chrono_store_update_expired(store, 2) == task);
    g_assert_cmpint(chrono_task_get_elapsed_seconds(task), ==, G_MAXINT64);
    chrono_store_free(store);

    store = chrono_store_new();
    task = new_task(store, "Large interval", G_MAXINT64);
    g_assert_true(chrono_store_start_task(store, chrono_task_get_id(task), 1, NULL));
    g_assert_true(chrono_store_pause_active(store, G_MAXINT64));
    g_assert_cmpint(chrono_task_get_elapsed_seconds(task), ==, (G_MAXINT64 - 1) / G_USEC_PER_SEC);
    g_assert_cmpint(chrono_task_effective_elapsed_remainder_us(task, 1), ==,
                    (G_MAXINT64 - 1) % G_USEC_PER_SEC);
    chrono_store_free(store);
}

void
register_timing_tests(void)
{
    g_test_add_func("/model/fractional-intervals", test_fractional_intervals);
    g_test_add_func("/model/fractional-switch-and-stats", test_fractional_switch_and_stats);
    g_test_add_func("/model/fractional-round-trip", test_fractional_round_trip);
    g_test_add_func("/model/fractional-limits", test_fractional_limits);
}
