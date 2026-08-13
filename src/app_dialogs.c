#include "app_dialogs.h"
#include "app.h"
#include "chrono_persistence.h"
#include "renderer.h"

typedef struct {
    GtkWindow *window;
    char *task_id;
} DeleteConfirmData;

typedef struct {
    GtkWindow *window;
    ChronoStore *imported_store;
} ImportConfirmData;

/* Consumes candidate. The live document changes only if the replacement can
 * be persisted; otherwise the previous ledger is restored. */
static gboolean
commit_store_candidate(AppState *state, ChronoStore *candidate)
{
    if (state == NULL || state->session.document == NULL || candidate == NULL) {
        chrono_store_free(candidate);
        return FALSE;
    }

    ChronoStore *previous = chrono_document_exchange_store(state->session.document,
                                                            candidate);
    if (previous == NULL) {
        chrono_store_free(candidate);
        return FALSE;
    }

    if (app_save_state(state)) {
        chrono_store_free(previous);
        return TRUE;
    }

    ChronoStore *rejected = chrono_document_exchange_store(state->session.document,
                                                            previous);
    if (rejected == NULL) {
        chrono_store_free(previous);
        state->session.data_load_failed = TRUE;
        app_set_annunciator(state, ANNUNCIATOR_FAILURE,
                            "Internal ledger rollback failed; automatic saves are blocked");
        return FALSE;
    }
    chrono_store_free(rejected);
    return FALSE;
}

static void
configure_json_file_dialog(GtkFileDialog *dialog, const char *accept_label)
{
    GtkFileFilter *json_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(json_filter, "JSON files");
    gtk_file_filter_add_mime_type(json_filter, "application/json");
    gtk_file_filter_add_pattern(json_filter, "*.json");

    GtkFileFilter *all_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(all_filter, "All files");
    gtk_file_filter_add_pattern(all_filter, "*");

    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, json_filter);
    g_list_store_append(filters, all_filter);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    gtk_file_dialog_set_default_filter(dialog, json_filter);
    gtk_file_dialog_set_accept_label(dialog, accept_label);
    gtk_file_dialog_set_modal(dialog, TRUE);

    g_object_unref(filters);
    g_object_unref(all_filter);
    g_object_unref(json_filter);
}

void
app_show_alert(AppState *state, const char *title, const char *detail)
{
    if (state == NULL || state->shell.closing)
        return;
    GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", title);
    if (detail != NULL)
        gtk_alert_dialog_set_detail(dialog, detail);
    gtk_alert_dialog_show(dialog, state->shell.window);
    g_object_unref(dialog);
}

static void
on_import_confirmed(GObject *source_object,
                    GAsyncResult *result,
                    gpointer user_data)
{
    ImportConfirmData *data = user_data;
    GError *error = NULL;
    int choice = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source_object),
                                                result,
                                                &error);
    AppState *state = g_object_get_data(G_OBJECT(data->window),
                                        "chronoledger-state");
    if (error == NULL && choice == 1 && state != NULL && !state->shell.closing) {
        ChronoStore *candidate = data->imported_store;
        data->imported_store = NULL;
        guint count = chrono_store_get_task_count(candidate);
        gboolean previous_load_failure = state->session.data_load_failed;
        state->session.data_load_failed = FALSE;
        if (commit_store_candidate(state, candidate)) {
            g_clear_pointer(&state->session.selected_task_id, g_free);
            state->session.queue_offset = 0;
            state->session.ledger_offset = 0;
            renderer_request_history_reset(state->renderer);
            char *message = g_strdup_printf("Imported %u allocation%s",
                                            count,
                                            count == 1 ? "" : "s");
            app_set_annunciator(state, ANNUNCIATOR_SUCCESS, message);
            g_free(message);
        } else if (!state->session.data_load_failed) {
            /* Preserve a safety lock raised by a failed rollback.  Otherwise
             * restore the pre-import recovery state after an ordinary save
             * failure whose rollback completed successfully. */
            state->session.data_load_failed = previous_load_failure;
        }
    }

    g_clear_error(&error);
    chrono_store_free(data->imported_store);
    g_object_unref(data->window);
    g_free(data);
}

static void
on_import_file_selected(GObject *source_object,
                        GAsyncResult *result,
                        gpointer user_data)
{
    GtkWindow *window = user_data;
    AppState *state = g_object_get_data(G_OBJECT(window), "chronoledger-state");
    GError *error = NULL;
    GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source_object),
                                              result,
                                              &error);
    if (file == NULL) {
        if (error != NULL &&
            !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED) &&
            state != NULL && !state->shell.closing)
            app_show_alert(state, "Could not select import file", error->message);
        g_clear_error(&error);
        g_object_unref(window);
        return;
    }

    if (state == NULL || state->shell.closing) {
        g_object_unref(file);
        g_clear_error(&error);
        g_object_unref(window);
        return;
    }

    ChronoStore *imported = chrono_persistence_import_file(file, &error);
    g_object_unref(file);
    if (imported == NULL) {
        if (state != NULL && !state->shell.closing)
            app_show_alert(state, "Could not import ChronoLedger JSON",
                       error != NULL ? error->message : NULL);
        g_clear_error(&error);
        g_object_unref(window);
        return;
    }

    char *detail = g_strdup_printf(
        "This replaces the current local ledger with %u imported allocation%s. "
        "Any running allocation in the file opens held.",
        chrono_store_get_task_count(imported),
        chrono_store_get_task_count(imported) == 1
            ? "" : "s");
    GtkAlertDialog *dialog = gtk_alert_dialog_new("Replace current ledger?");
    gtk_alert_dialog_set_detail(dialog, detail);
    const char *buttons[] = {"Cancel", "Replace", NULL};
    gtk_alert_dialog_set_buttons(dialog, buttons);
    gtk_alert_dialog_set_cancel_button(dialog, 0);
    gtk_alert_dialog_set_default_button(dialog, 0);

    ImportConfirmData *confirm = g_new0(ImportConfirmData, 1);
    confirm->window = window;
    confirm->imported_store = imported;
    gtk_alert_dialog_choose(dialog, window, NULL,
                            on_import_confirmed, confirm);
    g_object_unref(dialog);
    g_free(detail);
}

void
app_request_import(AppState *state)
{
    g_return_if_fail(state != NULL);
    if (state->shell.closing)
        return;
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Import ChronoLedger JSON");
    configure_json_file_dialog(dialog, "Import");
    gtk_file_dialog_open(dialog,
                         state->shell.window,
                         NULL,
                         on_import_file_selected,
                         g_object_ref(state->shell.window));
    g_object_unref(dialog);
}

static void
on_export_file_selected(GObject *source_object,
                        GAsyncResult *result,
                        gpointer user_data)
{
    GtkWindow *window = user_data;
    AppState *state = g_object_get_data(G_OBJECT(window), "chronoledger-state");
    GError *error = NULL;
    GFile *file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source_object),
                                              result,
                                              &error);
    if (file == NULL) {
        if (error != NULL &&
            !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED) &&
            state != NULL && !state->shell.closing)
            app_show_alert(state, "Could not select export file", error->message);
        g_clear_error(&error);
        g_object_unref(window);
        return;
    }

    if (state == NULL || state->shell.closing) {
        g_object_unref(file);
        g_clear_error(&error);
        g_object_unref(window);
        return;
    }

    if (chrono_persistence_export_file(state->session.document,
                                       file,
                                       g_get_monotonic_time(),
                                       &error)) {
        app_set_annunciator(state, ANNUNCIATOR_SUCCESS,
                            "Ledger export complete");
    } else {
        app_show_alert(state, "Could not export ChronoLedger JSON",
                   error != NULL ? error->message : NULL);
    }
    g_clear_error(&error);
    g_object_unref(file);
    g_object_unref(window);
}

void
app_request_export(AppState *state)
{
    g_return_if_fail(state != NULL);
    if (state->shell.closing)
        return;
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Export ChronoLedger JSON");
    configure_json_file_dialog(dialog, "Export");
    gtk_file_dialog_set_initial_name(dialog, "chronoledger-export.json");
    gtk_file_dialog_save(dialog,
                         state->shell.window,
                         NULL,
                         on_export_file_selected,
                         g_object_ref(state->shell.window));
    g_object_unref(dialog);
}

static void
on_clear_confirmed(GObject *source_object,
                   GAsyncResult *result,
                   gpointer user_data)
{
    GtkWindow *window = user_data;
    AppState *state = g_object_get_data(G_OBJECT(window), "chronoledger-state");
    GError *error = NULL;
    int choice = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source_object),
                                                result,
                                                &error);
    if (error == NULL && choice == 1 && state != NULL && !state->shell.closing) {
        ChronoStore *candidate = chrono_store_copy(app_store(state));
        if (candidate == NULL) {
            app_show_alert(state, "Could not clear completion ledger",
                       "The in-memory ledger is inconsistent.");
            g_clear_error(&error);
            g_object_unref(window);
            return;
        }
        guint removed = chrono_store_clear_completed(candidate);
        if (removed == 0) {
            chrono_store_free(candidate);
        } else if (commit_store_candidate(state, candidate)) {
            state->session.ledger_offset = 0;
            char *message = g_strdup_printf("Cleared %u logged allocation%s",
                                            removed,
                                            removed == 1 ? "" : "s");
            app_set_annunciator(state, ANNUNCIATOR_SUCCESS, message);
            g_free(message);
        }
    }
    g_clear_error(&error);
    g_object_unref(window);
}

void
app_request_clear_completed(AppState *state)
{
    g_return_if_fail(state != NULL);
    if (state->shell.closing)
        return;
    guint count = 0;
    const ChronoStore *store = app_store_const(state);
    guint task_count = chrono_store_get_task_count(store);
    for (guint i = 0; i < task_count; ++i) {
        const ChronoTask *task = chrono_store_get_task_at_const(store, i);
        if (task != NULL && chrono_task_get_status(task) == CHRONO_TASK_DONE)
            ++count;
    }
    if (count == 0)
        return;

    char *detail = g_strdup_printf(
        "%u logged allocation%s will be permanently removed from this device.",
        count,
        count == 1 ? "" : "s");
    GtkAlertDialog *dialog = gtk_alert_dialog_new("Clear completion ledger?");
    gtk_alert_dialog_set_detail(dialog, detail);
    const char *buttons[] = {"Cancel", "Clear", NULL};
    gtk_alert_dialog_set_buttons(dialog, buttons);
    gtk_alert_dialog_set_cancel_button(dialog, 0);
    gtk_alert_dialog_set_default_button(dialog, 0);
    gtk_alert_dialog_choose(dialog,
                            state->shell.window,
                            NULL,
                            on_clear_confirmed,
                            g_object_ref(state->shell.window));
    g_object_unref(dialog);
    g_free(detail);
}

static void
delete_confirm_data_free(DeleteConfirmData *data)
{
    if (data == NULL)
        return;
    g_object_unref(data->window);
    g_free(data->task_id);
    g_free(data);
}

static void
on_delete_confirmed(GObject *source_object,
                    GAsyncResult *result,
                    gpointer user_data)
{
    DeleteConfirmData *data = user_data;
    AppState *state = g_object_get_data(G_OBJECT(data->window),
                                        "chronoledger-state");
    GError *error = NULL;
    int choice = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source_object),
                                                result,
                                                &error);
    if (error == NULL && choice == 1 && state != NULL && !state->shell.closing) {
        ChronoStore *candidate = chrono_store_copy(app_store(state));
        if (candidate == NULL) {
            app_show_alert(state, "Could not remove allocation",
                       "The in-memory ledger is inconsistent.");
            g_clear_error(&error);
            delete_confirm_data_free(data);
            return;
        }
        if (!chrono_store_remove_task(candidate, data->task_id)) {
            chrono_store_free(candidate);
        } else if (commit_store_candidate(state, candidate)) {
            if (g_strcmp0(state->session.selected_task_id, data->task_id) == 0)
                g_clear_pointer(&state->session.selected_task_id, g_free);
            app_clamp_view_offsets(state);
            app_set_annunciator(state, ANNUNCIATOR_SUCCESS,
                                "Allocation removed");
        }
    }
    g_clear_error(&error);
    delete_confirm_data_free(data);
}

void
app_request_delete_task(AppState *state, const char *task_id)
{
    g_return_if_fail(state != NULL);
    if (state->shell.closing)
        return;
    ChronoTask *task = chrono_store_find_task(app_store(state), task_id);
    if (task == NULL)
        return;
    char *detail = g_strdup_printf(
        "“%s” and its recorded timing data will be permanently removed.",
        chrono_task_get_title(task));
    GtkAlertDialog *dialog = gtk_alert_dialog_new("Remove allocation?");
    gtk_alert_dialog_set_detail(dialog, detail);
    const char *buttons[] = {"Cancel", "Remove", NULL};
    gtk_alert_dialog_set_buttons(dialog, buttons);
    gtk_alert_dialog_set_cancel_button(dialog, 0);
    gtk_alert_dialog_set_default_button(dialog, 0);

    DeleteConfirmData *data = g_new0(DeleteConfirmData, 1);
    data->window = g_object_ref(state->shell.window);
    data->task_id = g_strdup(task_id);
    gtk_alert_dialog_choose(dialog,
                            state->shell.window,
                            NULL,
                            on_delete_confirmed,
                            data);
    g_object_unref(dialog);
    g_free(detail);
}
