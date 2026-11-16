/* Development-only presentation comparison. Includes the real startup policy
 * and controller; no timing hooks are added to the production application. */
#define _POSIX_C_SOURCE 200809L
#define main chronoledger_unused_main
#include "../../src/main.c"
#undef main
#include "../../src/app_input.c"
#include <stdio.h>
#include <time.h>

static AppState *study;
static GMainLoop *loop;
static FILE *output;
static char *task_id;
static guint phase, motion_source, heartbeat_source;
static guint64 draws, uploads, paints;
static gint64 start_us, motion_start_us, previous_draw_us, last_motion_us;
static double start_cpu;
static int expected_width, expected_height;
static guint violations;
static gboolean invalid;
static GArray *intervals, *input_ages;
static const struct { const char *name; guint seconds; gboolean active; } phases[] = {
    {"startup-warmup", 4, TRUE},
    {"timer-warmup", 4, TRUE},
    {"timer", 30, TRUE},
    {"motion-warmup", 3, TRUE},
    {"motion", 30, TRUE},
    {"idle-settle", 2, TRUE},
    {"idle", 5, TRUE},
    {"hidden-timer", 4, FALSE},
    {"hidden-idle", 3, FALSE},
};
static double process_seconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &now);
    return now.tv_sec + now.tv_nsec / 1e9;
}
static gint compare_i64(gconstpointer a, gconstpointer b)
{
    gint64 x = *(const gint64 *)a, y = *(const gint64 *)b;
    return (x > y) - (x < y);
}
static double percentile(GArray *values, double fraction)
{
    if (values->len == 0) return 0;
    g_array_sort(values, compare_i64);
    guint index = (guint)((values->len - 1) * fraction);
    return g_array_index(values, gint64, index) / 1000.0;
}
RendererDrawStatus __real_renderer_draw(Renderer *, const RendererFrameInput *,
                                       RendererFrameResult *, unsigned int *);
RendererDrawStatus __wrap_renderer_draw(Renderer *r, const RendererFrameInput *f,
                                       RendererFrameResult *result, unsigned int *error)
{
    RendererDrawStatus status = __real_renderer_draw(r, f, result, error);
    g_assert_cmpint(status, ==, RENDERER_DRAW_OK);
    if (study == NULL) return status;
    draws++;
    uploads += f->scene_needs_upload;
    gint64 now = g_get_monotonic_time();
    if (phase == 4) {
        if (previous_draw_us > 0) {
            gint64 interval = now - previous_draw_us;
            g_array_append_val(intervals, interval);
        }
        previous_draw_us = now;
        gint64 age = now - last_motion_us;
        /* Event age is not physical input-to-photon latency. */
        if (age >= 0 && age < G_USEC_PER_SEC)
            g_array_append_val(input_ages, age);
    }
    if (phases[phase].active &&
        (f->scene->surface_width_px != expected_width ||
         f->scene->surface_height_px != expected_height)) violations++;
    return status;
}
static void after_paint(GdkFrameClock *clock, gpointer unused)
{
    (void)clock; (void)unused;
    paints++;
}
static gboolean motion(gpointer unused)
{
    (void)unused;
    double elapsed = (g_get_monotonic_time() - motion_start_us) / 1e6;
    double x = study->scene.logical_width * .5 +
        MIN(250.0, study->scene.logical_width * .3) * sin(elapsed * 1.5);
    last_motion_us = g_get_monotonic_time();
    on_motion(NULL, x, study->scene.logical_height * .55 + 40 * sin(elapsed * 2.1), study);
    return G_SOURCE_CONTINUE;
}
/* A new process has no interactive activation token. Ask Hyprland once for
 * this test-owned title, as the cursor lifecycle harness does; never fight
 * subsequent owner input or change compositor configuration. */
static gboolean focus_initial_window(gpointer unused)
{
    (void)unused;
    if (g_getenv("HYPRLAND_INSTANCE_SIGNATURE") != NULL) {
        char *argv[] = {"hyprctl", "eval",
            "hl.dispatch(hl.dsp.focus({window = \"title:^(ChronoLedger presentation benchmark)$\"}))", NULL};
        GError *error = NULL;
        gint status = 0;
        g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
                     NULL, NULL, &status, &error);
        g_assert_no_error(error);
        g_assert_true(g_spawn_check_wait_status(status, &error));
        g_assert_no_error(error);
    }
    return G_SOURCE_REMOVE;
}
static gboolean heartbeat(gpointer unused)
{
    (void)unused;
    if (phases[phase].active && !gtk_window_is_active(study->shell.window))
        violations++;
    return G_SOURCE_CONTINUE;
}
static void report(void)
{
    double seconds = (g_get_monotonic_time() - start_us) / 1e6;
    fprintf(output, "%s,%.6f,%.6f,%" G_GUINT64_FORMAT ",%" G_GUINT64_FORMAT
        ",%" G_GUINT64_FORMAT ",%d,%d,%u,%.3f,%.3f,%.3f,%.3f,%d\n",
        phases[phase].name, seconds, (process_seconds() - start_cpu) * 1000,
        draws, uploads, paints, expected_width, expected_height, violations,
        percentile(intervals, .5), percentile(intervals, .95),
        percentile(input_ages, .5), percentile(input_ages, .95),
        study->runtime.power_mode);
    fflush(output);
}
static void close_study(void)
{
    if (motion_source != 0) {
        g_source_remove(motion_source);
        motion_source = 0;
    }
    if (heartbeat_source != 0) {
        g_source_remove(heartbeat_source);
        heartbeat_source = 0;
    }
    gtk_window_close(study->shell.window);
    study = NULL;
    g_main_loop_quit(loop);
}
static gboolean next_phase(gpointer unused)
{
    (void)unused;
    report();
    if (phase > 0 && violations > 0) {
        g_printerr("Invalid measurement: focus or allocation changed; stopping study.\n");
        invalid = TRUE;
        close_study();
        return G_SOURCE_REMOVE;
    }
    phase++;
    if (phase == G_N_ELEMENTS(phases)) {
        close_study();
        return G_SOURCE_REMOVE;
    }
    if (phase == 1 || phase == 7) {
        g_assert_true(chrono_store_start_task(app_store(study), task_id,
                                              g_get_monotonic_time(), NULL));
        ui_invalidate_dynamic(study);
        app_refresh_power_policy(study);
    }
    if (phase == 3) {
        on_enter(NULL, 200, 200, study);
        motion_start_us = g_get_monotonic_time();
        motion_source = g_timeout_add(8, motion, NULL);
    }
    if (phase == 5) {
        g_source_remove(motion_source);
        motion_source = 0;
        on_leave(NULL, study);
        chrono_store_pause_active(app_store(study), g_get_monotonic_time());
        ui_invalidate_dynamic(study);
        app_refresh_power_policy(study);
    }
    if (phase == 7) gtk_widget_set_visible(GTK_WIDGET(study->shell.window), FALSE);
    if (phase == 8) {
        chrono_store_pause_active(app_store(study), g_get_monotonic_time());
        app_refresh_power_policy(study);
    }
    expected_width = study->scene.surface_width_px;
    expected_height = study->scene.surface_height_px;
    draws = uploads = paints = violations = 0;
    previous_draw_us = 0;
    g_array_set_size(intervals, 0);
    g_array_set_size(input_ages, 0);
    start_us = g_get_monotonic_time();
    start_cpu = process_seconds();
    g_print("Benchmark %s: %dx%d active=%d\n", phases[phase].name,
        expected_width, expected_height, gtk_window_is_active(study->shell.window));
    g_timeout_add_seconds(phases[phase].seconds, next_phase, NULL);
    return G_SOURCE_REMOVE;
}
int main(void)
{
    g_assert_nonnull(g_getenv("CHRONOLEDGER_FILE"));
    const char *directory = g_getenv("PRESENTATION_STUDY_OUTPUT");
    g_assert_nonnull(directory);
    char *path = g_build_filename(directory, "phases.csv", NULL);
    output = fopen(path, "w"); g_assert_nonnull(output); g_free(path);
    fprintf(output, "phase,seconds,cpu_ms,draws,uploads,paints,width,height,violations,"
                    "frame_p50_ms,frame_p95_ms,input_age_p50_ms,input_age_p95_ms,power_mode\n");
    intervals = g_array_new(FALSE, FALSE, sizeof(gint64));
    input_ages = g_array_new(FALSE, FALSE, sizeof(gint64));
    configure_presentation_startup();
    gtk_init();
    GtkApplication *app = gtk_application_new("io.github.chronoledger.PresentationStudy",
                                              G_APPLICATION_NON_UNIQUE);
    g_application_register(G_APPLICATION(app), NULL, NULL);
    activate(app, NULL);
    study = g_object_get_data(G_OBJECT(gtk_application_get_active_window(app)), "chronoledger-state");
    gtk_window_set_title(study->shell.window, "ChronoLedger presentation benchmark");
    app_preferences(study)->notifications_enabled = FALSE;
    ChronoTask *task = chrono_task_new("Representative 25 minute allocation", 1500,
                                     g_get_real_time() / G_USEC_PER_SEC);
    task_id = g_strdup(chrono_task_get_id(task));
    g_assert_true(chrono_store_add_task(app_store(study), task));
    for (int i = 0; i < 4; ++i) {
        task = chrono_task_new("Queued study allocation", 1200, g_get_real_time() / G_USEC_PER_SEC);
        g_assert_true(chrono_store_add_task(app_store(study), task));
    }
    ui_invalidate_dynamic(study);
    g_signal_connect(gtk_widget_get_frame_clock(GTK_WIDGET(study->shell.window)),
                     "after-paint", G_CALLBACK(after_paint), NULL);
    loop = g_main_loop_new(NULL, FALSE);
    expected_width = study->scene.surface_width_px;
    expected_height = study->scene.surface_height_px;
    start_us = g_get_monotonic_time(); start_cpu = process_seconds();
    g_timeout_add(200, focus_initial_window, NULL);
    g_timeout_add_seconds(phases[0].seconds, next_phase, NULL);
    heartbeat_source = g_timeout_add_seconds(1, heartbeat, NULL);
    g_main_loop_run(loop);
    if (heartbeat_source != 0) g_source_remove(heartbeat_source);
    g_main_loop_unref(loop); g_object_unref(app);
    g_array_unref(intervals); g_array_unref(input_ages);
    if (motion_source != 0) g_source_remove(motion_source);
    g_free(task_id); fclose(output);
    return invalid ? 2 : 0;
}
