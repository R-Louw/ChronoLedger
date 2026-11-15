#include "app_runtime.h"
#include "app.h"
#include "app_input.h"
#include "app_view.h"
#include "cursor_state.h"
#include "render_policy.h"
#include "renderer.h"
#include "shader_profile.h"
#include "ui.h"

#include <math.h>

#define ACTIVE_SAVE_INTERVAL_SECONDS 30

static gboolean on_tick(GtkWidget *widget,
                        GdkFrameClock *frame_clock,
                        gpointer user_data);
static gboolean on_low_power_tick(gpointer user_data);
static void update_timer_state(AppState *state, gint64 now_us);
static void update_focus_progress_geometry(AppState *state, gint64 now_us);
static void update_dynamic_ui_state(AppState *state, gint64 now_us);

static gint64
saturating_add_i64(gint64 left, gint64 right)
{
    if (right > 0 && left > G_MAXINT64 - right)
        return G_MAXINT64;
    if (right < 0 && left < G_MININT64 - right)
        return G_MININT64;
    return left + right;
}

static gint64
elapsed_us(gint64 now_us, gint64 previous_us)
{
    if (now_us <= previous_us)
        return 0;
    if (previous_us < 0 && now_us > G_MAXINT64 + previous_us)
        return G_MAXINT64;
    return now_us - previous_us;
}

static void
update_monitor_refresh(AppState *state, gint64 now_us)
{
    if (state->runtime.last_monitor_probe_us != 0 &&
        elapsed_us(now_us, state->runtime.last_monitor_probe_us) < G_USEC_PER_SEC)
        return;
    state->runtime.last_monitor_probe_us = now_us;
    GtkNative *native = gtk_widget_get_native(GTK_WIDGET(state->shell.gl_area));
    GdkSurface *surface = native != NULL ? gtk_native_get_surface(native) : NULL;
    GdkDisplay *display = surface != NULL ? gdk_surface_get_display(surface) : NULL;
    GdkMonitor *monitor = display != NULL
        ? gdk_display_get_monitor_at_surface(display, surface)
        : NULL;
    int refresh_millihz = monitor != NULL
        ? gdk_monitor_get_refresh_rate(monitor)
        : 0;
    double refresh_hz = refresh_millihz > 0 ? refresh_millihz / 1000.0 : 0.0;
    if (refresh_hz > 1.0 &&
        fabs(refresh_hz - state->runtime.monitor_refresh_hz) > 0.05) {
        state->runtime.monitor_refresh_hz = refresh_hz;
        ui_invalidate_static(state);
    }
}

static void
stop_tick(AppState *state)
{
    if (state->runtime.tick_callback_id == 0)
        return;
    gtk_widget_remove_tick_callback(GTK_WIDGET(state->shell.gl_area),
                                    state->runtime.tick_callback_id);
    state->runtime.tick_callback_id = 0;
}

static void
start_tick(AppState *state)
{
    if (state->runtime.tick_callback_id != 0 || !renderer_is_ready(state->renderer))
        return;
    state->runtime.last_frame_us = 0;
    state->runtime.last_render_us = 0;
    renderer_reset_frame_timing(state->renderer);
    state->runtime.tick_callback_id = gtk_widget_add_tick_callback(
        GTK_WIDGET(state->shell.gl_area), on_tick, state, NULL);
}

static void
stop_low_power_source(AppState *state)
{
    if (state->runtime.low_power_source_id == 0)
        return;
    g_source_remove(state->runtime.low_power_source_id);
    state->runtime.low_power_source_id = 0;
}

static ChronoPowerMode
desired_power_mode(const AppState *state)
{
    ChronoPowerInputs inputs = {
        .renderer_ready = renderer_is_ready(state->renderer),
        .window_mapped = state->shell.window_mapped,
        .window_active = state->shell.window_active,
        .window_suspended = state->shell.window_suspended,
        .timer_active = chrono_store_has_active_task(app_store_const(state)) ||
            app_preferences_const(state)->routine_count > 0,
    };
    return chrono_power_choose_mode(inputs);
}

void
app_refresh_power_policy(AppState *state)
{
    if (state == NULL || state->shell.window == NULL)
        return;
    if (state->shell.closing) {
        stop_tick(state);
        stop_low_power_source(state);
        state->runtime.power_mode = CHRONO_POWER_SLEEP;
        return;
    }

    state->shell.window_mapped = gtk_widget_get_mapped(GTK_WIDGET(state->shell.window));
    state->shell.window_active = gtk_window_is_active(state->shell.window);
#if GTK_CHECK_VERSION(4, 12, 0)
    state->shell.window_suspended = gtk_window_is_suspended(state->shell.window);
#else
    state->shell.window_suspended = !state->shell.window_mapped;
#endif

    ChronoPowerMode previous = state->runtime.power_mode;
    ChronoPowerMode desired = desired_power_mode(state);
    state->runtime.power_mode = desired;

    if (desired == CHRONO_POWER_INTERACTIVE) {
        stop_low_power_source(state);
        if (previous != CHRONO_POWER_INTERACTIVE) {
            renderer_request_history_reset(state->renderer);
            ui_invalidate_dynamic(state);
        }
        start_tick(state);
        if (renderer_is_ready(state->renderer) && state->shell.window_mapped &&
            !state->shell.window_suspended)
            gtk_gl_area_queue_render(state->shell.gl_area);
        return;
    }

    stop_tick(state);
    cursor_state_clear(&state->input, state->renderer);

    if (desired == CHRONO_POWER_BACKGROUND_CLOCK) {
        if (state->runtime.low_power_source_id == 0) {
            state->runtime.low_power_source_id = g_timeout_add_full(
                G_PRIORITY_DEFAULT,
                CHRONO_BACKGROUND_CLOCK_INTERVAL_MS,
                on_low_power_tick,
                state,
                NULL);
        }
    } else {
        stop_low_power_source(state);
    }

    if (desired != previous) {
        ui_invalidate_dynamic(state);
        if (renderer_is_ready(state->renderer) && state->shell.window_mapped &&
            !state->shell.window_suspended)
            gtk_gl_area_queue_render(state->shell.gl_area);
    }
}

static gboolean
on_low_power_tick(gpointer user_data)
{
    AppState *state = user_data;
    if (state->shell.closing ||
        state->runtime.power_mode != CHRONO_POWER_BACKGROUND_CLOCK) {
        state->runtime.low_power_source_id = 0;
        return G_SOURCE_REMOVE;
    }

    gint64 now_us = g_get_monotonic_time();
    update_timer_state(state, now_us);
    update_focus_progress_geometry(state, now_us);
    update_dynamic_ui_state(state, now_us);

    if (renderer_is_ready(state->renderer) &&
        state->shell.window_mapped && !state->shell.window_suspended &&
        (state->scene.static_dirty || state->scene.scene_dirty ||
         !renderer_output_is_valid(state->renderer)))
        gtk_gl_area_queue_render(state->shell.gl_area);

    if (desired_power_mode(state) != CHRONO_POWER_BACKGROUND_CLOCK) {
        state->runtime.low_power_source_id = 0;
        app_refresh_power_policy(state);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static void
on_window_power_state_changed(GObject *object,
                              GParamSpec *pspec,
                              gpointer user_data)
{
    (void)object;
    (void)pspec;
    app_refresh_power_policy(user_data);
}

static void
on_window_map(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    app_refresh_power_policy(user_data);
}

static void
on_window_unmap(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    app_refresh_power_policy(user_data);
}

static void
on_gl_realize(GtkGLArea *area, gpointer user_data)
{
    AppState *state = user_data;
    renderer_realize(state);
    app_refresh_power_policy(state);
    if (!renderer_is_ready(state->renderer))
        return;
    gtk_gl_area_queue_render(area);
}

static void
on_gl_unrealize(GtkGLArea *area, gpointer user_data)
{
    (void)area;
    AppState *state = user_data;
    stop_tick(state);
    stop_low_power_source(state);
    renderer_unrealize(state);
    state->runtime.power_mode = CHRONO_POWER_SLEEP;
    app_refresh_power_policy(state);
}

static gboolean
on_gl_render(GtkGLArea *area, GdkGLContext *context, gpointer user_data)
{
    (void)context;
    AppState *state = user_data;
    if (!renderer_is_ready(state->renderer))
        return TRUE;

    GdkFrameClock *frame_clock = gtk_widget_get_frame_clock(GTK_WIDGET(area));
    gint64 callback_time_us = g_get_monotonic_time();
    gint64 frame_time_us = frame_clock != NULL
        ? gdk_frame_clock_get_frame_time(frame_clock)
        : callback_time_us;
    gint64 frame_counter = frame_clock != NULL
        ? gdk_frame_clock_get_frame_counter(frame_clock)
        : saturating_add_i64(state->runtime.current_render_frame_counter, 1);
    int width = gtk_widget_get_width(GTK_WIDGET(area));
    int height = gtk_widget_get_height(GTK_WIDGET(area));
    gboolean logical_size_changed =
        width != state->scene.logical_width ||
        height != state->scene.logical_height;
    ui_update_layout(state, width, height);
    if (logical_size_changed)
        app_clamp_view_offsets(state);
    state->runtime.current_render_frame_time_us = frame_time_us;
    state->runtime.current_render_frame_counter = frame_counter;

    /* Without native ticks there is no tail cleanup. Present a settled
     * scene when asleep/backgrounded, including a queued pointer-exit frame. */
    if (state->runtime.power_mode != CHRONO_POWER_INTERACTIVE)
        renderer_request_history_reset(state->renderer);

    gboolean duplicate = state->runtime.last_draw_frame_counter >= 0 &&
                         frame_counter == state->runtime.last_draw_frame_counter &&
                         width == state->runtime.last_draw_width &&
                         height == state->runtime.last_draw_height &&
                         !state->scene.static_dirty &&
                         !state->scene.scene_dirty &&
                         renderer_frame_cache_is_valid(state->renderer);
    if (duplicate)
        return TRUE;

    AppViewSnapshot view;
    if (!app_view_snapshot_build(&view, &state->session, frame_time_us)) {
        app_show_diagnostic(state,
                            "The immutable render snapshot could not be prepared.",
                            TRUE);
        return TRUE;
    }

    UiFramePrepareStatus ui_status = ui_prepare_frame(state, &view);
    if (ui_status != UI_FRAME_PREPARE_OK) {
        app_show_diagnostic(state,
                            ui_frame_prepare_status_message(ui_status),
                            TRUE);
        /* UI preparation failures do not invalidate the GL context.  Keep the
         * renderer alive so a later resize or lower-memory frame can recover. */
        return TRUE;
    }

    Rect hover_rect = {0};
    gboolean hover_dangerous = FALSE;
    gboolean hover_visible = ui_get_hover_highlight(
        state, &hover_rect, &hover_dangerous);
    RendererFrameInput frame = {
        .presentation = &state->presentation,
        .scene = &state->scene,
        .input = &state->input,
        .view = &view,
        .frame_delta = state->runtime.frame_delta,
        .render_interval = state->runtime.render_interval,
        .frame_clock_fps = state->runtime.frame_clock_fps,
        .monitor_refresh_hz = state->runtime.monitor_refresh_hz,
        .scene_needs_upload = state->scene.scene_dirty,
        .hover_visible = hover_visible,
        .hover_rect = hover_rect,
        .hover_dangerous = hover_dangerous,
    };
    RendererFrameResult result = {0};
    unsigned int gl_error = 0;
    RendererDrawStatus draw_status = renderer_draw(
        state->renderer, &frame, &result, &gl_error);
    if (draw_status != RENDERER_DRAW_OK) {
        char *message = draw_status == RENDERER_DRAW_GL_ERROR && gl_error != 0
            ? g_strdup_printf(
                "Rendering failed with OpenGL error 0x%04x.\n"
                "See the terminal for the selected GL API and driver.",
                gl_error)
            : g_strdup(renderer_draw_status_message(draw_status));
        g_warning("%s", message);
        app_show_diagnostic(state, message, TRUE);
        g_free(message);
        renderer_discard_resources(state->renderer);
        app_refresh_power_policy(state);
        return TRUE;
    }

    if (result.scene_uploaded)
        state->scene.scene_dirty = FALSE;
    state->runtime.last_draw_frame_counter = frame_counter;
    state->runtime.last_draw_width = width;
    state->runtime.last_draw_height = height;
    app_hide_diagnostic(state);
    return TRUE;
}

static void
update_timer_state(AppState *state, gint64 now_us)
{
    if (state->presentation.active_tab == CHRONO_MODE_ROUTINES &&
        app_preferences(state)->routine_count > 0 &&
        state->session.last_routine_second != g_get_real_time() / G_USEC_PER_SEC) {
        state->session.last_routine_second = g_get_real_time() / G_USEC_PER_SEC;
        ui_invalidate_dynamic(state);
    }
    if (!state->session.data_load_failed) {
        const ChronoPreferences *p = app_preferences_const(state);
        for (guint i = 0; i < p->routine_count; i++) {
            const ChronoRoutine *r = &p->routines[i];
            if (!chrono_routine_due(r, g_get_real_time() / G_USEC_PER_SEC) ||
                state->session.routine_notified_due[i] == r->due) continue;
            state->session.routine_notified_due[i] = r->due;
            app_set_annunciator(state, ANNUNCIATOR_CAUTION, r->title);
            if (p->notifications_enabled) {
                GNotification *n = g_notification_new("Routine due");
                g_notification_set_body(n, r->title);
                char *id = g_strdup_printf("chronoledger-routine-%u", i);
                g_application_send_notification(G_APPLICATION(state->shell.application), id, n);
                g_free(id);
                g_object_unref(n);
            }
        }
    }
    ChronoTask *expired = chrono_store_update_expired(app_store(state), now_us);
    if (expired != NULL) {
        state->session.next_checkpoint_us = 0;
        gboolean saved = app_save_state(state);
        app_notify_expired(state, expired);
        if (saved) {
            char *message = g_strdup_printf(
                "TIME UP · %s requires a decision", chrono_task_get_title(expired));
            app_set_annunciator(state, ANNUNCIATOR_CAUTION, message);
            g_free(message);
        }
        renderer_request_history_reset(state->renderer);
        return;
    }

    ChronoTask *active = chrono_store_get_active_task(app_store(state));
    if (active == NULL) {
        state->session.next_checkpoint_us = 0;
        return;
    }
    if (state->session.next_checkpoint_us == 0)
        state->session.next_checkpoint_us =
            saturating_add_i64(
                now_us, ACTIVE_SAVE_INTERVAL_SECONDS * G_USEC_PER_SEC);
    else if (now_us >= state->session.next_checkpoint_us) {
        app_save_state(state);
        state->session.next_checkpoint_us =
            saturating_add_i64(
                now_us, ACTIVE_SAVE_INTERVAL_SECONDS * G_USEC_PER_SEC);
    }
}

static void
update_focus_progress_geometry(AppState *state, gint64 now_us)
{
    ChronoTask *active = chrono_store_get_active_task(app_store(state));
    int next_step = -1;
    if (state->presentation.active_tab == CHRONO_MODE_FOCUS &&
        state->presentation.layout.density != UI_DENSITY_TOO_SMALL) {
        if (active != NULL) {
            next_step = chrono_progress_visual_step(
                chrono_task_progress_fraction(active, now_us));
        }
    }

    if (next_step < 0) {
        if (state->runtime.focus_progress_step >= 0) {
            state->runtime.focus_progress_step = -1;
            state->runtime.focus_progress_last_update_us = 0;
            renderer_invalidate_filtered(state->renderer);
        }
        return;
    }

    if (!chrono_progress_update_due(state->runtime.focus_progress_step,
                                    state->runtime.focus_progress_last_update_us,
                                    next_step,
                                    now_us))
        return;

    state->runtime.focus_progress_step = next_step;
    state->runtime.focus_progress_last_update_us = now_us;
    renderer_invalidate_filtered(state->renderer);
}

static void
update_dynamic_ui_state(AppState *state, gint64 now_us)
{
    ChronoTask *active = chrono_store_get_active_task(app_store(state));
    /* Only the Focus mode exposes live remaining-time text.  Ledger and
     * Calibration continue timing in the model without rebuilding an
     * unchanged Cairo/Pango scene once per second. */
    gint64 display_second = active != NULL &&
                            state->presentation.active_tab == CHRONO_MODE_FOCUS
        ? now_us / G_USEC_PER_SEC
        : -1;
    if (display_second != state->runtime.last_dynamic_second) {
        state->runtime.last_dynamic_second = display_second;
        ui_invalidate_dynamic(state);
    }

    int caret_phase = state->session.edit_target != EDIT_NONE
        ? (int)((now_us / 500000) & 1)
        : -1;
    if (caret_phase != state->runtime.last_caret_phase) {
        state->runtime.last_caret_phase = caret_phase;
        ui_invalidate_dynamic(state);
    }

    gboolean annunciator_visible = state->session.annunciator_text != NULL &&
        state->session.annunciator_text[0] != '\0' &&
        now_us < state->session.annunciator_until_us;
    if (annunciator_visible != state->runtime.annunciator_visible) {
        state->runtime.annunciator_visible = annunciator_visible;
        ui_invalidate_dynamic(state);
    }
}

static gboolean
on_tick(GtkWidget *widget, GdkFrameClock *frame_clock, gpointer user_data)
{
    AppState *state = user_data;
    if (state->shell.closing || !renderer_is_ready(state->renderer) ||
        state->runtime.power_mode != CHRONO_POWER_INTERACTIVE) {
        state->runtime.tick_callback_id = 0;
        app_refresh_power_policy(state);
        return G_SOURCE_REMOVE;
    }

    gint64 now_us = gdk_frame_clock_get_frame_time(frame_clock);
    update_monitor_refresh(state, now_us);
    update_timer_state(state, now_us);
    gboolean pointer_changed = app_input_process_pending(state);
    update_focus_progress_geometry(state, now_us);
    update_dynamic_ui_state(state, now_us);
    if (state->runtime.last_frame_us == 0) {
        state->runtime.last_frame_us = now_us;
        state->runtime.last_render_us = now_us;
        gtk_gl_area_queue_render(GTK_GL_AREA(widget));
        return G_SOURCE_CONTINUE;
    }

    state->runtime.last_frame_us = now_us;
    double clock_fps = gdk_frame_clock_get_fps(frame_clock);
    if (isfinite(clock_fps) && clock_fps > 1.0)
        state->runtime.frame_clock_fps = clock_fps;

    renderer_advance_scene_history(state->renderer, now_us);
    gboolean scene_or_cache_dirty =
        state->scene.static_dirty || state->scene.scene_dirty ||
        renderer_needs_render(state->renderer);
    gboolean trail_active = state->input.pointer_inside &&
        state->presentation.settings.cursor_persistence > 0.001f &&
        state->input.cursor_trail_count > 1;
    gboolean cursor_phosphor_cleanup =
        app_preferences(state)->cursor_shader_effects_enabled &&
        renderer_cursor_history_is_valid(state->renderer) &&
        state->input.cursor_phosphor_active_until_us > 0 &&
        now_us >= state->input.cursor_phosphor_active_until_us;
    if (cursor_phosphor_cleanup) {
        /* Queue one final native frame after the cursor-only phosphor tail has
         * expired. Without this cleanup presentation the last faint history
         * sample would remain frozen in an event-driven compositor buffer. */
        state->input.cursor_phosphor_active_until_us = 0;
        renderer_invalidate_cursor_history(state->renderer);
    }
    gboolean cursor_phosphor_active =
        app_preferences(state)->cursor_shader_effects_enabled &&
        now_us < state->input.cursor_phosphor_active_until_us;
    gboolean render_requested = chrono_render_request_needed(
        state->presentation.profile_transition_active,
        scene_or_cache_dirty, pointer_changed,
        trail_active || cursor_phosphor_active || cursor_phosphor_cleanup);
    if (!render_requested)
        return G_SOURCE_CONTINUE;

    double wall_delta = (double)elapsed_us(now_us, state->runtime.last_render_us) /
                        G_USEC_PER_SEC;
    state->runtime.last_render_us = now_us;
    state->runtime.render_interval = MAX(wall_delta, 0.0);
    state->runtime.frame_delta = CLAMP(wall_delta, 0.0, 0.1);
    state->runtime.animation_time = fmod(
        state->runtime.animation_time + state->runtime.frame_delta, 4096.0);
    ShaderProfileStep profile_step = shader_profile_advance(
        &state->presentation, state->runtime.frame_delta);
    if (profile_step.settings_changed) {
        cursor_state_refresh_mapping(&state->input, &state->scene,
            app_preferences_const(state), &state->presentation.settings);
        renderer_invalidate_final(state->renderer);
        if (profile_step.anti_aliasing_changed)
            ui_invalidate_static(state);
        if (state->presentation.active_tab == CHRONO_MODE_CALIBRATION)
            ui_invalidate_dynamic(state);
    }
    if (pointer_changed || trail_active || cursor_phosphor_active)
        cursor_state_update(&state->input,
                            &state->presentation.settings,
                            app_preferences_const(state),
                            state->renderer,
                            now_us);

    gtk_gl_area_queue_render(GTK_GL_AREA(widget));
    return G_SOURCE_CONTINUE;
}


void
app_runtime_install(AppState *state)
{
    g_return_if_fail(state != NULL);
    g_return_if_fail(state->shell.gl_area != NULL);
    g_return_if_fail(state->shell.window != NULL);
    g_signal_connect(state->shell.gl_area, "realize", G_CALLBACK(on_gl_realize), state);
    g_signal_connect(state->shell.gl_area, "unrealize", G_CALLBACK(on_gl_unrealize), state);
    g_signal_connect(state->shell.gl_area, "render", G_CALLBACK(on_gl_render), state);
    g_signal_connect(state->shell.window, "notify::is-active", G_CALLBACK(on_window_power_state_changed), state);
#if GTK_CHECK_VERSION(4, 12, 0)
    g_signal_connect(state->shell.window, "notify::suspended", G_CALLBACK(on_window_power_state_changed), state);
#endif
    g_signal_connect(state->shell.window, "map", G_CALLBACK(on_window_map), state);
    g_signal_connect(state->shell.window, "unmap", G_CALLBACK(on_window_unmap), state);
}
