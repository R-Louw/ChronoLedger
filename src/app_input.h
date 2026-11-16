#pragma once

#include "app_state.h"

void app_input_install(AppState *state);
gboolean app_input_process_pending(AppState *state);

void app_input_update_cursor(AppState *state, gboolean rendered);
