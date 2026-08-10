#pragma once

#include "app_state.h"

void app_input_install(AppState *state);
gboolean app_input_process_pending(AppState *state);
