/* Development-only signal-driven smoke test on a real display. Never uses
 * the installed application's identity or data path. */
#define main chronoledger_unused_main
#include "../../src/main.c"
#undef main
#include "../../src/app_input.c"
#include "app_modal.h"
#include "app_routines.h"
#include "app_view.h"
#include "renderer_state.h"
#include <epoxy/gl.h>

static AppState* test_state;
static GMainLoop* test_loop;
static GtkWindow* focus_window;
static guint phase;
static gboolean panel_scrolled;
static const char* capture_name;
static gboolean
capture_frame(GtkGLArea* area, GdkGLContext* context, gpointer unused)
{
    (void)context;
    (void)unused;
    if (capture_name == NULL)
        return TRUE;
    int w = gtk_widget_get_width(GTK_WIDGET(area)) * gtk_widget_get_scale_factor(GTK_WIDGET(area));
    int h = gtk_widget_get_height(GTK_WIDGET(area)) * gtk_widget_get_scale_factor(GTK_WIDGET(area));
    guchar *rgba = g_malloc((gsize)w * h * 4), *argb = g_malloc((gsize)w * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            gsize a = ((gsize)y * w + x) * 4, b = ((gsize)(h - 1 - y) * w + x) * 4;
            argb[a] = rgba[b + 2];
            argb[a + 1] = rgba[b + 1];
            argb[a + 2] = rgba[b];
            argb[a + 3] = 255;
        }
    char* path = g_strdup_printf("%s/%s.png", g_getenv("CURSOR_STUDY_OUTPUT"), capture_name);
    cairo_surface_t* image
        = cairo_image_surface_create_for_data(argb, CAIRO_FORMAT_ARGB32, w, h, w * 4);
    g_assert_cmpint(cairo_surface_write_to_png(image, path), ==, CAIRO_STATUS_SUCCESS);
    g_print("Capture %s %dx%d\n", capture_name, w, h);
    g_free(path);
    cairo_surface_destroy(image);
    g_free(rgba);
    g_free(argb);
    capture_name = NULL;
    return TRUE;
}
static void
expect_cursor(gboolean custom)
{
    GdkCursor* cursor = gtk_widget_get_cursor(GTK_WIDGET(test_state->shell.gl_area));
    g_assert_nonnull(cursor);
    const char* name = gdk_cursor_get_name(cursor);
    g_print("Cursor %s; expected %s; power %d active %d\n", name, custom ? "none" : "default",
        test_state->runtime.power_mode, gtk_window_is_active(test_state->shell.window));
    g_assert_cmpstr(name, ==, custom ? "none" : "default");
}
static void focus_title(const char *title)
{
    if (g_getenv("HYPRLAND_INSTANCE_SIGNATURE") != NULL) {
        // Only fixed test-owned titles are passed here.
        char *code = g_strdup_printf("hl.dispatch(hl.dsp.focus({window = \"title:^(%s)$\"}))", title);
        const char *argv[] = {"hyprctl", "eval", code, NULL};
        GError *error = NULL;
        g_spawn_sync(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH,
            NULL, NULL, NULL, NULL, NULL, &error);
        g_free(code);
        g_assert_no_error(error);
    }
}
static void focus_test_window(void)
{
    gtk_window_present(test_state->shell.window);
    focus_title("ChronoLedger validation");
}
static void capture_window(const char *name)
{
    GtkWidget *window = GTK_WIDGET(test_state->shell.window);
    int width = gtk_widget_get_width(window), height = gtk_widget_get_height(window);
    GdkPaintable *paintable = gtk_widget_paintable_new(window);
    GtkSnapshot *snapshot = gtk_snapshot_new();
    gdk_paintable_snapshot(paintable, GDK_SNAPSHOT(snapshot), width, height);
    GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
    g_assert_nonnull(node);
    graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, width, height);
    GdkTexture *texture = gsk_renderer_render_texture(
        gtk_native_get_renderer(GTK_NATIVE(window)), node, &bounds);
    char *path = g_strdup_printf("%s/%s.png", g_getenv("CURSOR_STUDY_OUTPUT"), name);
    g_assert_true(gdk_texture_save_to_png(texture, path));
    g_print("Window capture %s %dx%d\n", name, width, height);
    g_free(path);
    g_object_unref(texture);
    gsk_render_node_unref(node);
    g_object_unref(paintable);
}
static gboolean sweep(gpointer data)
{
    gint64 start = *(gint64 *)data;
    double seconds = (g_get_monotonic_time() - start) / 1000000.0;
    if (seconds > .8) return G_SOURCE_REMOVE;
    double x = seconds < .4 ? 100 + 900 * seconds : 460 - 700 * (seconds - .4);
    on_motion(NULL, x, 210 + 40 * seconds, test_state);
    return G_SOURCE_CONTINUE;
}
static void start_sweep(float persistence)
{
    AppState *s = test_state;
    s->presentation.settings.persistence = persistence;
    cursor_state_clear(&s->input, s->renderer);
    on_enter(NULL, 100, 210, s);
    gint64 *start = g_new(gint64, 1);
    *start = g_get_monotonic_time();
    g_timeout_add_full(G_PRIORITY_DEFAULT, 8, sweep, start, g_free);
}
static GtkWidget *find_widget(GtkWidget *root, GType type, guint *index)
{
    if (G_TYPE_CHECK_INSTANCE_TYPE(root, type)) {
        if (*index == 0) return root;
        --*index;
    }
    for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
         child = gtk_widget_get_next_sibling(child)) {
        GtkWidget *found = find_widget(child, type, index);
        if (found != NULL) return found;
    }
    return NULL;
}
static GtkWidget *nth_widget(GtkWidget *root, GType type, guint index)
{
    GtkWidget *result = find_widget(root, type, &index);
    g_assert_nonnull(result);
    return result;
}
static GtkWidget *save_button(GtkWidget *root)
{
    if (GTK_IS_BUTTON(root) && g_strcmp0(gtk_button_get_label(GTK_BUTTON(root)), "Save") == 0)
        return root;
    for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
         child = gtk_widget_get_next_sibling(child)) {
        GtkWidget *found = save_button(child);
        if (found != NULL) return found;
    }
    return NULL;
}
static gboolean
step(gpointer unused)
{
    (void)unused;
    AppState* s = test_state;
    if (capture_name != NULL) {
        gtk_gl_area_make_current(s->shell.gl_area);
        gtk_gl_area_attach_buffers(s->shell.gl_area);
        renderer_invalidate_final(s->renderer);
        s->runtime.last_draw_frame_counter = -1;
        AppViewSnapshot view;
        g_assert_true(app_view_snapshot_build(&view, &s->session, g_get_monotonic_time()));
        g_assert_cmpint(ui_prepare_frame(s, &view), ==, UI_FRAME_PREPARE_OK);
        RendererFrameInput frame = { .presentation = &s->presentation,
            .scene = &s->scene,
            .input = &s->input,
            .view = &view,
            .scene_needs_upload = TRUE,
            .cursor_visible = s->runtime.power_mode == CHRONO_POWER_INTERACTIVE,
            .frame_delta = 1.0 / 60,
            .render_interval = 1.0 / 60 };
        g_assert_cmpint(renderer_draw(s->renderer, &frame, NULL, NULL), ==, RENDERER_DRAW_OK);
        capture_frame(s->shell.gl_area, NULL, NULL);
    }
    switch (++phase) {
    case 1:
        focus_test_window();
        g_print("Initial allocation %dx%d\n", gtk_widget_get_width(GTK_WIDGET(s->shell.gl_area)),
            gtk_widget_get_height(GTK_WIDGET(s->shell.gl_area)));
        g_assert_true(renderer_is_ready(s->renderer));
        on_enter(NULL, 100, 150, s);
        gtk_widget_set_size_request(GTK_WIDGET(s->shell.gl_area), 800, 540);
        gtk_window_set_resizable(s->shell.window, FALSE);
        break;
    case 2:
        g_assert_true(gtk_window_is_active(s->shell.window));
        on_enter(NULL, 200, 200, s);
        on_motion(NULL, 400, 220, s);
        app_input_process_pending(s);
        g_assert_cmpfloat(s->input.raw_pointer_x, ==, 400);
        capture_name = "interactive";
        gtk_gl_area_queue_render(s->shell.gl_area);
        break;
    case 3: {
        expect_cursor(s->runtime.power_mode == CHRONO_POWER_INTERACTIVE);
        // Curvature and hover share the shader source lookup.
        app_preferences(s)->curvature_enabled = TRUE;
        s->presentation.settings.curvature_strength = 1;
        update_pointer_now(s, 300, 200);
        double x, y;
        cursor_state_map_pointer(
            &s->scene, app_preferences(s), &s->presentation.settings, 300, 200, &x, &y);
        g_assert_cmpfloat(s->input.pointer_x, ==, x);
        g_assert_cmpfloat(s->input.pointer_y, ==, y);
        Rect r = s->presentation.layout.modes[CHRONO_MODE_LEDGER];
        // Inject a press at a known scene target; no physical button events.
        ui_handle_press(s, r.x + r.width / 2, r.y + r.height / 2, GDK_BUTTON_PRIMARY, NULL);
        g_assert_cmpint(s->presentation.active_tab, ==, CHRONO_MODE_LEDGER);
        ui_handle_release(s, x, y, GDK_BUTTON_PRIMARY);
        on_leave(NULL, s);
        expect_cursor(FALSE);
        g_assert_cmpuint(s->input.cursor_sample_count, ==, 0);
        on_enter(NULL, 300, 200, s);
        app_show_alert(s, "Cursor study", "Disposable native panel; testing pointer fallback.");
        expect_cursor(FALSE);
        g_assert_cmpuint(s->input.cursor_sample_count, ==, 0);
        break;
    }
    case 4:
        app_modal_cancel(s);
        on_enter(NULL, 300, 200, s);
        gtk_widget_set_size_request(GTK_WIDGET(s->shell.gl_area), 360, 240);
        gtk_window_set_default_size(s->shell.window, 360, 240);
        capture_name = "below-minimum";
        gtk_gl_area_queue_render(s->shell.gl_area);
        break;
    case 5:
        g_print("Small allocation %dx%d\n", gtk_widget_get_width(GTK_WIDGET(s->shell.gl_area)),
            gtk_widget_get_height(GTK_WIDGET(s->shell.gl_area)));
        if (s->presentation.layout.density == UI_DENSITY_TOO_SMALL)
            expect_cursor(FALSE);
        else
            g_error("Compositor did not provide a below-minimum allocation");
        gtk_widget_set_size_request(GTK_WIDGET(s->shell.gl_area), 800, 540);
        gtk_window_set_default_size(s->shell.window, 800, 540);
        break;
    case 6:
        on_enter(NULL, 300, 200, s);
        focus_window = GTK_WINDOW(gtk_window_new());
        gtk_window_set_application(focus_window, s->shell.application);
        gtk_window_set_transient_for(focus_window, s->shell.window);
        gtk_window_set_title(focus_window, "Cursor study focus probe");
        gtk_window_set_default_size(focus_window, 240, 120);
        gtk_window_present(focus_window);
        focus_title("Cursor study focus probe");
        break;
    case 7:
        g_assert_false(gtk_window_is_active(s->shell.window));
        expect_cursor(FALSE);
        g_assert_cmpuint(s->input.cursor_sample_count, ==, 0);
        g_assert_cmpuint(s->runtime.tick_callback_id, ==, 0);
        gtk_window_destroy(focus_window);
        focus_window = NULL;
        focus_test_window();
        break;
    case 8:
        g_assert_true(gtk_window_is_active(s->shell.window));
        on_enter(NULL, 300, 200, s);
        gtk_gl_area_queue_render(s->shell.gl_area);
        break;
    case 9:
        expect_cursor(TRUE);
        app_preferences(s)->cursor_shader_effects_enabled = FALSE;
        on_motion(NULL, 400, 250, s);
        capture_name = "bypass";
        gtk_gl_area_queue_render(s->shell.gl_area);
        break;
    case 10:
        expect_cursor(TRUE);
        g_assert_cmpuint(s->input.cursor_sample_count, ==, 0);
        gtk_widget_set_visible(GTK_WIDGET(s->shell.window), FALSE);
        expect_cursor(FALSE);
        g_assert_cmpuint(s->runtime.tick_callback_id, ==, 0);
        break;
    case 11:
        gtk_window_present(s->shell.window);
        break;
    case 12:
        on_enter(NULL, 300, 200, s);
        break;
    case 13:
        expect_cursor(s->runtime.power_mode == CHRONO_POWER_INTERACTIVE);
        // Actual GL resource teardown followed by native fallback and recovery.
        gtk_gl_area_make_current(s->shell.gl_area);
        renderer_unrealize(s);
        app_refresh_power_policy(s);
        expect_cursor(FALSE);
        g_assert_false(renderer_is_ready(s->renderer));
        renderer_realize(s);
        app_refresh_power_policy(s);
        on_enter(NULL, 300, 200, s);
        break;
    case 14:
        expect_cursor(s->runtime.power_mode == CHRONO_POWER_INTERACTIVE);
        capture_name = "recovered";
        gtk_gl_area_queue_render(s->shell.gl_area);
        break;
    case 15: {
        gtk_widget_set_size_request(GTK_WIDGET(s->shell.gl_area), 560, 360);
        gtk_window_set_default_size(s->shell.window, 560, 360);
        app_preferences(s)->cursor_shader_effects_enabled = TRUE;
        app_preferences(s)->notifications_enabled = FALSE;
        const ChronoMode modes[] = {CHRONO_MODE_FOCUS, CHRONO_MODE_LEDGER,
            CHRONO_MODE_ROUTINES, CHRONO_MODE_CALIBRATION};
        for (guint i = 0; i < 4; i++) {
            g_assert_true(on_key_pressed(NULL, GDK_KEY_F1 + i, 0, 0, s));
            g_assert_cmpint(s->presentation.active_tab, ==, modes[i]);
        }
        g_assert_false(on_key_pressed(NULL, GDK_KEY_3, 0, 0, s));
        g_assert_true(on_key_pressed(NULL, GDK_KEY_F3, 0, 0, s));
        app_routine_command(s, 2);
        GtkWidget *panel = s->shell.modal_overlay;
        g_assert_nonnull(panel);
        gtk_editable_set_text(GTK_EDITABLE(nth_widget(panel, GTK_TYPE_ENTRY, 0)), "Scheduled instrument check");
        gtk_drop_down_set_selected(GTK_DROP_DOWN(nth_widget(panel, GTK_TYPE_DROP_DOWN, 0)), 2);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(nth_widget(panel, GTK_TYPE_SPIN_BUTTON, 1)), 21);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(nth_widget(panel, GTK_TYPE_SPIN_BUTTON, 2)), 37);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(nth_widget(panel, GTK_TYPE_SPIN_BUTTON, 3)), 2);
        GtkWidget *save = save_button(panel);
        g_assert_nonnull(save);
        g_signal_emit_by_name(save, "clicked");
        break;
    }
    case 16: {
        g_assert_null(s->shell.modal_overlay);
        ChronoPreferences *p = app_preferences(s);
        g_assert_cmpuint(p->routine_count, ==, 1);
        g_assert_true(p->routines[0].scheduled);
        g_assert_cmpuint(p->routines[0].clock_minute, ==, 21 * 60 + 37);
        g_assert_cmpuint(p->routines[0].window_minutes, ==, 2);
        app_routine_command(s, 4);
        gtk_widget_grab_focus(nth_widget(s->shell.modal_overlay, GTK_TYPE_SPIN_BUTTON, 3));
        // Exercise real frame-clock maintenance while an operator panel is open.
        p->routines[0].due = g_get_real_time() / G_USEC_PER_SEC - 121;
        break;
    }
    case 17: {
        ChronoRoutine *r = &app_preferences(s)->routines[0];
        g_assert_nonnull(s->shell.modal_overlay);
        // An inactive compositor allocation uses the one-second source, not
        // frame ticks. Wait for semantic maintenance rather than assuming FPS.
        if (!r->overdue) { --phase; return G_SOURCE_CONTINUE; }
        if (!panel_scrolled) {
            GtkScrolledWindow *scroll = GTK_SCROLLED_WINDOW(nth_widget(
                s->shell.modal_overlay, GTK_TYPE_SCROLLED_WINDOW, 0));
            GtkAdjustment *adjustment = gtk_scrolled_window_get_vadjustment(scroll);
            g_assert_cmpfloat(gtk_adjustment_get_upper(adjustment), >,
                gtk_adjustment_get_page_size(adjustment));
            gtk_adjustment_set_value(adjustment, gtk_adjustment_get_upper(adjustment) -
                gtk_adjustment_get_page_size(adjustment));
            panel_scrolled = TRUE;
            --phase;
            return G_SOURCE_CONTINUE;
        }
        capture_window("routine-editor-minimal");
        // Saving a schedule that became due during editing must retain the item.
        g_signal_emit_by_name(save_button(s->shell.modal_overlay), "clicked");
        g_assert_nonnull(s->shell.modal_overlay);
        g_assert_false(gtk_widget_get_sensitive(nth_widget(s->shell.modal_overlay, GTK_TYPE_DROP_DOWN, 0)));
        g_assert_cmpint(r->deadline, ==, r->due + 120);
        ChronoDocument *loaded = chrono_persistence_load(s->session.data_path, NULL);
        g_assert_nonnull(loaded);
        g_assert_true(chrono_document_get_preferences_const(loaded)->routines[0].overdue);
        chrono_document_free(loaded);
        app_modal_cancel(s);
        capture_name = "routine-overdue";
        gtk_gl_area_queue_render(s->shell.gl_area);
        break;
    }
    case 18: {
        Rect r = app_routine_control(&s->presentation.layout, 3);
        ui_handle_press(s, r.x + r.width / 2, r.y + r.height / 2, GDK_BUTTON_PRIMARY, NULL);
        ui_handle_release(s, r.x + r.width / 2, r.y + r.height / 2, GDK_BUTTON_PRIMARY);
        ChronoRoutine *routine = &app_preferences(s)->routines[0];
        g_assert_cmpint(routine->deadline, ==, 0);
        g_assert_false(routine->overdue);
        g_assert_cmpint(routine->due, >, g_get_real_time() / G_USEC_PER_SEC);
        // Keep maintenance alive when the window is unmapped and GL is unavailable.
        routine->due = g_get_real_time() / G_USEC_PER_SEC - 121;
        gtk_widget_set_visible(GTK_WIDGET(s->shell.window), FALSE);
        g_assert_cmpint(s->runtime.power_mode, ==, CHRONO_POWER_BACKGROUND_CLOCK);
        g_assert_cmpuint(s->runtime.low_power_source_id, !=, 0);
        break;
    }
    case 19:
    case 20:
        break; // Allow the one-second background source to run.
    case 21:
        g_assert_true(app_preferences(s)->routines[0].overdue);
        gtk_window_present(s->shell.window);
        break;
    case 22:
        app_routine_command(s, 3);
        capture_name = "routine-waiting";
        gtk_gl_area_queue_render(s->shell.gl_area);
        break;
    case 23:
        focus_test_window();
        g_assert_cmpint(app_preferences(s)->routines[0].deadline, ==, 0);
        g_print("Scheduled editor, panel/background maintenance, persisted overdue, confirmation and tab keys passed\n");
        break;
    case 24:
        start_sweep(.24f);
        break;
    case 25:
        expect_cursor(TRUE);
        capture_name = "motion-ordinary";
        gtk_gl_area_queue_render(s->shell.gl_area);
        break;
    case 26:
        break;
    case 27:
        start_sweep(1);
        break;
    case 28:
        expect_cursor(TRUE);
        capture_name = "motion-increased";
        gtk_gl_area_queue_render(s->shell.gl_area);
        break;
    case 29:
        break;
    case 30:
        gtk_window_close(s->shell.window);
        test_state = NULL;
        g_main_loop_quit(test_loop);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}
static gboolean
timeout(gpointer unused)
{
    (void)unused;
    g_error("Cursor display test timed out, phase %u", phase);
    return G_SOURCE_REMOVE;
}
int
main(void)
{
    g_assert_nonnull(g_getenv("CHRONOLEDGER_FILE"));
    g_assert_nonnull(g_getenv("CURSOR_STUDY_OUTPUT"));
    gtk_init();
    GtkApplication* app
        = gtk_application_new("io.github.chronoledger.CursorStudy", G_APPLICATION_NON_UNIQUE);
    g_application_register(G_APPLICATION(app), NULL, NULL);
    activate(app, NULL);
    test_state
        = g_object_get_data(G_OBJECT(gtk_application_get_active_window(app)), "chronoledger-state");
    gtk_window_set_title(test_state->shell.window, "ChronoLedger validation");
    test_loop = g_main_loop_new(NULL, FALSE);
    g_timeout_add(450, step, NULL);
    g_timeout_add_seconds(30, timeout, NULL);
    g_main_loop_run(test_loop);
    g_main_loop_unref(test_loop);
    g_object_unref(app);
    g_print("Display-backed cursor checks passed\n");
    return 0;
}
