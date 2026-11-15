#include "ui.h"
#include "app_actions.h"
#include "app_routines.h"
#include "renderer.h"
#include "ui_controls.h"
#include "ui_layout.h"
#include "cursor_state.h"

#include <math.h>

static gboolean
rect_contains(Rect rect, double x, double y)
{
    return rect.width > 0.0 && rect.height > 0.0 &&
           x >= rect.x && y >= rect.y &&
           x <= rect.x + rect.width && y <= rect.y + rect.height;
}



static double slider_position(AppState *state, SliderId slider, double scene_x)
{
    Rect track = state->presentation.layout.slider_track[slider];
    if (slider == SLIDER_CURVATURE && app_preferences_const(state)->curvature_enabled)
        return cursor_state_curvature_drag_value(&state->scene, track,
            state->input.raw_pointer_x, state->input.raw_pointer_y);
    return track.width > 0 ? (scene_x - track.x) / track.width : 0;
}

static guint
count_open_tasks(const ChronoStore *store)
{
    guint count = 0;
    guint task_count = chrono_store_get_task_count(store);
    for (guint i = 0; i < task_count; ++i) {
        const ChronoTask *task = chrono_store_get_task_at_const(store, i);
        if (task != NULL && chrono_task_get_status(task) != CHRONO_TASK_DONE &&
            count < G_MAXUINT)
            ++count;
    }
    return count;
}

static guint
count_done_tasks(const ChronoStore *store)
{
    guint count = 0;
    guint task_count = chrono_store_get_task_count(store);
    for (guint i = 0; i < task_count; ++i) {
        const ChronoTask *task = chrono_store_get_task_at_const(store, i);
        if (task != NULL && chrono_task_get_status(task) == CHRONO_TASK_DONE &&
            count < G_MAXUINT)
            ++count;
    }
    return count;
}

static ChronoTask *
open_task_at(AppState *state, guint visible_index)
{
    if (visible_index > G_MAXUINT - state->session.queue_offset)
        return NULL;
    guint target = state->session.queue_offset + visible_index;
    guint found = 0;
    ChronoStore *store = app_store(state);
    guint task_count = chrono_store_get_task_count(store);
    for (guint i = 0; i < task_count; ++i) {
        ChronoTask *task = chrono_store_get_task_at(store, i);
        if (task == NULL || chrono_task_get_status(task) == CHRONO_TASK_DONE)
            continue;
        if (found++ == target)
            return task;
    }
    return NULL;
}

static ChronoTask *
done_task_at(AppState *state, guint visible_index)
{
    if (visible_index > G_MAXUINT - state->session.ledger_offset)
        return NULL;
    guint target = state->session.ledger_offset + visible_index;
    return chrono_store_get_completed_task_at(app_store(state), target);
}

static GdkToplevel *
window_toplevel(AppState *state)
{
    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(state->shell.window));
    if (surface == NULL || !GDK_IS_TOPLEVEL(surface))
        return NULL;
    return GDK_TOPLEVEL(surface);
}

static void
begin_window_move(AppState *state,
                  guint button,
                  double x,
                  double y,
                  GtkGestureClick *gesture)
{
    GdkEvent *event = gtk_event_controller_get_current_event(
        GTK_EVENT_CONTROLLER(gesture));
    GdkToplevel *toplevel = window_toplevel(state);
    if (event == NULL || toplevel == NULL)
        return;

    GdkDevice *device = gdk_event_get_device(event);
    if (device == NULL)
        return;

    gdk_toplevel_begin_move(toplevel,
                            device,
                            (int)button,
                            x,
                            y,
                            gdk_event_get_time(event));
}

static void
begin_window_resize(AppState *state,
                    guint button,
                    double x,
                    double y,
                    GtkGestureClick *gesture)
{
    GdkEvent *event = gtk_event_controller_get_current_event(
        GTK_EVENT_CONTROLLER(gesture));
    GdkToplevel *toplevel = window_toplevel(state);
    if (event == NULL || toplevel == NULL)
        return;

    GdkDevice *device = gdk_event_get_device(event);
    if (device == NULL)
        return;

    gdk_toplevel_begin_resize(toplevel,
                              GDK_SURFACE_EDGE_SOUTH_EAST,
                              device,
                              (int)button,
                              x,
                              y,
                              gdk_event_get_time(event));
}

void
ui_handle_press(AppState *state,
                double x,
                double y,
                guint button,
                GtkGestureClick *gesture)
{
    if (state == NULL || !isfinite(x) || !isfinite(y))
        return;
    state->input.pointer_x = x;
    state->input.pointer_y = y;
    if (button != GDK_BUTTON_PRIMARY)
        return;
    state->input.pointer_down = TRUE;

    UiControlRef control_ref = ui_control_hit_test(&state->presentation, &state->session, x, y);
    if (control_ref.id == UI_CONTROL_WINDOW_RESIZE) {
        begin_window_resize(state, button, state->input.raw_pointer_x,
                            state->input.raw_pointer_y, gesture);
        return;
    }
    if (rect_contains(state->presentation.layout.title_drag, x, y)) {
        begin_window_move(state, button, state->input.raw_pointer_x,
                          state->input.raw_pointer_y, gesture);
        return;
    }
    if (state->presentation.layout.density == UI_DENSITY_TOO_SMALL)
        return;

    AppAction action = {0};
    ChronoTask *task = NULL;

    switch (control_ref.id) {
    case UI_CONTROL_ROUTINE:
        app_routine_command(state, control_ref.index);
        return;
    case UI_CONTROL_MODE:
        action.type = APP_ACTION_SWITCH_MODE;
        action.data.mode = (ChronoMode)control_ref.index;
        break;
    case UI_CONTROL_SAVE_RETRY:
        action.type = APP_ACTION_RETRY_SAVE;
        break;
    case UI_CONTROL_FOCUS_PRIMARY:
        action.type = APP_ACTION_ACTIVATE_FOCUS_PRIMARY;
        break;
    case UI_CONTROL_FOCUS_SECONDARY:
        action.type = APP_ACTION_COMPLETE_FOCUSED_TASK;
        break;
    case UI_CONTROL_FOCUS_ADD_5:
    case UI_CONTROL_FOCUS_ADD_15:
        action.type = APP_ACTION_ADD_TASK_TIME;
        action.data.task_time.task_id = NULL;
        action.data.task_time.seconds =
            control_ref.id == UI_CONTROL_FOCUS_ADD_5 ? 300 : 900;
        break;
    case UI_CONTROL_QUEUE_ROW:
    case UI_CONTROL_QUEUE_PRIMARY:
    case UI_CONTROL_QUEUE_DONE:
    case UI_CONTROL_QUEUE_DELETE:
        task = open_task_at(state, control_ref.index);
        if (task == NULL)
            return;
        action.data.task.task_id = chrono_task_get_id(task);
        if (control_ref.id == UI_CONTROL_QUEUE_ROW)
            action.type = APP_ACTION_SELECT_TASK;
        else if (control_ref.id == UI_CONTROL_QUEUE_PRIMARY)
            action.type = APP_ACTION_TOGGLE_TASK;
        else if (control_ref.id == UI_CONTROL_QUEUE_DONE)
            action.type = APP_ACTION_COMPLETE_TASK;
        else
            action.type = APP_ACTION_DELETE_TASK;
        break;
    case UI_CONTROL_TITLE_INPUT:
        action.type = APP_ACTION_BEGIN_EDIT;
        action.data.edit_target = EDIT_TITLE;
        break;
    case UI_CONTROL_DURATION_INPUT:
        action.type = APP_ACTION_BEGIN_EDIT;
        action.data.edit_target = EDIT_DURATION;
        break;
    case UI_CONTROL_ADD_TASK:
        action.type = APP_ACTION_COMMIT_NEW_TASK;
        break;
    case UI_CONTROL_IMPORT:
        action.type = APP_ACTION_IMPORT;
        break;
    case UI_CONTROL_EXPORT:
        action.type = APP_ACTION_EXPORT;
        break;
    case UI_CONTROL_CLEAR_COMPLETED:
        action.type = APP_ACTION_CLEAR_COMPLETED;
        break;
    case UI_CONTROL_LEDGER_DELETE:
        task = done_task_at(state, control_ref.index);
        if (task == NULL)
            return;
        action.type = APP_ACTION_DELETE_TASK;
        action.data.task.task_id = chrono_task_get_id(task);
        break;
    case UI_CONTROL_PRESENTATION:
        action.type = APP_ACTION_APPLY_PRESENTATION;
        action.data.presentation = (PresentationMode)control_ref.index;
        break;
    case UI_CONTROL_SLIDER: {
        if (control_ref.index >= CHRONO_SLIDER_COUNT)
            return;
        Rect track = state->presentation.layout.slider_track[control_ref.index];
        if (track.width <= 0.0)
            return;
        state->input.active_slider = (SliderId)control_ref.index;
        action.type = APP_ACTION_SET_SLIDER;
        action.data.slider.id = state->input.active_slider;
        action.data.slider.normalized_value = slider_position(state, state->input.active_slider, x);
        break;
    }
    case UI_CONTROL_MASK:
        action.type = APP_ACTION_TOGGLE_MASK;
        break;
    case UI_CONTROL_CURVATURE:
        action.type = APP_ACTION_TOGGLE_CURVATURE;
        break;
    case UI_CONTROL_CURSOR_SHADER_EFFECTS:
        action.type = APP_ACTION_TOGGLE_CURSOR_SHADER_EFFECTS;
        break;
    case UI_CONTROL_NOTIFICATIONS:
        action.type = APP_ACTION_TOGGLE_NOTIFICATIONS;
        break;
    case UI_CONTROL_SOUND:
        action.type = APP_ACTION_TOGGLE_SOUND;
        break;
    case UI_CONTROL_AA:
        action.type = APP_ACTION_SET_AA;
        action.data.aa_mode = (AntiAliasingMode)control_ref.index;
        break;
    case UI_CONTROL_PALETTE:
        action.type = APP_ACTION_SET_PALETTE;
        action.data.palette = (Palette)control_ref.index;
        break;
    case UI_CONTROL_RESET_CALIBRATION:
        action.type = APP_ACTION_RESET_CALIBRATION;
        break;
    case UI_CONTROL_LEDGER_ROW:
    case UI_CONTROL_NONE:
    case UI_CONTROL_WINDOW_RESIZE:
    default:
        return;
    }

    app_dispatch_action(state, &action);
}

void
ui_handle_release(AppState *state, double x, double y, guint button)
{
    if (state == NULL || !isfinite(x) || !isfinite(y))
        return;
    (void)button;
    state->input.pointer_down = FALSE;
    state->input.active_slider = SLIDER_NONE;
    ui_invalidate_dynamic(state);
}

gboolean
ui_get_hover_highlight(const AppState *state,
                       Rect *rect_out,
                       gboolean *danger_out)
{
    if (state == NULL || !state->input.pointer_inside ||
        state->input.hover_control.id == UI_CONTROL_NONE)
        return FALSE;
    return ui_control_get_bounds(&state->presentation, &state->session,
                                 state->input.hover_control,
                                 rect_out, danger_out);
}

void
ui_handle_motion(AppState *state, double x, double y)
{
    if (state == NULL || !isfinite(x) || !isfinite(y))
        return;
    state->input.pointer_x = x;
    state->input.pointer_y = y;

    UiControlRef control_ref = ui_control_hit_test(&state->presentation, &state->session, x, y);
    if (!ui_control_equal(control_ref, state->input.hover_control)) {
        state->input.hover_control = control_ref;
        /* Only GPU interaction geometry changes. The cached Cairo/Pango
         * scene and its full-resolution uploads remain valid. */
        renderer_invalidate_filtered(state->renderer);
    }

    if (state->input.pointer_down &&
        state->input.active_slider >= SLIDER_GLOW &&
        state->input.active_slider < CHRONO_SLIDER_COUNT) {
        Rect track = state->presentation.layout.slider_track[state->input.active_slider];
        if (track.width > 0.0) {
            AppAction action = {
                .type = APP_ACTION_SET_SLIDER,
                .data.slider = {
                    .id = state->input.active_slider,
                    .normalized_value = slider_position(state, state->input.active_slider, x),
                },
            };
            app_dispatch_action(state, &action);
        }
    }
}

void
ui_handle_scroll(AppState *state, double dy)
{
    if (state == NULL || !isfinite(dy) || fabs(dy) < 0.01)
        return;
    if (state->presentation.active_tab == CHRONO_MODE_CALIBRATION) {
        ui_layout_scroll_calibration(&state->presentation.layout,
            state->presentation.layout.calibration_scroll + dy * 38.0);
        state->input.hover_control = ui_control_none();
        ui_invalidate_dynamic(state);
        return;
    }
    guint previous_queue = state->session.queue_offset;
    guint previous_ledger = state->session.ledger_offset;
    if (state->presentation.active_tab == CHRONO_MODE_FOCUS) {
        guint count = count_open_tasks(app_store(state));
        guint visible = MAX(state->presentation.layout.visible_queue_rows, 1u);
        guint max_offset = count > visible ? count - visible : 0;
        if (dy > 0.0 && state->session.queue_offset < max_offset)
            ++state->session.queue_offset;
        else if (dy < 0.0 && state->session.queue_offset > 0)
            --state->session.queue_offset;
    } else if (state->presentation.active_tab == CHRONO_MODE_LEDGER) {
        guint count = count_done_tasks(app_store(state));
        guint visible = MAX(state->presentation.layout.visible_ledger_rows, 1u);
        guint max_offset = count > visible ? count - visible : 0;
        if (dy > 0.0 && state->session.ledger_offset < max_offset)
            ++state->session.ledger_offset;
        else if (dy < 0.0 && state->session.ledger_offset > 0)
            --state->session.ledger_offset;
    }
    if (state->session.queue_offset != previous_queue ||
        state->session.ledger_offset != previous_ledger)
        ui_invalidate_dynamic(state);
}
