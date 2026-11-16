#pragma once

#include "app_state.h"

void app_show_alert(AppState *state, const char *title, const char *detail);
/* Keep the native chooser's parent realized until its async result arrives. */
gboolean app_defer_close_for_file_dialog(AppState *state);
