#include "ui.h"
#include "app.h"
#include "renderer.h"
#include "app_actions.h"
#include "app_view.h"
#include "ui_controls.h"
#include "ui_layout.h"
#include "ui_painter.h"
#include "ui_views.h"
#include "time_parse.h"

#include <math.h>
#include <pango/pangocairo.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846

void
ui_update_layout(AppState *state, int logical_width, int logical_height)
{
    g_return_if_fail(state != NULL);

    state->scene.logical_width = logical_width;
    state->scene.logical_height = logical_height;
    if (logical_width <= 0 || logical_height <= 0)
        return;

    if (ui_layout_compute(&state->presentation.layout,
                          logical_width, logical_height))
        state->scene.static_dirty = TRUE;
}

UiFramePrepareStatus
ui_prepare_frame(AppState *state,
                 const AppViewSnapshot *view)
{
    g_return_val_if_fail(state != NULL, UI_FRAME_PREPARE_SURFACE_ALLOCATION_FAILED);
    g_return_val_if_fail(view != NULL, UI_FRAME_PREPARE_DYNAMIC_LAYER_FAILED);

    if (state->scene.logical_width <= 0 || state->scene.logical_height <= 0)
        return UI_FRAME_PREPARE_OK;

    if (!ui_ensure_surfaces(state))
        return UI_FRAME_PREPARE_SURFACE_ALLOCATION_FAILED;

    gboolean static_was_dirty = state->scene.static_dirty;
    if (!ui_render_static_layer(state))
        return UI_FRAME_PREPARE_STATIC_LAYER_FAILED;
    if (static_was_dirty) {
        state->scene.scene_dirty = TRUE;
        renderer_invalidate_filtered(state->renderer);
    }

    if (!state->scene.scene_dirty)
        return UI_FRAME_PREPARE_OK;
    if (!ui_compose_static_layer(state))
        return UI_FRAME_PREPARE_COMPOSITION_FAILED;
    if (!ui_render_dynamic_layer(state, view))
        return UI_FRAME_PREPARE_DYNAMIC_LAYER_FAILED;
    return UI_FRAME_PREPARE_OK;
}

const char *
ui_frame_prepare_status_message(UiFramePrepareStatus status)
{
    switch (status) {
    case UI_FRAME_PREPARE_OK:
        return NULL;
    case UI_FRAME_PREPARE_SURFACE_ALLOCATION_FAILED:
        return "Unable to allocate safe cached Cairo surfaces for this window size.";
    case UI_FRAME_PREPARE_STATIC_LAYER_FAILED:
        return "Cairo/Pango failed while rebuilding the instrument surface.";
    case UI_FRAME_PREPARE_COMPOSITION_FAILED:
        return "Cairo failed while composing the cached instrument layer.";
    case UI_FRAME_PREPARE_DYNAMIC_LAYER_FAILED:
        return "Cairo/Pango failed while rendering live ledger state.";
    }
    return "The UI frame could not be prepared.";
}

void
ui_invalidate_static(AppState *state)
{
    state->scene.static_dirty = TRUE;
    state->scene.scene_dirty = TRUE;
    renderer_invalidate_filtered(state->renderer);
}

void
ui_invalidate_dynamic(AppState *state)
{
    state->scene.scene_dirty = TRUE;
    renderer_invalidate_filtered(state->renderer);
}
