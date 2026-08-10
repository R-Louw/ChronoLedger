#pragma once

#include "app_state.h"

void app_show_diagnostic(AppState *state, const char *message, gboolean is_error);
void app_hide_diagnostic(AppState *state);
void app_refresh_power_policy(AppState *state);
gboolean app_save_state(AppState *state);
void app_set_annunciator(AppState *state,
                         AnnunciatorKind kind,
                         const char *message);
void app_notify_expired(AppState *state, ChronoTask *task);
void app_begin_edit(AppState *state, EditTarget target);
void app_finish_edit(AppState *state);
void app_request_import(AppState *state);
void app_request_export(AppState *state);
void app_request_clear_completed(AppState *state);
void app_request_delete_task(AppState *state, const char *task_id);
void app_retry_save(AppState *state);
void app_clamp_view_offsets(AppState *state);
