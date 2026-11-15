#include "app_input.h"
#include "app_actions.h"
#include "cursor_state.h"
#include "renderer.h"
#include "ui.h"
#include "ui_control_types.h"

#include <math.h>

static void
update_pointer_now(AppState *state, double raw_x, double raw_y)
{
    if (state == NULL || state->shell.gl_area == NULL ||
        !isfinite(raw_x) || !isfinite(raw_y))
        return;

    state->scene.logical_width =
        gtk_widget_get_width(GTK_WIDGET(state->shell.gl_area));
    state->scene.logical_height =
        gtk_widget_get_height(GTK_WIDGET(state->shell.gl_area));
    state->input.raw_pointer_x = raw_x;
    state->input.raw_pointer_y = raw_y;
    double scene_x = raw_x;
    double scene_y = raw_y;
    cursor_state_map_pointer(&state->scene,
                             app_preferences_const(state),
                             raw_x, raw_y,
                             &scene_x, &scene_y);
    ui_handle_motion(state, scene_x, scene_y);
}

static void
queue_cursor_motion(AppState *state, double x, double y, gint64 timestamp_us)
{
    if (state == NULL || !isfinite(x) || !isfinite(y) || timestamp_us <= 0)
        return;

    state->input.cursor_motion_head %= CHRONO_CURSOR_EVENT_QUEUE;
    state->input.cursor_motion_count = MIN(state->input.cursor_motion_count,
                                     CHRONO_CURSOR_EVENT_QUEUE);
    if (state->input.cursor_motion_count == CHRONO_CURSOR_EVENT_QUEUE) {
        /* Preserve the newest path samples under pathological input rates. */
        state->input.cursor_motion_count--;
    }
    CursorMotionPoint *point =
        &state->input.cursor_motion_queue[state->input.cursor_motion_head];
    *point = (CursorMotionPoint){x, y, timestamp_us};
    state->input.cursor_motion_head =
        (state->input.cursor_motion_head + 1u) % CHRONO_CURSOR_EVENT_QUEUE;
    state->input.cursor_motion_count++;
}

gboolean
app_input_process_pending(AppState *state)
{
    if (!state->input.pointer_motion_pending || !state->input.pointer_inside)
        return FALSE;

    double latest_x = state->input.pending_raw_pointer_x;
    double latest_y = state->input.pending_raw_pointer_y;
    if (!isfinite(latest_x) || !isfinite(latest_y)) {
        state->input.pointer_motion_pending = FALSE;
        state->input.cursor_motion_count = 0;
        return FALSE;
    }
    state->input.cursor_motion_head %= CHRONO_CURSOR_EVENT_QUEUE;
    guint queued = MIN(state->input.cursor_motion_count,
                       CHRONO_CURSOR_EVENT_QUEUE);
    if (queued > 0) {
        guint oldest =
            (state->input.cursor_motion_head + CHRONO_CURSOR_EVENT_QUEUE - queued) %
            CHRONO_CURSOR_EVENT_QUEUE;
        for (guint i = 0; i < queued; ++i) {
            const CursorMotionPoint *point =
                &state->input.cursor_motion_queue[(oldest + i) %
                                            CHRONO_CURSOR_EVENT_QUEUE];
            if (!isfinite(point->x) || !isfinite(point->y) ||
                point->timestamp_us <= 0)
                continue;
            cursor_state_record_motion(
                &state->input, &state->presentation.settings,
                app_preferences_const(state), state->renderer,
                point->x, point->y, point->timestamp_us);
            latest_x = point->x;
            latest_y = point->y;
        }
    }

    state->input.pointer_motion_pending = FALSE;
    state->input.cursor_motion_count = 0;
    update_pointer_now(state, latest_x, latest_y);
    return TRUE;
}

static void
on_motion(GtkEventControllerMotion *controller,
          double x,
          double y,
          gpointer user_data)
{
    (void)controller;
    AppState *state = user_data;
    if (!isfinite(x) || !isfinite(y))
        return;
    gint64 now_us = g_get_monotonic_time();
    state->input.pending_raw_pointer_x = x;
    state->input.pending_raw_pointer_y = y;
    queue_cursor_motion(state, x, y, now_us);
    state->input.pointer_motion_pending = TRUE;
}

static void
on_enter(GtkEventControllerMotion *controller,
         double x,
         double y,
         gpointer user_data)
{
    (void)controller;
    AppState *state = user_data;
    if (!isfinite(x) || !isfinite(y))
        return;
    state->input.pointer_inside = TRUE;
    state->input.pointer_motion_pending = FALSE;
    state->input.cursor_motion_count = 0;
    update_pointer_now(state, x, y);
    cursor_state_record_motion(&state->input,
                               &state->presentation.settings,
                               app_preferences_const(state), state->renderer,
                               x, y, g_get_monotonic_time());
}

static void
on_leave(GtkEventControllerMotion *controller, gpointer user_data)
{
    (void)controller;
    AppState *state = user_data;
    state->input.pointer_inside = FALSE;
    state->input.pointer_down = FALSE;
    state->input.pointer_motion_pending = FALSE;
    state->input.cursor_motion_count = 0;
    state->input.active_slider = SLIDER_NONE;
    cursor_state_clear(&state->input, state->renderer);
    if (state->input.hover_control.id != UI_CONTROL_NONE)
        renderer_invalidate_filtered(state->renderer);
    state->input.hover_control = ui_control_none();

    /* Present the cleared cursor plane and any removed hover geometry. */
    if (state->shell.gl_area != NULL && renderer_is_ready(state->renderer))
        gtk_gl_area_queue_render(state->shell.gl_area);
}

static void
on_pressed(GtkGestureClick *gesture,
           int press_count,
           double x,
           double y,
           gpointer user_data)
{
    (void)press_count;
    AppState *state = user_data;
    if (!isfinite(x) || !isfinite(y))
        return;
    state->input.pointer_motion_pending = FALSE;
    state->input.cursor_motion_count = 0;
    update_pointer_now(state, x, y);
    cursor_state_record_motion(&state->input,
                               &state->presentation.settings,
                               app_preferences_const(state), state->renderer,
                               x, y, g_get_monotonic_time());
    guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
    ui_handle_press(state, state->input.pointer_x, state->input.pointer_y,
                    button, gesture);
}

static void
on_released(GtkGestureClick *gesture,
            int press_count,
            double x,
            double y,
            gpointer user_data)
{
    (void)press_count;
    AppState *state = user_data;
    if (!isfinite(x) || !isfinite(y))
        return;
    state->input.pointer_motion_pending = FALSE;
    state->input.cursor_motion_count = 0;
    update_pointer_now(state, x, y);
    cursor_state_record_motion(&state->input,
                               &state->presentation.settings,
                               app_preferences_const(state), state->renderer,
                               x, y, g_get_monotonic_time());
    guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
    ui_handle_release(state, state->input.pointer_x, state->input.pointer_y, button);
}

static gboolean
on_scroll(GtkEventControllerScroll *controller,
          double dx,
          double dy,
          gpointer user_data)
{
    (void)controller;
    if (!isfinite(dx) || !isfinite(dy))
        return TRUE;
    ui_handle_scroll(user_data, dy);
    return TRUE;
}

static gboolean
on_key_pressed(GtkEventControllerKey *controller,
               guint keyval,
               guint keycode,
               GdkModifierType modifiers,
               gpointer user_data)
{
    (void)controller;
    (void)keycode;
    AppState *state = user_data;
    AppAction action = {0};

    if (state->session.edit_target != EDIT_NONE) {
        if (keyval == GDK_KEY_Escape) {
            action.type = APP_ACTION_FINISH_EDIT;
        } else if (keyval == GDK_KEY_Tab) {
            action.type = APP_ACTION_BEGIN_EDIT;
            action.data.edit_target = state->session.edit_target == EDIT_TITLE
                ? EDIT_DURATION
                : EDIT_TITLE;
        } else {
            return FALSE;
        }
        return app_dispatch_action(state, &action);
    }

    if ((modifiers & GDK_CONTROL_MASK) != 0) {
        if (keyval == GDK_KEY_n || keyval == GDK_KEY_N)
            action.type = APP_ACTION_BEGIN_NEW_TASK;
        else if (keyval == GDK_KEY_i || keyval == GDK_KEY_I)
            action.type = APP_ACTION_IMPORT;
        else if (keyval == GDK_KEY_e || keyval == GDK_KEY_E)
            action.type = APP_ACTION_EXPORT;
        else
            return FALSE;
        return app_dispatch_action(state, &action);
    }

    switch (keyval) {
    case GDK_KEY_Escape:
        gtk_window_close(state->shell.window);
        return TRUE;
    case GDK_KEY_1:
    case GDK_KEY_2:
    case GDK_KEY_3:
        action.type = APP_ACTION_SWITCH_MODE;
        action.data.mode = (ChronoMode)(keyval - GDK_KEY_1);
        break;
    case GDK_KEY_Up:
    case GDK_KEY_Down:
        if (state->presentation.active_tab != CHRONO_MODE_FOCUS)
            return TRUE;
        action.type = APP_ACTION_SELECT_ADJACENT_OPEN_TASK;
        action.data.direction = keyval == GDK_KEY_Up ? -1 : 1;
        break;
    case GDK_KEY_space:
    case GDK_KEY_Return:
        action.type = APP_ACTION_TOGGLE_FOCUSED_TASK;
        break;
    case GDK_KEY_d:
    case GDK_KEY_D:
        action.type = APP_ACTION_COMPLETE_FOCUSED_TASK;
        break;
    case GDK_KEY_a:
    case GDK_KEY_A:
        action.type = APP_ACTION_CYCLE_AA;
        break;
    case GDK_KEY_c:
    case GDK_KEY_C:
        if (state->presentation.active_tab != CHRONO_MODE_CALIBRATION)
            return FALSE;
        action.type = APP_ACTION_TOGGLE_CURSOR_SHADER_EFFECTS;
        break;
    case GDK_KEY_r:
    case GDK_KEY_R:
        if (state->presentation.active_tab != CHRONO_MODE_CALIBRATION)
            return FALSE;
        action.type = APP_ACTION_RESET_CALIBRATION;
        break;
    default:
        return FALSE;
    }

    return app_dispatch_action(state, &action);
}

static void
hide_system_cursor(GtkWidget *widget)
{
    gtk_widget_set_cursor_from_name(widget, "none");
}


void
app_input_install(AppState *state)
{
    g_return_if_fail(state != NULL);
    g_return_if_fail(state->shell.gl_area != NULL);
    g_return_if_fail(state->shell.window != NULL);
    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_motion), state);
    g_signal_connect(motion, "enter", G_CALLBACK(on_enter), state);
    g_signal_connect(motion, "leave", G_CALLBACK(on_leave), state);
    gtk_widget_add_controller(GTK_WIDGET(state->shell.gl_area), motion);
    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
    g_signal_connect(click, "pressed", G_CALLBACK(on_pressed), state);
    g_signal_connect(click, "released", G_CALLBACK(on_released), state);
    gtk_widget_add_controller(GTK_WIDGET(state->shell.gl_area), GTK_EVENT_CONTROLLER(click));
    GtkEventController *scroll = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL | GTK_EVENT_CONTROLLER_SCROLL_DISCRETE);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_scroll), state);
    gtk_widget_add_controller(GTK_WIDGET(state->shell.gl_area), scroll);
    GtkEventController *keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key_pressed), state);
    gtk_widget_add_controller(GTK_WIDGET(state->shell.window), keys);
    hide_system_cursor(GTK_WIDGET(state->shell.gl_area));
}
