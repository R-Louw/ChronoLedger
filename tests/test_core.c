#include <glib.h>
#include <glib/gstdio.h>

#include <math.h>
#include <string.h>

#include "app_view.h"
#include "chrono_document.h"
#include "chrono_model.h"
#include "chrono_persistence.h"
#include "power_policy.h"
#include "render_policy.h"
#include "renderer_shaders.h"
#include "shader_profile.h"
#include "time_parse.h"
#include "ui_layout.h"

void register_persistence_tests(void);
void register_timing_tests(void);
void register_ordering_tests(void);

static ChronoTask *
add_task(ChronoStore *store, const char *title, gint64 estimate_seconds)
{
    ChronoTask *task = chrono_task_new(title, estimate_seconds, 100);
    g_assert_nonnull(task);
    g_assert_true(chrono_store_add_task(store, task));
    return task;
}

static void
test_duplicate_add_is_atomic(void)
{
    ChronoStore *store = chrono_store_new();
    ChronoTask *owned = add_task(store, "Primary", 60);
    ChronoTask *duplicate = chrono_task_copy(owned);
    g_assert_nonnull(duplicate);

    g_assert_false(chrono_store_add_task(store, duplicate));
    g_assert_cmpuint(chrono_store_get_task_count(store), ==, 1);

    ChronoStore *copy = chrono_store_copy(store);
    g_assert_nonnull(copy);
    chrono_store_free(copy);

    chrono_task_free(duplicate);
    chrono_store_free(store);
}

static void
test_starting_second_task_pauses_first(void)
{
    ChronoStore *store = chrono_store_new();
    ChronoTask *first = add_task(store, "First", 60);
    ChronoTask *second = add_task(store, "Second", 60);
    char *first_id = g_strdup(chrono_task_get_id(first));
    char *second_id = g_strdup(chrono_task_get_id(second));
    const gint64 start_us = 10 * G_USEC_PER_SEC;
    GError *error = NULL;

    g_assert_true(chrono_store_start_task(store, first_id, start_us, &error));
    g_assert_no_error(error);
    g_assert_true(chrono_store_start_task(store, second_id,
                                          start_us + 2 * G_USEC_PER_SEC,
                                          &error));
    g_assert_no_error(error);

    g_assert_cmpint(chrono_task_get_status(first), ==, CHRONO_TASK_PAUSED);
    g_assert_cmpint(chrono_task_get_elapsed_seconds(first), ==, 2);
    g_assert_cmpint(chrono_task_get_remaining_seconds(first), ==, 58);
    g_assert_cmpint(chrono_task_get_status(second), ==, CHRONO_TASK_ACTIVE);
    g_assert_true(chrono_store_get_active_task(store) == second);

    g_free(first_id);
    g_free(second_id);
    chrono_store_free(store);
}

static void
test_failed_task_switch_is_atomic(void)
{
    ChronoStore *store = chrono_store_new();
    ChronoTask *first = add_task(store, "First", 60);
    ChronoTask *second = add_task(store, "Second", 60);
    char *first_id = g_strdup(chrono_task_get_id(first));
    char *second_id = g_strdup(chrono_task_get_id(second));
    const gint64 start_us = 10 * G_USEC_PER_SEC;
    GError *error = NULL;

    g_assert_true(chrono_store_start_task(store, first_id, start_us, &error));
    g_assert_no_error(error);
    g_assert_false(chrono_store_start_task(store, second_id,
                                           start_us - G_USEC_PER_SEC,
                                           &error));
    g_assert_error(error, CHRONO_TASK_ERROR,
                   CHRONO_TASK_ERROR_INVALID_ARGUMENT);
    g_clear_error(&error);

    g_assert_true(chrono_store_get_active_task(store) == first);
    g_assert_cmpint(chrono_task_get_status(first), ==, CHRONO_TASK_ACTIVE);
    g_assert_cmpint(chrono_task_get_elapsed_seconds(first), ==, 0);
    g_assert_cmpint(chrono_task_get_remaining_seconds(first), ==, 60);
    g_assert_cmpint(chrono_task_get_status(second), ==, CHRONO_TASK_READY);

    g_free(first_id);
    g_free(second_id);
    chrono_store_free(store);
}

static void
test_add_time_overflow_is_atomic(void)
{
    ChronoStore *store = chrono_store_new();
    ChronoTask *task = add_task(store, "Bounded", G_MAXINT64);
    char *task_id = g_strdup(chrono_task_get_id(task));
    GError *error = NULL;

    g_assert_false(chrono_store_add_time(store, task_id, 1, &error));
    g_assert_error(error, CHRONO_TASK_ERROR,
                   CHRONO_TASK_ERROR_INVALID_ARGUMENT);
    g_clear_error(&error);
    g_assert_cmpint(chrono_task_get_allocated_seconds(task), ==, G_MAXINT64);
    g_assert_cmpint(chrono_task_get_remaining_seconds(task), ==, G_MAXINT64);
    g_assert_true(chrono_store_get_task_at(store, 0) == task);

    g_free(task_id);
    chrono_store_free(store);
}

static void
test_document_exchange_preserves_preferences(void)
{
    ChronoDocument *document = chrono_document_new();
    g_assert_nonnull(document);
    ChronoPreferences *preferences = chrono_document_get_preferences(document);
    preferences->notifications_enabled = FALSE;
    preferences->sound_enabled = TRUE;

    ChronoStore *replacement = chrono_store_new();
    add_task(replacement, "Replacement", 120);
    ChronoStore *previous = chrono_document_exchange_store(document, replacement);

    g_assert_nonnull(previous);
    g_assert_true(chrono_document_get_store(document) == replacement);
    preferences = chrono_document_get_preferences(document);
    g_assert_false(preferences->notifications_enabled);
    g_assert_true(preferences->sound_enabled);

    chrono_store_free(previous);
    chrono_document_free(document);
}

static char *
make_temp_path(char **directory_out)
{
    GError *error = NULL;
    char *directory = g_dir_make_tmp("chronoledger-tests-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(directory);
    *directory_out = directory;
    return g_build_filename(directory, "nested", "ledger.json", NULL);
}

static void
remove_temp_path(const char *path, const char *directory)
{
    char *parent = g_path_get_dirname(path);
    g_assert_cmpint(g_remove(path), ==, 0);
    g_assert_cmpint(g_rmdir(parent), ==, 0);
    g_assert_cmpint(g_rmdir(directory), ==, 0);
    g_free(parent);
}

static void
test_active_save_restores_paused(void)
{
    char *directory = NULL;
    char *path = make_temp_path(&directory);
    ChronoDocument *document = chrono_document_new();
    ChronoStore *store = chrono_document_get_store(document);
    ChronoTask *task = add_task(store, "Persisted", 90);
    char *task_id = g_strdup(chrono_task_get_id(task));
    ChronoPreferences *preferences = chrono_document_get_preferences(document);
    preferences->notifications_enabled = FALSE;
    preferences->sound_enabled = TRUE;

    const gint64 start_us = 20 * G_USEC_PER_SEC;
    GError *error = NULL;
    g_assert_true(chrono_store_start_task(store, task_id, start_us, &error));
    g_assert_no_error(error);
    g_assert_true(chrono_persistence_save(document, path,
                                          start_us + 5 * G_USEC_PER_SEC,
                                          &error));
    g_assert_no_error(error);
    chrono_document_free(document);

    ChronoDocument *loaded = chrono_persistence_load(path, &error);
    g_assert_no_error(error);
    g_assert_nonnull(loaded);
    const ChronoStore *loaded_store = chrono_document_get_store_const(loaded);
    const ChronoTask *loaded_task = chrono_store_find_task_const(loaded_store, task_id);
    g_assert_nonnull(loaded_task);
    g_assert_cmpint(chrono_task_get_status(loaded_task), ==, CHRONO_TASK_PAUSED);
    g_assert_cmpint(chrono_task_get_elapsed_seconds(loaded_task), ==, 5);
    g_assert_cmpint(chrono_task_get_remaining_seconds(loaded_task), ==, 85);

    const ChronoPreferences *loaded_preferences =
        chrono_document_get_preferences_const(loaded);
    g_assert_false(loaded_preferences->notifications_enabled);
    g_assert_true(loaded_preferences->sound_enabled);

    chrono_document_free(loaded);
    g_free(task_id);
    remove_temp_path(path, directory);
    g_free(path);
    g_free(directory);
}

static void
test_duplicate_ids_are_rejected(void)
{
    static const char json[] =
        "{\n"
        "  \"schema_version\": 6,\n"
        "  \"tasks\": [\n"
        "    {\"id\":\"same\",\"title\":\"One\",\"estimate_seconds\":60,"
        "\"allocated_seconds\":60,\"elapsed_seconds\":0,\"remaining_seconds\":60,"
        "\"status\":\"ready\",\"created_at_unix\":1,\"completed_at_unix\":0},\n"
        "    {\"id\":\"same\",\"title\":\"Two\",\"estimate_seconds\":60,"
        "\"allocated_seconds\":60,\"elapsed_seconds\":0,\"remaining_seconds\":60,"
        "\"status\":\"ready\",\"created_at_unix\":1,\"completed_at_unix\":0}\n"
        "  ]\n"
        "}\n";

    GError *error = NULL;
    char *directory = g_dir_make_tmp("chronoledger-tests-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(directory);
    char *path = g_build_filename(directory, "duplicate.json", NULL);
    g_assert_true(g_file_set_contents(path, json, -1, &error));
    g_assert_no_error(error);

    ChronoDocument *document = chrono_persistence_load(path, &error);
    g_assert_null(document);
    g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL);
    g_clear_error(&error);

    g_assert_cmpint(g_remove(path), ==, 0);
    g_assert_cmpint(g_rmdir(directory), ==, 0);
    g_free(path);
    g_free(directory);
}

static void
test_import_preserves_local_preferences(void)
{
    char *directory = NULL;
    char *path = make_temp_path(&directory);
    ChronoDocument *exported = chrono_document_new();
    add_task(chrono_document_get_store(exported), "Imported", 120);
    ChronoPreferences *exported_preferences =
        chrono_document_get_preferences(exported);
    exported_preferences->notifications_enabled = FALSE;
    exported_preferences->sound_enabled = FALSE;
    exported_preferences->phosphor_mask_enabled = FALSE;
    exported_preferences->curvature_enabled = FALSE;
    exported_preferences->cursor_shader_effects_enabled = FALSE;

    GError *error = NULL;
    g_assert_true(chrono_persistence_save(exported, path, G_USEC_PER_SEC,
                                          &error));
    g_assert_no_error(error);
    chrono_document_free(exported);

    GFile *file = g_file_new_for_path(path);
    ChronoStore *imported = chrono_persistence_import_file(file, &error);
    g_object_unref(file);
    g_assert_no_error(error);
    g_assert_nonnull(imported);

    ChronoDocument *local = chrono_document_new();
    ChronoPreferences *local_preferences =
        chrono_document_get_preferences(local);
    local_preferences->notifications_enabled = TRUE;
    local_preferences->sound_enabled = TRUE;
    local_preferences->phosphor_mask_enabled = TRUE;
    local_preferences->curvature_enabled = TRUE;
    local_preferences->cursor_shader_effects_enabled = TRUE;
    ChronoStore *previous = chrono_document_exchange_store(local, imported);
    g_assert_nonnull(previous);
    chrono_store_free(previous);

    local_preferences = chrono_document_get_preferences(local);
    g_assert_true(local_preferences->notifications_enabled);
    g_assert_true(local_preferences->sound_enabled);
    g_assert_true(local_preferences->phosphor_mask_enabled);
    g_assert_true(local_preferences->curvature_enabled);
    g_assert_true(local_preferences->cursor_shader_effects_enabled);
    g_assert_cmpuint(chrono_store_get_task_count(
                         chrono_document_get_store_const(local)), ==, 1);

    chrono_document_free(local);
    remove_temp_path(path, directory);
    g_free(path);
    g_free(directory);
}

static void
test_duration_parse_and_format_boundaries(void)
{
    gint64 seconds = -1;
    g_assert_true(chrono_parse_duration_seconds(" 1h 30 min ", &seconds));
    g_assert_cmpint(seconds, ==, 5400);
    g_assert_true(chrono_parse_duration_seconds("15", &seconds));
    g_assert_cmpint(seconds, ==, 900);

    seconds = 123;
    g_assert_false(chrono_parse_duration_seconds("1h 30", &seconds));
    g_assert_cmpint(seconds, ==, 123);
    g_assert_false(chrono_parse_duration_seconds("0m", &seconds));
    g_assert_false(chrono_parse_duration_seconds(
        "9223372036854775807h", &seconds));
    g_assert_false(chrono_parse_duration_seconds("10 minutes trailing",
                                                  &seconds));

    char *formatted = chrono_format_duration(3661);
    g_assert_cmpstr(formatted, ==, "1:01:01");
    g_free(formatted);
    formatted = chrono_format_compact_duration(61);
    g_assert_cmpstr(formatted, ==, "1m 01s");
    g_free(formatted);
}

static void
test_power_policy_modes(void)
{
    ChronoPowerInputs inputs = {
        .renderer_ready = FALSE,
        .window_mapped = TRUE,
        .window_active = TRUE,
        .timer_active = TRUE,
    };
    g_assert_cmpint(chrono_power_choose_mode(inputs), ==, CHRONO_POWER_SLEEP);

    inputs.renderer_ready = TRUE;
    g_assert_cmpint(chrono_power_choose_mode(inputs), ==,
                    CHRONO_POWER_INTERACTIVE);
    inputs.window_suspended = TRUE;
    g_assert_cmpint(chrono_power_choose_mode(inputs), ==,
                    CHRONO_POWER_BACKGROUND_CLOCK);
    inputs.timer_active = FALSE;
    g_assert_cmpint(chrono_power_choose_mode(inputs), ==, CHRONO_POWER_SLEEP);
    inputs.window_suspended = FALSE;
    inputs.window_mapped = FALSE;
    g_assert_cmpint(chrono_power_choose_mode(inputs), ==, CHRONO_POWER_SLEEP);
}

static void
test_render_policy_boundaries(void)
{
    g_assert_cmpint(chrono_progress_visual_step(NAN), ==, 0);
    g_assert_cmpint(chrono_progress_visual_step(-1.0), ==, 0);
    g_assert_cmpint(chrono_progress_visual_step(0.5), ==,
                    CHRONO_PROGRESS_VISUAL_STEPS / 2);
    g_assert_cmpint(chrono_progress_visual_step(1.5), ==,
                    CHRONO_PROGRESS_VISUAL_STEPS);

    g_assert_true(chrono_progress_update_due(-1, 0, 0, 1));
    g_assert_false(chrono_progress_update_due(2, 100, 2, 200));
    g_assert_false(chrono_progress_update_due(
        2, 100, 3, 100 + CHRONO_PROGRESS_MIN_UPDATE_US - 1));
    g_assert_true(chrono_progress_update_due(
        2, 100, 3, 100 + CHRONO_PROGRESS_MIN_UPDATE_US));
    g_assert_true(chrono_progress_update_due(
        CHRONO_PROGRESS_VISUAL_STEPS - 1, 100,
        CHRONO_PROGRESS_VISUAL_STEPS, 101));

    g_assert_false(chrono_render_request_needed(FALSE, FALSE, FALSE, FALSE));
    g_assert_true(chrono_render_request_needed(FALSE, TRUE, FALSE, FALSE));
    g_assert_true(chrono_render_request_needed(TRUE, FALSE, FALSE, FALSE));
}

static void
test_scene_history_retires(void)
{
    const gint64 start = 1000000;
    gint64 deadline = 0;
    g_assert_cmpint(chrono_scene_history_step(&deadline, 0, start), ==,
                    CHRONO_SCENE_HISTORY_IDLE);
    g_assert_cmpint(chrono_scene_history_deadline(0.0f, start), ==, 0);
    g_assert_cmpint(chrono_scene_history_deadline(NAN, start), ==, 0);
    g_assert_cmpint(chrono_scene_history_deadline(1.0f, 0), ==, 0);

    const float amounts[] = {0.24f, 1.0f};
    for (guint i = 0; i < G_N_ELEMENTS(amounts); ++i) {
        deadline = chrono_scene_history_deadline(amounts[i], start);
        gint64 original_deadline = deadline;
        g_assert_cmpint(deadline, >, start);
        g_assert_cmpint(deadline - start, <=, 6450001);
        /* Native/cursor-only ticks must not recharge the scene tail. */
        g_assert_cmpint(chrono_scene_history_step(&deadline, start, start + 10000), ==,
                        CHRONO_SCENE_HISTORY_IDLE);
        g_assert_cmpint(chrono_scene_history_step(&deadline, start,
                        start + CHRONO_PROGRESS_MIN_UPDATE_US), ==,
                        CHRONO_SCENE_HISTORY_DECAY);
        g_assert_cmpint(chrono_scene_history_step(&deadline, start, deadline - 1), ==,
                        CHRONO_SCENE_HISTORY_DECAY);
        g_assert_cmpint(deadline, ==, original_deadline);
        /* Cleanup is due even when the previous CRT update was very recent. */
        g_assert_cmpint(chrono_scene_history_step(&deadline, original_deadline - 1,
                                                 original_deadline), ==,
                        CHRONO_SCENE_HISTORY_CLEANUP);
        g_assert_cmpint(deadline, ==, 0);
        g_assert_cmpint(chrono_scene_history_step(&deadline, original_deadline,
                                                 original_deadline + 1), ==,
                        CHRONO_SCENE_HISTORY_IDLE);
    }
    deadline = chrono_scene_history_deadline(1.0f, G_MAXINT64 - 10);
    g_assert_cmpint(deadline, ==, G_MAXINT64);
    g_assert_cmpint(chrono_scene_history_step(&deadline, G_MAXINT64 - 1, G_MAXINT64), ==,
                    CHRONO_SCENE_HISTORY_CLEANUP);
}

static void
test_scene_decay_uses_elapsed_time(void)
{
    const float amounts[] = {0.24f, 1.0f};
    for (guint i = 0; i < G_N_ELEMENTS(amounts); ++i) {
        float short_decay = chrono_scene_persistence_decay(amounts[i], 0.1);
        float long_decay = chrono_scene_persistence_decay(amounts[i], 2.0);
        g_assert_cmpfloat(long_decay, <, short_decay);
        /* Decay must compose across time partitions, including idle gaps. */
        g_assert_cmpfloat_with_epsilon(long_decay, pow(short_decay, 20), 0.000001);
        double tail_seconds = (double)(chrono_scene_history_deadline(amounts[i], 1) - 1) /
                              G_USEC_PER_SEC;
        g_assert_cmpfloat(chrono_scene_persistence_decay(amounts[i], tail_seconds),
                          <, 1.0f / 255.0f);
    }
    g_assert_cmpfloat(chrono_scene_persistence_decay(1.0f, 0.0), ==, 1.0f);
    g_assert_cmpfloat(chrono_scene_persistence_decay(1.0f, -1.0), ==, 1.0f);
    g_assert_cmpfloat(chrono_scene_persistence_decay(1.0f, INFINITY), ==, 0.0f);
}

static void
test_layout_density_boundaries(void)
{
    UiLayout layout = {.density = UI_DENSITY_TOO_SMALL};
    g_assert_false(ui_layout_compute(&layout,
                                     CHRONO_MINIMAL_WIDTH - 1,
                                     CHRONO_MINIMAL_HEIGHT - 1));
    g_assert_cmpint(layout.density, ==, UI_DENSITY_TOO_SMALL);

    g_assert_true(ui_layout_compute(&layout,
                                    CHRONO_MINIMAL_WIDTH,
                                    CHRONO_MINIMAL_HEIGHT));
    g_assert_cmpint(layout.density, ==, UI_DENSITY_MINIMAL);
    g_assert_false(layout.show_add_form);
    g_assert_false(layout.show_ledger_chart);
    g_assert_cmpfloat(layout.focus_chamber.width, >, 0.0);
    g_assert_false(ui_layout_compute(&layout,
                                     CHRONO_MINIMAL_WIDTH,
                                     CHRONO_MINIMAL_HEIGHT));

    g_assert_true(ui_layout_compute(&layout,
                                    CHRONO_COMPACT_WIDTH,
                                    CHRONO_COMPACT_HEIGHT));
    g_assert_cmpint(layout.density, ==, UI_DENSITY_COMPACT);
    g_assert_cmpuint(layout.visible_queue_rows, <=,
                     CHRONO_VISIBLE_QUEUE_ROWS);
    g_assert_cmpuint(layout.visible_ledger_rows, <=,
                     CHRONO_VISIBLE_LEDGER_ROWS);

    g_assert_true(ui_layout_compute(&layout,
                                    CHRONO_FULL_WIDTH,
                                    CHRONO_FULL_HEIGHT));
    g_assert_cmpint(layout.density, ==, UI_DENSITY_FULL);
    g_assert_true(layout.show_add_form);
    g_assert_true(layout.show_ledger_chart);
    g_assert_true(layout.show_advanced_calibration);
}

static void
test_shader_profile_transition(void)
{
    AppPresentationState presentation = {0};
    shader_settings_reset(&presentation.settings);
    shader_profile_begin(&presentation, PRESENTATION_OFF);
    g_assert_true(presentation.profile_transition_active);

    ShaderProfileStep step = shader_profile_advance(&presentation, 0.13);
    g_assert_true(step.settings_changed);
    g_assert_true(step.anti_aliasing_changed);
    g_assert_false(step.transition_completed);
    g_assert_cmpint(presentation.settings.aa_mode, ==, AA_OFF);

    step = shader_profile_advance(&presentation, 0.13);
    g_assert_true(step.transition_completed);
    g_assert_false(presentation.profile_transition_active);
    g_assert_cmpfloat(presentation.settings.glow, ==, 0.0f);
    g_assert_cmpfloat(presentation.settings.cursor_persistence, ==, 0.0f);
}

static void
test_view_snapshot_is_coherent(void)
{
    ChronoDocument *document = chrono_document_new();
    ChronoStore *store = chrono_document_get_store(document);
    ChronoTask *completed = add_task(store, "Completed", 30);
    ChronoTask *active = add_task(store, "Active", 60);
    char *completed_id = g_strdup(chrono_task_get_id(completed));
    char *active_id = g_strdup(chrono_task_get_id(active));
    GError *error = NULL;

    g_assert_true(chrono_store_complete_task(store, completed_id,
                                             5 * G_USEC_PER_SEC, &error));
    g_assert_no_error(error);
    g_assert_true(chrono_store_start_task(store, active_id,
                                          10 * G_USEC_PER_SEC, &error));
    g_assert_no_error(error);
    chrono_document_get_preferences(document)->notifications_enabled = FALSE;

    AppSessionState session = {.document = document};
    AppViewSnapshot snapshot;
    g_assert_true(app_view_snapshot_build(&snapshot, &session,
                                           13 * G_USEC_PER_SEC));
    g_assert_true(snapshot.has_active_task);
    g_assert_true(snapshot.has_focus_task);
    g_assert_cmpuint(snapshot.open_task_count, ==, 1);
    g_assert_cmpuint(snapshot.completed_task_count, ==, 1);
    g_assert_cmpuint(snapshot.queue_row_count, ==, 1);
    g_assert_cmpuint(snapshot.ledger_row_count, ==, 1);
    g_assert_true(snapshot.active_task.id == chrono_task_get_id(active));
    g_assert_true(snapshot.focus_task.id == snapshot.active_task.id);
    g_assert_cmpint(snapshot.active_task.effective_elapsed_seconds, ==, 3);
    g_assert_cmpstr(snapshot.ledger_rows[0].id, ==, completed_id);
    g_assert_cmpuint(snapshot.completed_stats.completed_count, ==, 1);
    g_assert_false(snapshot.preferences.notifications_enabled);

    chrono_document_get_preferences(document)->notifications_enabled = TRUE;
    g_assert_false(snapshot.preferences.notifications_enabled);

    g_free(completed_id);
    g_free(active_id);
    chrono_document_free(document);
}

static void
test_shader_resources_load(void)
{
    for (guint i = RENDERER_SHADER_FULLSCREEN_VERTEX;
         i <= RENDERER_SHADER_PERSISTENCE_FRAGMENT;
         ++i) {
        GError *error = NULL;
        char *source = renderer_shader_load_source((RendererShaderId)i,
                                                   FALSE, &error);
        g_assert_no_error(error);
        g_assert_nonnull(source);
        g_assert_true(g_str_has_prefix(source, "#version 130\n"));
        g_assert_nonnull(strstr(source, "void main"));
        g_free(source);

        source = renderer_shader_load_source((RendererShaderId)i,
                                             TRUE, &error);
        g_assert_no_error(error);
        g_assert_nonnull(source);
        g_assert_true(g_str_has_prefix(source, "#version 300 es\n"));
        g_assert_nonnull(strstr(source, "void main"));
        g_free(source);
    }

    GError *error = NULL;
    char *source = renderer_shader_load_source(
        (RendererShaderId)(RENDERER_SHADER_PERSISTENCE_FRAGMENT + 1),
        FALSE, &error);
    g_assert_null(source);
    g_assert_nonnull(error);
    g_clear_error(&error);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    register_persistence_tests();
    register_timing_tests();
    register_ordering_tests();
    g_test_add_func("/model/duplicate-add-is-atomic", test_duplicate_add_is_atomic);
    g_test_add_func("/model/switch-active-pauses-previous",
                    test_starting_second_task_pauses_first);
    g_test_add_func("/model/failed-switch-is-atomic",
                    test_failed_task_switch_is_atomic);
    g_test_add_func("/model/add-time-overflow-is-atomic",
                    test_add_time_overflow_is_atomic);
    g_test_add_func("/document/exchange-preserves-preferences",
                    test_document_exchange_preserves_preferences);
    g_test_add_func("/persistence/active-save-restores-paused",
                    test_active_save_restores_paused);
    g_test_add_func("/persistence/duplicate-ids-rejected",
                    test_duplicate_ids_are_rejected);
    g_test_add_func("/persistence/import-preserves-local-preferences",
                    test_import_preserves_local_preferences);
    g_test_add_func("/time/parse-and-format-boundaries",
                    test_duration_parse_and_format_boundaries);
    g_test_add_func("/policy/power-modes", test_power_policy_modes);
    g_test_add_func("/policy/render-boundaries", test_render_policy_boundaries);
    g_test_add_func("/policy/scene-history-retires", test_scene_history_retires);
    g_test_add_func("/policy/scene-decay-elapsed-time", test_scene_decay_uses_elapsed_time);
    g_test_add_func("/layout/density-boundaries", test_layout_density_boundaries);
    g_test_add_func("/presentation/profile-transition",
                    test_shader_profile_transition);
    g_test_add_func("/view/snapshot-coherence", test_view_snapshot_is_coherent);
    g_test_add_func("/renderer/shader-resources", test_shader_resources_load);
    return g_test_run();
}
