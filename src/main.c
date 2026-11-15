#include "app.h"
#include "app_modal.h"
#include "ui.h"
#include "renderer.h"
#include "chrono_persistence.h"
#include "app_actions.h"
#include "shader_profile.h"
#include "app_input.h"
#include "app_dialogs.h"
#include "app_runtime.h"

#define CHRONO_APP_ID "io.github.chronoledger.ChronoLedger"
#define ANNUNCIATOR_DURATION_US (4 * G_USEC_PER_SEC)

static gint64
saturating_add_i64(gint64 left, gint64 right)
{
    if (right > 0 && left > G_MAXINT64 - right)
        return G_MAXINT64;
    if (right < 0 && left < G_MININT64 - right)
        return G_MININT64;
    return left + right;
}

static void
install_css(GtkWidget *widget)
{
    static const char css[] =
        ".crt-diagnostic {"
        "  background: #020604;"
        "  color: #83ffac;"
        "  padding: 36px;"
        "}"
        ".crt-diagnostic-title {"
        "  font-family: sans-serif;"
        "  font-size: 23px;"
        "  font-weight: 700;"
        "}"
        ".crt-diagnostic-message {"
        "  font-family: monospace;"
        "  font-size: 14px;"
        "}"
        ".chrono-input-proxy {"
        "  opacity: 0.01;"
        "  min-width: 1px;"
        "  min-height: 1px;"
        "  padding: 0;"
        "  margin: 0;"
        "}";

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, css);
    gtk_style_context_add_provider_for_display(
        gtk_widget_get_display(widget),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

void
app_show_diagnostic(AppState *state, const char *message, gboolean is_error)
{
    if (state->shell.diagnostic_overlay == NULL || state->shell.diagnostic_label == NULL)
        return;
    gtk_label_set_text(state->shell.diagnostic_label, message);
    gtk_spinner_set_spinning(state->shell.diagnostic_spinner, !is_error);
    gtk_widget_set_visible(GTK_WIDGET(state->shell.diagnostic_spinner), !is_error);
    gtk_widget_set_visible(state->shell.diagnostic_overlay, TRUE);
}

void
app_hide_diagnostic(AppState *state)
{
    if (state->shell.diagnostic_overlay == NULL)
        return;
    gtk_spinner_set_spinning(state->shell.diagnostic_spinner, FALSE);
    gtk_widget_set_visible(state->shell.diagnostic_overlay, FALSE);
}

static GtkWidget *
create_diagnostic_overlay(AppState *state)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_hexpand(box, TRUE);
    gtk_widget_set_vexpand(box, TRUE);
    gtk_widget_set_halign(box, GTK_ALIGN_FILL);
    gtk_widget_set_valign(box, GTK_ALIGN_FILL);
    gtk_widget_add_css_class(box, "crt-diagnostic");

    GtkWidget *center = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_halign(center, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(center, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(center, TRUE);
    gtk_widget_set_vexpand(center, TRUE);
    gtk_box_append(GTK_BOX(box), center);

    GtkWidget *title = gtk_label_new("ChronoLedger");
    gtk_widget_add_css_class(title, "crt-diagnostic-title");
    gtk_box_append(GTK_BOX(center), title);

    state->shell.diagnostic_spinner = GTK_SPINNER(gtk_spinner_new());
    gtk_widget_set_halign(GTK_WIDGET(state->shell.diagnostic_spinner), GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(center), GTK_WIDGET(state->shell.diagnostic_spinner));

    state->shell.diagnostic_label = GTK_LABEL(
        gtk_label_new("Initializing the phosphor pipeline…"));
    gtk_label_set_wrap(state->shell.diagnostic_label, TRUE);
    gtk_label_set_justify(state->shell.diagnostic_label, GTK_JUSTIFY_CENTER);
    gtk_label_set_max_width_chars(state->shell.diagnostic_label, 82);
    gtk_widget_add_css_class(GTK_WIDGET(state->shell.diagnostic_label),
                             "crt-diagnostic-message");
    gtk_box_append(GTK_BOX(center), GTK_WIDGET(state->shell.diagnostic_label));

    GtkWidget *close_button = gtk_button_new_with_label("Close");
    gtk_widget_set_halign(close_button, GTK_ALIGN_CENTER);
    g_signal_connect_swapped(close_button,
                             "clicked",
                             G_CALLBACK(gtk_window_close),
                             state->shell.window);
    gtk_box_append(GTK_BOX(center), close_button);
    return box;
}

void
app_set_annunciator(AppState *state,
                    AnnunciatorKind kind,
                    const char *message)
{
    g_free(state->session.annunciator_text);
    state->session.annunciator_text = g_strdup(message != NULL ? message : "");
    state->session.annunciator_kind = kind;
    state->session.annunciator_until_us = saturating_add_i64(
        g_get_monotonic_time(), ANNUNCIATOR_DURATION_US);
    state->runtime.annunciator_visible = state->session.annunciator_text[0] != '\0';
    ui_invalidate_dynamic(state);
}

gboolean
app_save_state(AppState *state)
{
    if (state->session.data_load_failed) {
        g_free(state->session.save_error);
        state->session.save_error = g_strdup(
            "The existing ledger was not loaded and will not be overwritten; "
            "import a valid ledger to replace it explicitly");
        state->session.save_state = SAVE_STATE_FAILED;
        app_set_annunciator(state, ANNUNCIATOR_FAILURE, state->session.save_error);
        ui_invalidate_dynamic(state);
        return FALSE;
    }

    GError *error = NULL;
    gboolean ok = chrono_persistence_save(state->session.document,
                                          state->session.data_path,
                                          g_get_monotonic_time(),
                                          &error);
    g_free(state->session.save_error);
    state->session.save_error = NULL;
    if (ok) {
        state->session.save_state = SAVE_STATE_SAVED;
    } else {
        state->session.save_state = SAVE_STATE_FAILED;
        state->session.save_error = g_strdup(error != NULL
                                     ? error->message
                                     : "Unknown persistence failure");
        app_set_annunciator(state, ANNUNCIATOR_FAILURE,
                            state->session.save_error);
    }
    g_clear_error(&error);
    ui_invalidate_dynamic(state);
    return ok;
}

void
app_retry_save(AppState *state)
{
    if (app_save_state(state))
        app_set_annunciator(state, ANNUNCIATOR_SUCCESS, "Ledger synchronized");
}

void
app_notify_expired(AppState *state, ChronoTask *task)
{
    if (task == NULL)
        return;

    if (app_preferences(state)->notifications_enabled) {
        GNotification *notification = g_notification_new("Allocation reached");
        char *body = g_strdup_printf("%s has reached its original allocation.",
                                     chrono_task_get_title(task));
        g_notification_set_body(notification, body);
        g_notification_set_priority(notification, G_NOTIFICATION_PRIORITY_HIGH);
        GIcon *icon = g_themed_icon_new("alarm-symbolic");
        g_notification_set_icon(notification, icon);
        char *id = g_strdup_printf("chronoledger-expired-%s", chrono_task_get_id(task));
        g_application_send_notification(G_APPLICATION(state->shell.application),
                                        id,
                                        notification);
        g_free(id);
        g_free(body);
        g_object_unref(icon);
        g_object_unref(notification);
    }

    if (app_preferences(state)->sound_enabled)
        gtk_widget_error_bell(GTK_WIDGET(state->shell.window));
}

static void
sync_proxy_text(AppState *state, const char *text)
{
    state->session.input_syncing = TRUE;
    gtk_editable_set_text(GTK_EDITABLE(state->shell.input_proxy),
                          text != NULL ? text : "");
    gtk_editable_set_position(GTK_EDITABLE(state->shell.input_proxy), -1);
    state->session.input_syncing = FALSE;
}

void
app_begin_edit(AppState *state, EditTarget target)
{
    if (state->session.data_load_failed)
        return;
    if (!state->presentation.layout.show_add_form) {
        app_set_annunciator(state, ANNUNCIATOR_INFO,
                            "Expand the tile to create a new allocation");
        return;
    }
    state->session.edit_target = target;
    gtk_entry_set_max_length(state->shell.input_proxy,
                             target == EDIT_TITLE
                                 ? CHRONO_TASK_TITLE_MAX_CHARS
                                 : 32);
    sync_proxy_text(state,
                    target == EDIT_TITLE
                        ? state->session.draft_title
                        : state->session.draft_duration);
    gtk_widget_grab_focus(GTK_WIDGET(state->shell.input_proxy));
    ui_invalidate_dynamic(state);
}

void
app_finish_edit(AppState *state)
{
    state->session.edit_target = EDIT_NONE;
    gtk_widget_grab_focus(GTK_WIDGET(state->shell.gl_area));
    ui_invalidate_dynamic(state);
}

static void
on_proxy_changed(GtkEditable *editable, gpointer user_data)
{
    AppState *state = user_data;
    if (state->session.input_syncing || state->session.edit_target == EDIT_NONE)
        return;
    const char *text = gtk_editable_get_text(editable);
    char **target = state->session.edit_target == EDIT_TITLE
        ? &state->session.draft_title
        : &state->session.draft_duration;
    g_free(*target);
    *target = g_strdup(text);
    g_clear_pointer(&state->session.validation_message, g_free);
    ui_invalidate_dynamic(state);
}

static void
on_proxy_activate(GtkEntry *entry, gpointer user_data)
{
    (void)entry;
    AppState *state = user_data;
    if (state->session.edit_target == EDIT_TITLE)
        app_begin_edit(state, EDIT_DURATION);
    else if (state->session.edit_target == EDIT_DURATION) {
        AppAction action = {.type = APP_ACTION_COMMIT_NEW_TASK};
        app_dispatch_action(state, &action);
    }
}

void
app_capture_presentation(AppState *state)
{
    app_preferences(state)->selected_tab = state->presentation.active_tab;
    app_preferences(state)->presentation_mode = state->presentation.presentation_mode;
    app_preferences(state)->settings = state->presentation.profile_transition_active
        ? state->presentation.profile_target : state->presentation.settings;

}

static gboolean
on_close_request(GtkWindow *window, gpointer user_data)
{
    (void)window;
    AppState *state = user_data;

    if (state->shell.closing)
        return FALSE;

    app_capture_presentation(state);

    if (!state->session.data_load_failed && !app_save_state(state)) {
        app_show_alert(state, "ChronoLedger could not save before closing",
                   state->session.save_error);
        return TRUE;
    }
    state->shell.closing = TRUE;
    app_modal_cancel(state);
    state->session.next_checkpoint_us = 0;
    app_refresh_power_policy(state);
    return FALSE;
}

static void
app_state_free(gpointer data)
{
    AppState *state = data;
    if (state->runtime.low_power_source_id != 0)
        g_source_remove(state->runtime.low_power_source_id);
    ui_release_resources(state);
    renderer_destroy(state->renderer);
    chrono_document_free(state->session.document);
    g_free(state->session.data_path);
    g_free(state->session.save_error);
    g_free(state->session.selected_task_id);
    g_free(state->session.draft_title);
    g_free(state->session.draft_duration);
    g_free(state->session.validation_message);
    g_free(state->session.annunciator_text);
    g_free(state);
}

static ChronoDocument *
load_initial_document(AppState *state, char **startup_error)
{
    GError *error = NULL;
    ChronoDocument *document = chrono_persistence_load(state->session.data_path, &error);
    if (document == NULL) {
        state->session.data_load_failed = TRUE;
        *startup_error = g_strdup_printf(
            "%s. The original ledger has been left untouched; import a valid "
            "ledger to replace it explicitly.",
            error != NULL ? error->message : "Unknown load error");
        state->session.save_state = SAVE_STATE_FAILED;
        g_free(state->session.save_error);
        state->session.save_error = g_strdup(*startup_error);
        g_clear_error(&error);
        return chrono_document_new();
    }

    if (chrono_store_get_task_count(chrono_document_get_store(document)) > 0 ||
        g_file_test(state->session.data_path, G_FILE_TEST_EXISTS) ||
        g_getenv("CHRONOLEDGER_FILE") != NULL)
        return document;

    char *legacy_path = g_build_filename(g_get_user_data_dir(),
                                         "time-wallet", "tasks.json", NULL);
    if (!g_file_test(legacy_path, G_FILE_TEST_EXISTS)) {
        g_free(legacy_path);
        return document;
    }

    ChronoDocument *legacy = chrono_persistence_load(legacy_path, &error);
    g_free(legacy_path);
    if (legacy == NULL) {
        g_clear_error(&error);
        return document;
    }

    chrono_document_free(document);
    if (!chrono_persistence_save(legacy, state->session.data_path,
                                 g_get_monotonic_time(), &error)) {
        *startup_error = g_strdup(error != NULL ? error->message
                                                : "Could not migrate Time Wallet data");
        g_clear_error(&error);
    } else {
        app_set_annunciator(state, ANNUNCIATOR_SUCCESS,
                            "Time Wallet data migrated into ChronoLedger");
    }
    return legacy;
}

static void
activate(GtkApplication *application, gpointer user_data)
{
    (void)user_data;
    GList *windows = gtk_application_get_windows(application);
    if (windows != NULL) {
        gtk_window_present(GTK_WINDOW(windows->data));
        return;
    }

    AppState *state = g_new0(AppState, 1);
    state->renderer = renderer_create();
    state->shell.application = application;
    state->input.active_slider = SLIDER_NONE;
    state->presentation.active_tab = CHRONO_MODE_FOCUS;
    state->runtime.power_mode = CHRONO_POWER_SLEEP;
    state->presentation.presentation_mode = PRESENTATION_BALANCED;
    state->input.hover_control = ui_control_none();
    state->scene.static_dirty = TRUE;
    state->scene.scene_dirty = TRUE;
    state->runtime.last_dynamic_second = -1;
    state->runtime.focus_progress_step = -1;
    state->runtime.last_caret_phase = -1;
    state->runtime.frame_delta = 0.0;
    state->runtime.render_interval = 0.0;
    state->runtime.last_draw_frame_counter = -1;
    state->session.save_state = SAVE_STATE_SAVED;
    state->session.draft_title = g_strdup("");
    state->session.draft_duration = g_strdup("");
    state->session.data_path = chrono_default_data_path();
    shader_settings_reset(&state->presentation.settings);

    char *startup_error = NULL;
    state->session.document = load_initial_document(state, &startup_error);

    state->presentation.active_tab = app_preferences(state)->selected_tab;
    state->presentation.presentation_mode = app_preferences(state)->presentation_mode;
    state->presentation.settings = app_preferences(state)->settings;

    state->shell.window = GTK_WINDOW(gtk_application_window_new(application));
    gtk_window_set_title(state->shell.window, "ChronoLedger");
    gtk_window_set_default_size(state->shell.window, 1280, 800);
    gtk_window_set_decorated(state->shell.window, FALSE);
    gtk_window_set_resizable(state->shell.window, TRUE);

    state->shell.gl_area = GTK_GL_AREA(gtk_gl_area_new());
    gtk_widget_set_hexpand(GTK_WIDGET(state->shell.gl_area), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(state->shell.gl_area), TRUE);
    gtk_widget_set_focusable(GTK_WIDGET(state->shell.gl_area), TRUE);
    gtk_widget_set_size_request(GTK_WIDGET(state->shell.gl_area),
                                CHRONO_WINDOW_MIN_WIDTH, CHRONO_WINDOW_MIN_HEIGHT);
#if GTK_CHECK_VERSION(4, 12, 0)
    gtk_gl_area_set_allowed_apis(state->shell.gl_area,
                                 GDK_GL_API_GL | GDK_GL_API_GLES);
#endif
    gtk_gl_area_set_required_version(state->shell.gl_area, 3, 0);
    gtk_gl_area_set_has_depth_buffer(state->shell.gl_area, FALSE);
    gtk_gl_area_set_has_stencil_buffer(state->shell.gl_area, FALSE);
    gtk_gl_area_set_auto_render(state->shell.gl_area, FALSE);
    app_input_install(state);

    state->shell.root_overlay = gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(state->shell.root_overlay),
                          GTK_WIDGET(state->shell.gl_area));

    state->shell.input_proxy = GTK_ENTRY(gtk_entry_new());
    gtk_widget_set_size_request(GTK_WIDGET(state->shell.input_proxy), 1, 1);
    gtk_widget_set_halign(GTK_WIDGET(state->shell.input_proxy), GTK_ALIGN_START);
    gtk_widget_set_valign(GTK_WIDGET(state->shell.input_proxy), GTK_ALIGN_START);
    gtk_widget_add_css_class(GTK_WIDGET(state->shell.input_proxy),
                             "chrono-input-proxy");
    g_signal_connect(state->shell.input_proxy, "changed",
                     G_CALLBACK(on_proxy_changed), state);
    g_signal_connect(state->shell.input_proxy, "activate",
                     G_CALLBACK(on_proxy_activate), state);
    gtk_overlay_add_overlay(GTK_OVERLAY(state->shell.root_overlay),
                            GTK_WIDGET(state->shell.input_proxy));

    state->shell.diagnostic_overlay = create_diagnostic_overlay(state);
    gtk_overlay_add_overlay(GTK_OVERLAY(state->shell.root_overlay),
                            state->shell.diagnostic_overlay);
    gtk_window_set_child(state->shell.window, state->shell.root_overlay);
    install_css(state->shell.root_overlay);

    g_object_set_data_full(G_OBJECT(state->shell.window),
                           "chronoledger-state",
                           state,
                           app_state_free);
    g_signal_connect(state->shell.window, "close-request",
                     G_CALLBACK(on_close_request), state);
    app_runtime_install(state);

    GdkDisplay *display = gtk_widget_get_display(GTK_WIDGET(state->shell.window));
    g_print("ChronoLedger %s\n", CHRONOLEDGER_VERSION);
    g_print("GTK: %u.%u.%u\n",
            gtk_get_major_version(),
            gtk_get_minor_version(),
            gtk_get_micro_version());
    if (display != NULL) {
        g_print("Display: %s (%s)\n",
                gdk_display_get_name(display),
                G_OBJECT_TYPE_NAME(display));
    } else {
        g_print("Display: unavailable\n");
    }
    g_print("Data file: %s\n", state->session.data_path);
    g_print("Creating full-window CRT render pipeline…\n");

    app_show_diagnostic(state,
                        "Initializing OpenGL/OpenGL ES renderer…",
                        FALSE);
    gtk_window_present(state->shell.window);
    gtk_widget_grab_focus(GTK_WIDGET(state->shell.gl_area));

    if (startup_error != NULL) {
        app_set_annunciator(state, ANNUNCIATOR_FAILURE, startup_error);
        g_free(startup_error);
    }
}

int
main(int argc, char **argv)
{
    GtkApplication *application = gtk_application_new(
        CHRONO_APP_ID,
        g_getenv("CHRONOLEDGER_FILE") != NULL
            ? G_APPLICATION_NON_UNIQUE : G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(application, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(application), argc, argv);
    g_object_unref(application);
    return status;
}
