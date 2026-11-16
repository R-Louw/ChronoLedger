#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include "cursor_state.h"
#include "app_routines.h"
#include <math.h>

static guint shown_alerts;

static void
capture_alert(GtkAlertDialog *dialog, GtkWindow *window)
{
    (void)dialog;
    (void)window;
    ++shown_alerts;
}

/* Include the controller translation units to exercise their private async
 * completions without exporting callback internals in the application API.
 * All other application modules are linked normally. */
#define gtk_alert_dialog_show capture_alert
#include "../src/app_dialogs.c"
#undef gtk_alert_dialog_show
#define main chronoledger_application_main
#include "../src/main.c"
#undef main

typedef struct {
    AppState *state;
    GObject *window;
    char *directory;
    char *primary;
    char *obstacle;
} Fixture;

static void
setup(Fixture *fixture, gconstpointer unused)
{
    (void)unused;
    GError *error = NULL;
    fixture->directory = g_dir_make_tmp("chronoledger-controller-XXXXXX", &error);
    g_assert_no_error(error);
    fixture->primary = g_build_filename(fixture->directory, "ledger.json", NULL);
    fixture->obstacle = g_build_filename(fixture->directory, "blocked", NULL);
    g_assert_true(g_file_set_contents(fixture->primary, "original broken ledger", -1, &error));
    g_assert_no_error(error);
    g_assert_true(g_file_set_contents(fixture->obstacle, "not a directory", -1, &error));
    g_assert_no_error(error);
    fixture->state = g_new0(AppState, 1);
    fixture->state->renderer = renderer_create();
    fixture->state->session.data_path = g_strdup(fixture->primary);
    char *startup_error = NULL;
    fixture->state->session.document = load_initial_document(fixture->state, &startup_error);
    g_assert_nonnull(startup_error);
    g_free(startup_error);
    g_assert_nonnull(fixture->state->session.document);
    g_assert_true(fixture->state->session.data_load_failed);
    fixture->window = g_object_new(G_TYPE_OBJECT, NULL);
    g_object_set_data(fixture->window, "chronoledger-state", fixture->state);
    shown_alerts = 0;
}

static void
teardown(Fixture *fixture, gconstpointer unused)
{
    (void)unused;
    g_object_set_data(fixture->window, "chronoledger-state", NULL);
    g_object_unref(fixture->window);
    app_state_free(fixture->state);
    g_assert_cmpint(g_remove(fixture->primary), ==, 0);
    g_assert_cmpint(g_remove(fixture->obstacle), ==, 0);
    g_assert_cmpint(g_rmdir(fixture->directory), ==, 0);
    g_free(fixture->primary);
    g_free(fixture->obstacle);
    g_free(fixture->directory);
}

static void
assert_primary_protected(const Fixture *fixture)
{
    GError *error = NULL;
    char *bytes = NULL;
    g_assert_true(g_file_get_contents(fixture->primary, &bytes, NULL, &error));
    g_assert_no_error(error);
    g_assert_cmpstr(bytes, ==, "original broken ledger");
    g_free(bytes);
}

static void
test_recovery_mutations(Fixture *fixture, gconstpointer unused)
{
    (void)unused;
    AppState *state = fixture->state;
    state->session.draft_title = g_strdup("Unsaved draft");
    state->session.draft_duration = g_strdup("1m");
    const AppActionType mutations[] = {
        APP_ACTION_BEGIN_NEW_TASK, APP_ACTION_BEGIN_EDIT, APP_ACTION_COMMIT_NEW_TASK,
        APP_ACTION_ACTIVATE_FOCUS_PRIMARY, APP_ACTION_TOGGLE_FOCUSED_TASK,
        APP_ACTION_COMPLETE_FOCUSED_TASK, APP_ACTION_TOGGLE_TASK,
        APP_ACTION_COMPLETE_TASK, APP_ACTION_ADD_TASK_TIME, APP_ACTION_DELETE_TASK,
        APP_ACTION_CLEAR_COMPLETED, APP_ACTION_TOGGLE_MASK, APP_ACTION_TOGGLE_CURVATURE,
        APP_ACTION_TOGGLE_CURSOR_SHADER_EFFECTS, APP_ACTION_TOGGLE_NOTIFICATIONS,
        APP_ACTION_TOGGLE_SOUND,
    };
    app_preferences(state)->routine_count = 1;
    app_preferences(state)->routines[0] = (ChronoRoutine){.title = "Keep outstanding",
        .enabled = TRUE, .scheduled = TRUE, .clock_minute = 540, .window_minutes = 10,
        .due = 1700000000, .deadline = 1700000600, .overdue = TRUE};
    ChronoPreferences preferences = *app_preferences(state);
    for (guint i = 0; i < G_N_ELEMENTS(mutations); ++i) {
        AppAction action = {.type = mutations[i]};
        g_assert_false(app_dispatch_action(state, &action));
    }
    g_assert_cmpmem(&preferences, sizeof(preferences), app_preferences(state), sizeof(preferences));
    g_assert_cmpuint(chrono_store_get_task_count(app_store(state)), ==, 0);
    g_assert_false(app_save_state(state));
    g_assert_false(commit_store_candidate(state, chrono_store_new()));
    g_assert_false(on_close_request(NULL, state));
    g_assert_true(state->shell.closing);
    assert_primary_protected(fixture);
}

static void
confirm_import(Fixture *fixture, ChronoStore *candidate)
{
    GtkAlertDialog *dialog = gtk_alert_dialog_new("Replace current ledger?");
    GTask *result = g_task_new(dialog, NULL, NULL, NULL);
    g_task_set_source_tag(result, G_GNUC_EXTENSION (gpointer)gtk_alert_dialog_choose);
    g_task_return_int(result, 1);
    ImportConfirmData *data = g_new0(ImportConfirmData, 1);
    data->window = (GtkWindow *)g_object_ref(fixture->window);
    data->imported_store = candidate;
    on_import_confirmed(G_OBJECT(dialog), G_ASYNC_RESULT(result), data);
    g_object_unref(result);
    g_object_unref(dialog);
}

static void
test_recovery_import(Fixture *fixture, gconstpointer unused)
{
    (void)unused;
    AppState *state = fixture->state;
    ChronoStore *previous = app_store(state);
    app_preferences(state)->sound_enabled = TRUE;
    app_preferences(state)->curvature_enabled = TRUE;
    app_preferences(state)->routine_count = 1;
    app_preferences(state)->routines[0] = (ChronoRoutine){.title = "Keep outstanding",
        .enabled = TRUE, .scheduled = TRUE, .clock_minute = 540, .window_minutes = 10,
        .due = 1700000000, .deadline = 1700000600, .overdue = TRUE};
    ChronoPreferences preferences = *app_preferences(state);
    g_free(state->session.data_path);
    state->session.data_path = g_build_filename(fixture->obstacle, "ledger.json", NULL);
    confirm_import(fixture, chrono_store_new());
    g_assert_true(state->session.data_load_failed);
    g_assert_true(app_store(state) == previous);
    g_assert_cmpint(state->session.save_state, ==, SAVE_STATE_FAILED);
    assert_primary_protected(fixture);

    g_free(state->session.data_path);
    state->session.data_path = g_strdup(fixture->primary);
    ChronoStore *candidate = chrono_store_new();
    g_assert_true(chrono_store_add_task(candidate, chrono_task_new("Replacement", 60, 1)));
    confirm_import(fixture, candidate);
    g_assert_false(state->session.data_load_failed);
    g_assert_cmpmem(&preferences, sizeof(preferences), app_preferences(state), sizeof(preferences));
    GError *error = NULL;
    ChronoDocument *saved = chrono_persistence_load(fixture->primary, &error);
    g_assert_no_error(error);
    g_assert_nonnull(saved);
    const ChronoTask *task = chrono_store_get_task_at_const(chrono_document_get_store_const(saved), 0);
    g_assert_cmpstr(chrono_task_get_title(task), ==, "Replacement");
    chrono_document_free(saved);
}

static void test_routine_confirmation_command(Fixture *fixture, gconstpointer unused)
{
    (void)unused;
    AppState *s = fixture->state;
    ChronoPreferences *p = app_preferences(s);
    p->routine_count = 1;
    gint64 now = g_get_real_time() / G_USEC_PER_SEC;
    p->routines[0] = (ChronoRoutine){.title = "Confirm", .enabled = TRUE,
        .scheduled = TRUE, .clock_minute = 540, .window_minutes = 1, .due = now - 120};
    ChronoRoutine original = p->routines[0];
    app_routine_command(s, 3);
    app_routine_command(s, 5);
    g_assert_cmpmem(&original, sizeof(original), &p->routines[0], sizeof(original));
    assert_primary_protected(fixture);
    s->session.data_load_failed = FALSE;
    // Disable at/after the trigger latches the original item before disabling.
    app_routine_command(s, 5);
    g_assert_false(p->routines[0].enabled);
    g_assert_true(p->routines[0].overdue);
    g_assert_cmpint(p->routines[0].deadline, ==, original.due + 60);
    app_routine_command(s, 3);
    g_assert_cmpint(p->routines[0].deadline, ==, 0);
    g_assert_false(p->routines[0].overdue);
    g_assert_cmpint(p->routines[0].due, >, now);
    g_assert_false(chrono_store_has_active_task(app_store(s)));
    g_assert_cmpuint(chrono_store_get_task_count(app_store(s)), ==, 0);
    ChronoDocument *saved = chrono_persistence_load(fixture->primary, NULL);
    g_assert_nonnull(saved);
    const ChronoRoutine *r = &chrono_document_get_preferences_const(saved)->routines[0];
    g_assert_cmpint(r->due, ==, p->routines[0].due);
    g_assert_cmpint(r->deadline, ==, 0);
    chrono_document_free(saved);
}

static void
test_dialog_cancellation(Fixture *fixture, gconstpointer unused)
{
    (void)unused;
    GtkFileDialog *dialog = gtk_file_dialog_new();
    const GQuark domains[] = {G_IO_ERROR, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR};
    const int codes[] = {G_IO_ERROR_CANCELLED, GTK_DIALOG_ERROR_CANCELLED,
                         GTK_DIALOG_ERROR_DISMISSED, GTK_DIALOG_ERROR_FAILED};
    for (guint i = 0; i < G_N_ELEMENTS(codes); ++i) {
        GTask *result = g_task_new(dialog, NULL, NULL, NULL);
        g_task_set_source_tag(result, G_GNUC_EXTENSION (gpointer)gtk_file_dialog_open);
        g_task_return_new_error(result, domains[i], codes[i], "Dialog result");
        on_import_file_selected(G_OBJECT(dialog), G_ASYNC_RESULT(result),
                                 g_object_ref(fixture->window));
        g_object_unref(result);
        result = g_task_new(dialog, NULL, NULL, NULL);
        g_task_set_source_tag(result, G_GNUC_EXTENSION (gpointer)gtk_file_dialog_save);
        g_task_return_new_error(result, domains[i], codes[i], "Dialog result");
        on_export_file_selected(G_OBJECT(dialog), G_ASYNC_RESULT(result),
                                 g_object_ref(fixture->window));
        g_object_unref(result);
        g_assert_cmpuint(shown_alerts, ==, i == 3 ? 2 : 0);
    }
    g_object_unref(dialog);
}

static void test_panel_result_adapter(void)
{
    GtkAlertDialog *dialog = gtk_alert_dialog_new("Confirm");
    for (int choice = 0; choice <= 1; ++choice) {
        GTask *task = g_task_new(dialog, NULL, NULL, NULL);
        g_task_set_source_tag(task, G_GNUC_EXTENSION (gpointer)app_modal_choose);
        g_task_return_int(task, choice);
        GError *error = NULL;
        g_assert_cmpint(app_modal_choose_finish(dialog, G_ASYNC_RESULT(task), &error), ==, choice);
        g_assert_no_error(error);
        g_object_unref(task);
    }
    g_object_unref(dialog);
}

static void test_curvature_mapping(void)
{
    AppSceneState scene = {.logical_width = 938, .logical_height = 1021};
    ChronoPreferences preferences;
    chrono_preferences_init_defaults(&preferences);
    ShaderSettings settings = preferences.settings;
    preferences.curvature_enabled = TRUE;
    for (int step = 0; step <= 10; ++step) {
        settings.curvature_strength = step / 10.0f;
        double k = settings.curvature_strength * CHRONO_CURVATURE_MAX;
        for (int ix = -9; ix <= 9; ix += 3) {
            for (int iy = -9; iy <= 9; iy += 3) {
                double sx = ix / 10.0, sy = iy / 10.0;
                // Solve the shader's source lookup for a visible control point.
                double px = sx, py = sy;
                for (int n = 0; n < 40; ++n) {
                    px = sx / (1 + k * pow(py, 4) * .13);
                    py = sy / (1 + k * pow(px, 4) * .17);
                }
                double x, y;
                cursor_state_map_pointer(&scene, &preferences, &settings,
                    (px * .5 + .5) * scene.logical_width,
                    (py * .5 + .5) * scene.logical_height, &x, &y);
                g_assert_cmpfloat_with_epsilon(x, (sx * .5 + .5) * scene.logical_width, .0001);
                g_assert_cmpfloat_with_epsilon(y, (sy * .5 + .5) * scene.logical_height, .0001);
            }
        }
    }
    Rect track = {43, 700, 850, 16};
    for (int ix = 50; ix < 890; ix += 20) {
        double value = cursor_state_curvature_drag_value(&scene, track, ix, 700);
        settings.curvature_strength = (float)value;
        double x, y;
        cursor_state_map_pointer(&scene, &preferences, &settings, ix, 700, &x, &y);
        g_assert_cmpfloat_with_epsilon(x, track.x + value * track.width, .0001);
    }
    preferences.curvature_enabled = FALSE;
    double x, y;
    cursor_state_map_pointer(&scene, &preferences, &settings, 42, 123, &x, &y);
    g_assert_cmpfloat(x, ==, 42); g_assert_cmpfloat(y, ==, 123);
}

static void test_manual_profile_override(Fixture *f, gconstpointer unused)
{
    (void)unused;
    AppState *s = f->state;
    shader_settings_reset(&s->presentation.settings);
    s->presentation.settings.palette = PALETTE_COPPER;
    shader_profile_begin(&s->presentation, PRESENTATION_AUTHENTIC);
    g_assert_cmpint(s->presentation.profile_target.palette, ==, PALETTE_COPPER);
    AppAction action = {.type = APP_ACTION_SET_SLIDER,
        .data.slider = {SLIDER_CURVATURE, .92}};
    g_assert_true(app_dispatch_action(s, &action));
    shader_profile_advance(&s->presentation, 1);
    g_assert_false(s->presentation.profile_transition_active);
    g_assert_cmpfloat_with_epsilon(s->presentation.settings.curvature_strength, .92, .0001);
    g_assert_cmpint(s->presentation.settings.palette, ==, PALETTE_COPPER);
}

static void test_cursor_afterglow(void)
{
    AppInputState input = {.pointer_inside = TRUE};
    ShaderSettings settings;
    shader_settings_reset(&settings);
    ChronoPreferences preferences;
    chrono_preferences_init_defaults(&preferences);
    Renderer *renderer = renderer_create();
    cursor_state_record_motion(&input, &settings, &preferences, renderer, 100, 150, 1000000);
    cursor_state_record_motion(&input, &settings, &preferences, renderer, 250, 150, 1016667);
    g_assert_cmpuint(input.cursor_sample_count, >, 100);
    guint oldest = (input.cursor_sample_head + CHRONO_CURSOR_SAMPLES - input.cursor_sample_count) % CHRONO_CURSOR_SAMPLES;
    for (guint i = 1; i < input.cursor_sample_count; ++i) {
        const CursorPhosphorPoint *a = &input.cursor_samples[(oldest + i - 1) % CHRONO_CURSOR_SAMPLES];
        const CursorPhosphorPoint *b = &input.cursor_samples[(oldest + i) % CHRONO_CURSOR_SAMPLES];
        g_assert_cmpfloat(hypot(b->x - a->x, b->y - a->y), <=, CHRONO_CURSOR_SAMPLE_SPACING + .00001);
        g_assert_cmpint(b->timestamp_us, >=, a->timestamp_us);
    }
    guint count = input.cursor_sample_count;
    gint64 last_time = input.cursor_samples[(input.cursor_sample_head + CHRONO_CURSOR_SAMPLES - 1) % CHRONO_CURSOR_SAMPLES].timestamp_us;
    for (int i = 1; i <= 20; ++i)
        cursor_state_update(&input, &settings, &preferences, renderer, 1016667 + i * 1000);
    g_assert_cmpuint(input.cursor_sample_count, ==, count);
    g_assert_cmpint(input.cursor_samples[(input.cursor_sample_head + CHRONO_CURSOR_SAMPLES - 1) % CHRONO_CURSOR_SAMPLES].timestamp_us, ==, last_time);
    float initial = cursor_state_emission(1, 0);
    g_assert_cmpfloat_with_epsilon(cursor_state_emission(1, 645000), initial / 2, .000001);
    g_assert_cmpfloat_with_epsilon(cursor_state_emission(1, 1290000), initial / 4, .000001);
    g_assert_cmpfloat(cursor_state_emission(1, 6450000), ==, 0);
    g_assert_cmpfloat(cursor_state_emission(0, 0), ==, 0);
    g_assert_cmpfloat(cursor_state_emission(NAN, 0), ==, 0);
    // A single update after a stall expires everything, without intermediate draws.
    cursor_state_update(&input, &settings, &preferences, renderer, 9000000);
    g_assert_cmpuint(input.cursor_sample_count, ==, 0);
    cursor_state_record_motion(&input, &settings, &preferences, renderer, 300, 150, 9000001);
    g_assert_cmpuint(input.cursor_sample_count, >, 0);
    preferences.cursor_shader_effects_enabled = FALSE;
    cursor_state_update(&input, &settings, &preferences, renderer, 9000002);
    g_assert_cmpuint(input.cursor_sample_count, ==, 0);
    g_assert_cmpint(input.cursor_anchor_us, ==, 0);
    preferences.cursor_shader_effects_enabled = TRUE;
    // Reversals and queue pressure remain bounded; cleanup discards all ownership.
    for (int i = 0; i < 300; ++i)
        cursor_state_record_motion(&input, &settings, &preferences, renderer,
            i % 2 ? 900 : 100, 200, 10000000 + i * 1000);
    g_assert_cmpuint(input.cursor_sample_count, ==, CHRONO_CURSOR_SAMPLES);
    cursor_state_clear(&input, renderer);
    g_assert_cmpuint(input.cursor_sample_count, ==, 0);
    renderer_destroy(renderer);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add("/controller/routine-confirmation", Fixture, NULL, setup, test_routine_confirmation_command, teardown);
    g_test_add_func("/controller/cursor-afterglow", test_cursor_afterglow);
    g_test_add("/controller/recovery-mutations", Fixture, NULL, setup, test_recovery_mutations, teardown);
    g_test_add("/controller/recovery-import", Fixture, NULL, setup, test_recovery_import, teardown);
    g_test_add("/controller/dialog-cancellation", Fixture, NULL, setup, test_dialog_cancellation, teardown);
    g_test_add_func("/controller/panel-result-adapter", test_panel_result_adapter);
    g_test_add_func("/controller/curvature-mapping", test_curvature_mapping);
    g_test_add("/controller/manual-profile-override", Fixture, NULL, setup, test_manual_profile_override, teardown);
    int result = g_test_run();
    while (g_main_context_pending(NULL))
        g_main_context_iteration(NULL, FALSE);
    return result;
}
