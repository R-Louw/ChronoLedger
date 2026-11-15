#pragma once
#include "app_state.h"

/* Response runs synchronously on the GTK thread. Return FALSE to keep editing.
 * The panel owns data until dismissal; callbacks must not retain AppState. */
typedef gboolean (*AppModalResponse)(AppState *state, gboolean accepted, gpointer data);
void app_modal_present(AppState *state, const char *title, const char *detail,
                       GtkWidget *content, const char *accept_label,
                       gboolean destructive, AppModalResponse response,
                       gpointer data, GDestroyNotify destroy);
void app_modal_cancel(AppState *state);
void app_modal_choose(AppState *state, GtkAlertDialog *dialog,
                      GAsyncReadyCallback callback, gpointer data);

int app_modal_choose_finish(GtkAlertDialog *dialog, GAsyncResult *result, GError **error);
