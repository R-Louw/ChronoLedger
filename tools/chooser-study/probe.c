/* Development-only interposer: exercise the actual executable and real native
 * chooser, without adding lifecycle test switches to application code. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <glib-unix.h>
#include <glib/gstdio.h>
#include <signal.h>
#include <unistd.h>
#include "app_state.h"

typedef struct {
    GtkWindow *window;
    GAsyncReadyCallback callback;
    gpointer user_data;
} Operation;

static guint completions, window_releases, area_releases, dialog_releases;
static guint task_releases, portal_requests, close_source;
static guint results_consumed;
static guint file_releases;

static GtkWidget *find_button(GtkWidget *widget, const char *label)
{
    if (GTK_IS_BUTTON(widget) && g_strcmp0(gtk_button_get_label(GTK_BUTTON(widget)), label) == 0)
        return widget;
    for (GtkWidget *child = gtk_widget_get_first_child(widget); child;
         child = gtk_widget_get_next_sibling(child)) {
        GtkWidget *found = find_button(child, label);
        if (found) return found;
    }
    return NULL;
}

static void assert_closed(GtkWindow *window)
{
    AppState *state = g_object_get_data(G_OBJECT(window), "chronoledger-state");
    g_assert_true(state->shell.closing);
    g_assert_cmpuint(state->runtime.tick_callback_id, ==, 0);
    g_assert_cmpuint(state->runtime.low_power_source_id, ==, 0);
    g_assert_null(gtk_native_get_surface(GTK_NATIVE(window)));
    g_assert_false(gtk_widget_get_realized(GTK_WIDGET(state->shell.gl_area)));
    g_assert_null(state->shell.modal_overlay);
    g_print("STUDY callback consumed; scheduling stopped; GL area unrealized\n");
}

static gboolean close_after_confirmation(gpointer data)
{
    GtkWindow *window = data;
    AppState *state = g_object_get_data(G_OBJECT(window), "chronoledger-state");
    g_assert_null(state->shell.modal_overlay);
    char *contents = NULL;
    g_assert_true(g_file_get_contents(g_getenv("CHRONOLEDGER_FILE"), &contents, NULL, NULL));
    g_assert_nonnull(strstr(contents, "Chooser native selection"));
    g_free(contents);
    gtk_window_close(window);
    assert_closed(window);
    return G_SOURCE_REMOVE;
}

static void released(gpointer data, GObject *object)
{
    (void)object;
    ++*(guint *)data;
}

static gboolean request_close(gpointer data)
{
    close_source = 0;
    GtkWindow *window = data;
    AppState *state = g_object_get_data(G_OBJECT(window), "chronoledger-state");
    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(window));
    g_assert_true(state->shell.file_dialog_pending);
    g_assert_false(state->shell.closing);
    const char *phase = g_getenv("CHOOSER_STUDY_PHASE");
    if (g_str_equal(phase, "initializing") &&
        g_strcmp0(g_getenv("GDK_BACKEND"), "wayland") == 0 &&
        (g_getenv("GDK_DEBUG") == NULL || !strstr(g_getenv("GDK_DEBUG"), "no-portals")))
        g_assert_cmpuint(portal_requests, ==, 0);
    g_print("STUDY close requested phase=%s portal_requests=%u\n", phase, portal_requests);
    gtk_window_close(window);
    gtk_window_close(window); /* Coalesce repeated closes, without destruction. */
    g_assert_true(state->shell.file_dialog_pending);
    g_assert_true(state->shell.close_after_file_dialog);
    g_assert_false(state->shell.closing);
    g_assert_true(gtk_native_get_surface(GTK_NATIVE(window)) == surface);
    g_assert_false(gdk_surface_is_destroyed(surface));
    g_print("STUDY close deferred; parent surface preserved\n");
    return G_SOURCE_REMOVE;
}

static void completed(GObject *source, GAsyncResult *result, gpointer data)
{
    Operation *operation = data;
    AppState *state = g_object_get_data(G_OBJECT(operation->window), "chronoledger-state");
    g_assert_true(state->shell.file_dialog_pending);
    g_object_weak_ref(G_OBJECT(result), released, &task_releases);
    ++completions;
    g_print("STUDY native callback completed\n");
    operation->callback(source, result, operation->user_data);
    g_assert_false(state->shell.file_dialog_pending);
    g_assert_false(state->shell.close_after_file_dialog);
    if (g_getenv("CHOOSER_STUDY_SAVE_FAILURE")) {
        g_assert_false(state->shell.closing);
        g_assert_cmpint(state->session.save_state, ==, SAVE_STATE_FAILED);
        g_assert_nonnull(state->session.save_error);
        g_assert_nonnull(state->shell.modal_overlay);
        g_print("STUDY deferred close retained save-failure veto and error panel\n");
        /* The runner replaced only its own temporary primary file with this
         * empty directory after startup. Restore writability, then retry. */
        g_assert_cmpint(g_rmdir(g_getenv("CHRONOLEDGER_FILE")), ==, 0);
        gtk_window_close(operation->window);
        assert_closed(operation->window);
    } else if (g_str_equal(g_getenv("CHOOSER_STUDY_PHASE"), "normal")) {
        g_assert_false(state->shell.closing);
        if (g_getenv("CHOOSER_STUDY_SELECTION") &&
            g_str_equal(g_getenv("CHOOSER_STUDY_OPERATION"), "import")) {
            g_assert_nonnull(state->shell.modal_overlay);
            GtkWidget *replace = find_button(state->shell.modal_overlay, "Replace");
            g_assert_nonnull(replace);
            g_signal_emit_by_name(replace, "clicked");
            g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, close_after_confirmation,
                           g_object_ref(operation->window), g_object_unref);
        } else {
            g_assert_null(state->shell.modal_overlay);
            gtk_window_close(operation->window);
            assert_closed(operation->window);
        }
    } else {
        assert_closed(operation->window);
    }
    g_object_unref(operation->window);
    g_free(operation);
}

static Operation *begin(GtkFileDialog *dialog, GtkWindow *parent,
                        GAsyncReadyCallback callback, gpointer data)
{
    g_assert_nonnull(g_getenv("CHRONOLEDGER_FILE"));
    g_assert_cmpstr(g_application_get_application_id(G_APPLICATION(
        gtk_window_get_application(parent))), ==,
        "io.github.chronoledger.ChronoLedger.Development");
    g_assert_cmpuint(completions, ==, 0);
    AppState *state = g_object_get_data(G_OBJECT(parent), "chronoledger-state");
    g_assert_true(state->shell.file_dialog_pending);
    g_object_weak_ref(G_OBJECT(parent), released, &window_releases);
    g_object_weak_ref(G_OBJECT(state->shell.gl_area), released, &area_releases);
    g_object_weak_ref(G_OBJECT(dialog), released, &dialog_releases);
    char *title = g_strdup_printf("ChronoLedger chooser test %d", getpid());
    gtk_file_dialog_set_title(dialog, title);
    g_free(title);
    const char *selection = g_getenv("CHOOSER_STUDY_SELECTION");
    if (selection != NULL) {
        GFile *file = g_file_new_for_path(selection);
        GFile *folder = g_file_get_parent(file);
        char *name = g_file_get_basename(file);
        gtk_file_dialog_set_initial_folder(dialog, folder);
        gtk_file_dialog_set_initial_name(dialog, name);
        if (g_file_query_exists(file, NULL))
            gtk_file_dialog_set_initial_file(dialog, file);
        g_free(name);
        g_object_unref(folder);
        g_object_unref(file);
    }
    Operation *operation = g_new0(Operation, 1);
    operation->window = g_object_ref(parent);
    operation->callback = callback;
    operation->user_data = data;
    return operation;
}

static void opened(GtkWindow *parent)
{
    if (g_str_equal(g_getenv("CHOOSER_STUDY_PHASE"), "initializing"))
        close_source = g_idle_add_full(G_PRIORITY_HIGH, request_close,
                                      g_object_ref(parent), g_object_unref);
    else if (!g_str_equal(g_getenv("CHOOSER_STUDY_PHASE"), "normal"))
        close_source = g_unix_signal_add_full(G_PRIORITY_DEFAULT, SIGUSR1,
            request_close, g_object_ref(parent), g_object_unref);
    g_print("STUDY native chooser initiated\n");
}

void gtk_file_dialog_open(GtkFileDialog *dialog, GtkWindow *parent,
                         GCancellable *cancel, GAsyncReadyCallback callback, gpointer data)
{
    static void (*real_open)(GtkFileDialog *, GtkWindow *, GCancellable *, GAsyncReadyCallback, gpointer);
    if (!real_open) real_open = dlsym(RTLD_NEXT, "gtk_file_dialog_open");
    Operation *operation = begin(dialog, parent, callback, data);
    real_open(dialog, parent, cancel, completed, operation);
    opened(parent);
}

void gtk_file_dialog_save(GtkFileDialog *dialog, GtkWindow *parent,
                         GCancellable *cancel, GAsyncReadyCallback callback, gpointer data)
{
    static void (*real_save)(GtkFileDialog *, GtkWindow *, GCancellable *, GAsyncReadyCallback, gpointer);
    if (!real_save) real_save = dlsym(RTLD_NEXT, "gtk_file_dialog_save");
    Operation *operation = begin(dialog, parent, callback, data);
    real_save(dialog, parent, cancel, completed, operation);
    opened(parent);
}

static void consumed(GFile *file, GError **error)
{
    ++results_consumed;
    if (g_getenv("CHOOSER_STUDY_SELECTION") != NULL) {
        g_assert_nonnull(file);
        g_assert_no_error(*error);
        char *path = g_file_get_path(file);
        g_assert_cmpstr(path, ==, g_getenv("CHOOSER_STUDY_SELECTION"));
        g_free(path);
        g_object_weak_ref(G_OBJECT(file), released, &file_releases);
        g_print("STUDY selected file result consumed\n");
    } else {
        g_assert_null(file);
        g_assert_nonnull(*error);
        g_assert_true(g_error_matches(*error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED));
        g_print("STUDY native dismissal result consumed\n");
    }
}

GFile *gtk_file_dialog_open_finish(GtkFileDialog *dialog, GAsyncResult *result, GError **error)
{
    static GFile *(*real_finish)(GtkFileDialog *, GAsyncResult *, GError **);
    if (!real_finish) real_finish = dlsym(RTLD_NEXT, "gtk_file_dialog_open_finish");
    GFile *file = real_finish(dialog, result, error);
    consumed(file, error);
    return file;
}

GFile *gtk_file_dialog_save_finish(GtkFileDialog *dialog, GAsyncResult *result, GError **error)
{
    static GFile *(*real_finish)(GtkFileDialog *, GAsyncResult *, GError **);
    if (!real_finish) real_finish = dlsym(RTLD_NEXT, "gtk_file_dialog_save_finish");
    GFile *file = real_finish(dialog, result, error);
    consumed(file, error);
    return file;
}

/* Observe only this process's portal traffic, to prove initialization closes
 * precede GTK submitting its request (and hence precede handle-export done). */
void g_dbus_connection_send_message_with_reply(GDBusConnection *connection,
    GDBusMessage *message, GDBusSendMessageFlags flags, gint timeout_msec,
    volatile guint32 *serial, GCancellable *cancel, GAsyncReadyCallback callback, gpointer data)
{
    static void (*real_send)(GDBusConnection *, GDBusMessage *, GDBusSendMessageFlags,
        gint, volatile guint32 *, GCancellable *, GAsyncReadyCallback, gpointer);
    if (!real_send) real_send = dlsym(RTLD_NEXT, "g_dbus_connection_send_message_with_reply");
    if (g_strcmp0(g_dbus_message_get_interface(message), "org.freedesktop.portal.FileChooser") == 0) {
        ++portal_requests;
        g_print("STUDY portal chooser request submitted\n");
    }
    real_send(connection, message, flags, timeout_msec, serial, cancel, callback, data);
}

int g_application_run(GApplication *application, int argc, char **argv)
{
    static int (*real_run)(GApplication *, int, char **);
    if (!real_run) real_run = dlsym(RTLD_NEXT, "g_application_run");
    int status = real_run(application, argc, argv);
    g_assert_cmpint(status, ==, 0);
    g_assert_cmpuint(close_source, ==, 0);
    g_assert_cmpuint(completions, ==, 1);
    g_assert_cmpuint(results_consumed, ==, 1);
    g_assert_cmpuint(file_releases, ==, g_getenv("CHOOSER_STUDY_SELECTION") ? 1 : 0);
    g_assert_cmpuint(window_releases, ==, 1);
    g_assert_cmpuint(area_releases, ==, 1);
    g_assert_cmpuint(dialog_releases, ==, 1);
    g_assert_cmpuint(task_releases, ==, 1);
    g_print("STUDY PASS: callback, window, GL area, dialog and task released; application exited\n");
    return status;
}
