#pragma once

#include "app_state.h"

typedef struct _AppViewSnapshot AppViewSnapshot;

typedef enum {
    UI_FRAME_PREPARE_OK,
    UI_FRAME_PREPARE_SURFACE_ALLOCATION_FAILED,
    UI_FRAME_PREPARE_STATIC_LAYER_FAILED,
    UI_FRAME_PREPARE_COMPOSITION_FAILED,
    UI_FRAME_PREPARE_DYNAMIC_LAYER_FAILED,
} UiFramePrepareStatus;

void ui_update_layout(AppState *state, int logical_width, int logical_height);
UiFramePrepareStatus ui_prepare_frame(AppState *state,
                                      const AppViewSnapshot *view);
const char *ui_frame_prepare_status_message(UiFramePrepareStatus status);

void ui_invalidate_static(AppState *state);
void ui_invalidate_dynamic(AppState *state);
gboolean ui_ensure_surfaces(AppState *state);
void ui_release_resources(AppState *state);
gboolean ui_render_static_layer(AppState *state);
gboolean ui_compose_static_layer(AppState *state);
gboolean ui_render_dynamic_layer(AppState *state,
                                 const AppViewSnapshot *view);
void ui_handle_press(AppState *state,
                     double x,
                     double y,
                     guint button,
                     GtkGestureClick *gesture);
void ui_handle_release(AppState *state, double x, double y, guint button);
void ui_handle_motion(AppState *state, double x, double y);
gboolean ui_get_hover_highlight(const AppState *state,
                                Rect *rect_out,
                                gboolean *danger_out);
void ui_handle_scroll(AppState *state, double dy);
