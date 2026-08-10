#include "app_actions.h"
#include "app.h"
#include "ui.h"
#include "renderer.h"
#include "time_parse.h"
#include "cursor_state.h"
#include "shader_profile.h"

#include <math.h>
#include <string.h>

void
app_clamp_view_offsets(AppState *state)
{
    if (state == NULL || app_store_const(state) == NULL)
        return;

    guint open_count = 0;
    guint done_count = 0;
    const ChronoStore *store = app_store_const(state);
    guint task_count = chrono_store_get_task_count(store);
    for (guint i = 0; i < task_count; ++i) {
        const ChronoTask *task = chrono_store_get_task_at_const(store, i);
        if (task == NULL)
            continue;
        if (chrono_task_get_status(task) == CHRONO_TASK_DONE)
            ++done_count;
        else
            ++open_count;
    }

    guint visible_queue = MAX(state->presentation.layout.visible_queue_rows, 1u);
    guint visible_ledger = MAX(state->presentation.layout.visible_ledger_rows, 1u);
    guint max_queue_offset = open_count > visible_queue
        ? open_count - visible_queue
        : 0u;
    guint max_ledger_offset = done_count > visible_ledger
        ? done_count - visible_ledger
        : 0u;

    guint previous_queue = state->session.queue_offset;
    guint previous_ledger = state->session.ledger_offset;
    state->session.queue_offset = MIN(state->session.queue_offset,
                                      max_queue_offset);
    state->session.ledger_offset = MIN(state->session.ledger_offset,
                                       max_ledger_offset);
    if (state->session.queue_offset != previous_queue ||
        state->session.ledger_offset != previous_ledger)
        ui_invalidate_dynamic(state);
}

static ChronoTask *
focused_task(AppState *state)
{
    if (state == NULL || app_store(state) == NULL)
        return NULL;
    ChronoTask *task = chrono_store_get_active_task(app_store(state));
    if (task == NULL)
        task = chrono_store_first_expired_task(app_store(state));
    if (task == NULL && state->session.selected_task_id != NULL)
        task = chrono_store_find_task(app_store(state), state->session.selected_task_id);
    return task;
}

static void
select_task(AppState *state, const char *task_id)
{
    if (state == NULL)
        return;
    if (g_strcmp0(state->session.selected_task_id, task_id) == 0)
        return;
    g_free(state->session.selected_task_id);
    state->session.selected_task_id = task_id != NULL ? g_strdup(task_id) : NULL;
    ui_invalidate_dynamic(state);
}

static void
report_task_error(AppState *state, const char *prefix, GError *error)
{
    char *message = g_strdup_printf("%s%s%s",
                                    prefix,
                                    error != NULL ? ": " : "",
                                    error != NULL ? error->message : "");
    app_set_annunciator(state, ANNUNCIATOR_FAILURE, message);
    g_free(message);
    g_clear_error(&error);
}

static gboolean
start_or_pause_task(AppState *state,
                    ChronoTask *task,
                    gboolean announce_success)
{
    if (state == NULL || app_store(state) == NULL || task == NULL)
        return FALSE;

    gint64 now_us = g_get_monotonic_time();
    if (chrono_task_get_status(task) == CHRONO_TASK_ACTIVE) {
        if (!chrono_store_pause_active(app_store(state), now_us)) {
            app_set_annunciator(state, ANNUNCIATOR_FAILURE,
                                "Could not hold inconsistent allocation");
            return FALSE;
        }
        if (announce_success)
            app_set_annunciator(state, ANNUNCIATOR_INFO, "Allocation held");
    } else {
        GError *error = NULL;
        if (!chrono_store_start_task(app_store(state), chrono_task_get_id(task), now_us, &error)) {
            report_task_error(state, "Could not engage allocation", error);
            return FALSE;
        }
        select_task(state, chrono_task_get_id(task));
        if (announce_success)
            app_set_annunciator(state, ANNUNCIATOR_SUCCESS,
                                "Allocation engaged");
    }

    app_save_state(state);
    return TRUE;
}

static gboolean
complete_task(AppState *state, ChronoTask *task)
{
    if (state == NULL || app_store(state) == NULL || task == NULL)
        return FALSE;

    char *task_id = g_strdup(chrono_task_get_id(task));
    GError *error = NULL;
    if (!chrono_store_complete_task(app_store(state), task_id,
                                    g_get_monotonic_time(), &error)) {
        report_task_error(state, "Could not complete allocation", error);
        g_free(task_id);
        return FALSE;
    }

    if (g_strcmp0(state->session.selected_task_id, task_id) == 0)
        g_clear_pointer(&state->session.selected_task_id, g_free);
    g_free(task_id);
    app_clamp_view_offsets(state);
    if (app_save_state(state))
        app_set_annunciator(state, ANNUNCIATOR_SUCCESS,
                            "Allocation committed to the ledger");
    return TRUE;
}

static gboolean
add_task_time(AppState *state, ChronoTask *task, gint64 seconds)
{
    if (state == NULL || app_store(state) == NULL || task == NULL)
        return FALSE;

    GError *error = NULL;
    if (!chrono_store_add_time(app_store(state), chrono_task_get_id(task), seconds, &error)) {
        report_task_error(state, "Could not extend allocation", error);
        return FALSE;
    }
    if (app_save_state(state)) {
        app_set_annunciator(
            state,
            ANNUNCIATOR_SUCCESS,
            seconds == 300 ? "Added five minutes" : "Added fifteen minutes");
    }
    return TRUE;
}

static void
commit_new_task(AppState *state)
{
    gint64 seconds = 0;
    if (state->session.draft_title == NULL || *g_strstrip(state->session.draft_title) == '\0') {
        g_free(state->session.validation_message);
        state->session.validation_message = g_strdup("Enter a task title");
        app_begin_edit(state, EDIT_TITLE);
        return;
    }
    if (!chrono_parse_duration_seconds(state->session.draft_duration, &seconds)) {
        g_free(state->session.validation_message);
        state->session.validation_message = g_strdup("Use 25m, 1h, or 1h30m");
        app_begin_edit(state, EDIT_DURATION);
        return;
    }
    if (app_store(state) == NULL ||
        chrono_store_get_task_count(app_store(state)) >= CHRONO_STORE_MAX_TASKS) {
        g_free(state->session.validation_message);
        state->session.validation_message = g_strdup("Ledger task limit reached");
        ui_invalidate_dynamic(state);
        return;
    }

    ChronoTask *task = chrono_task_new(state->session.draft_title,
                                       seconds,
                                       g_get_real_time() / G_USEC_PER_SEC);
    if (task == NULL) {
        g_free(state->session.validation_message);
        state->session.validation_message = g_strdup("Title is invalid or too long");
        ui_invalidate_dynamic(state);
        return;
    }
    if (!chrono_store_add_task(app_store(state), task)) {
        chrono_task_free(task);
        g_free(state->session.validation_message);
        state->session.validation_message = g_strdup(
            "Ledger state is inconsistent; the allocation was not added");
        ui_invalidate_dynamic(state);
        return;
    }

    select_task(state, chrono_task_get_id(task));
    g_clear_pointer(&state->session.draft_title, g_free);
    g_clear_pointer(&state->session.draft_duration, g_free);
    state->session.draft_title = g_strdup("");
    state->session.draft_duration = g_strdup("");
    g_clear_pointer(&state->session.validation_message, g_free);
    app_finish_edit(state);
    if (app_save_state(state))
        app_set_annunciator(state, ANNUNCIATOR_SUCCESS, "Allocation committed");
}

static void
select_adjacent_open_task(AppState *state, int direction)
{
    if (state == NULL || app_store(state) == NULL)
        return;
    guint count = 0;
    gint selected = -1;
    ChronoStore *store = app_store(state);
    guint task_count = chrono_store_get_task_count(store);
    for (guint i = 0; i < task_count; ++i) {
        ChronoTask *task = chrono_store_get_task_at(store, i);
        if (task == NULL || chrono_task_get_status(task) == CHRONO_TASK_DONE)
            continue;
        if (g_strcmp0(chrono_task_get_id(task), state->session.selected_task_id) == 0 &&
            count <= G_MAXINT)
            selected = (gint)count;
        if (count < G_MAXINT)
            ++count;
    }
    if (count == 0)
        return;

    gint next = selected < 0
        ? 0
        : CLAMP(selected + CLAMP(direction, -1, 1), 0, (gint)count - 1);
    guint visible = 0;
    for (guint i = 0; i < task_count; ++i) {
        ChronoTask *task = chrono_store_get_task_at(store, i);
        if (task == NULL || chrono_task_get_status(task) == CHRONO_TASK_DONE)
            continue;
        if ((gint)visible == next) {
            select_task(state, chrono_task_get_id(task));
            guint visible_rows = MAX(state->presentation.layout.visible_queue_rows, 1u);
            if (visible < state->session.queue_offset)
                state->session.queue_offset = visible;
            else if (state->session.queue_offset > G_MAXUINT - visible_rows ||
                     visible >= state->session.queue_offset + visible_rows)
                state->session.queue_offset = visible >= visible_rows
                    ? visible - visible_rows + 1u
                    : 0u;
            ui_invalidate_dynamic(state);
            return;
        }
        ++visible;
    }
}

static float *
slider_value(ShaderSettings *settings, SliderId slider)
{
    switch (slider) {
    case SLIDER_GLOW:
        return &settings->glow;
    case SLIDER_PERSISTENCE:
        return &settings->persistence;
    case SLIDER_CURSOR_PERSISTENCE:
        return &settings->cursor_persistence;
    case SLIDER_VIGNETTE:
        return &settings->vignette;
    case SLIDER_MASK:
        return &settings->mask_strength;
    case SLIDER_NONE:
    default:
        return NULL;
    }
}

static void
apply_presentation_profile(AppState *state, PresentationMode mode)
{
    shader_profile_begin(&state->presentation, mode);
    renderer_request_history_reset(state->renderer);
    cursor_state_clear(&state->input, state->renderer);
    ui_invalidate_static(state);
}

static void
set_slider(AppState *state, SliderId slider, double normalized_value)
{
    if (slider < 0 || slider >= CHRONO_SLIDER_COUNT ||
        !isfinite(normalized_value))
        return;
    float *value = slider_value(&state->presentation.settings, slider);
    if (value == NULL)
        return;
    *value = (float)CLAMP(normalized_value, 0.0, 1.0);
    state->presentation.presentation_mode = PRESENTATION_BALANCED;
    renderer_request_history_reset(state->renderer);
    ui_invalidate_dynamic(state);
}

static void
toggle_mask(AppState *state)
{
    if (app_store(state) == NULL)
        return;
    app_preferences(state)->phosphor_mask_enabled =
        !app_preferences(state)->phosphor_mask_enabled;
    renderer_invalidate_final(state->renderer);
    if (app_save_state(state)) {
        app_set_annunciator(
            state,
            ANNUNCIATOR_INFO,
            app_preferences(state)->phosphor_mask_enabled
                ? "PHOSPHOR MASK ENABLED"
                : "PHOSPHOR MASK DISABLED");
    }
}

static void
toggle_curvature(AppState *state)
{
    if (app_store(state) == NULL)
        return;
    app_preferences(state)->curvature_enabled = !app_preferences(state)->curvature_enabled;
    renderer_request_history_reset(state->renderer);
    renderer_invalidate_final(state->renderer);
    cursor_state_refresh_mapping(&state->input, &state->scene,
                                 app_preferences_const(state));
    if (state->input.pointer_inside)
        ui_handle_motion(state, state->input.pointer_x, state->input.pointer_y);
    if (app_save_state(state)) {
        app_set_annunciator(
            state,
            ANNUNCIATOR_INFO,
            app_preferences(state)->curvature_enabled
                ? "CURVATURE ENABLED · HQ RECONSTRUCTION"
                : "CURVATURE DISABLED");
    }
}

static void
toggle_cursor_shader_effects(AppState *state)
{
    if (app_store(state) == NULL)
        return;
    app_preferences(state)->cursor_shader_effects_enabled =
        !app_preferences(state)->cursor_shader_effects_enabled;
    cursor_state_clear(&state->input, state->renderer);
    ui_invalidate_static(state);
    if (app_save_state(state)) {
        app_set_annunciator(
            state,
            ANNUNCIATOR_INFO,
            app_preferences(state)->cursor_shader_effects_enabled
                ? "CURSOR SHADER EFFECTS ENABLED"
                : "CURSOR SHADER EFFECTS DISABLED");
    }
}

static ChronoTask *
task_for_action(AppState *state, const char *task_id)
{
    if (state == NULL || app_store(state) == NULL)
        return NULL;
    return task_id != NULL
        ? chrono_store_find_task(app_store(state), task_id)
        : focused_task(state);
}

gboolean
app_dispatch_action(AppState *state, const AppAction *action)
{
    if (state == NULL || action == NULL)
        return FALSE;

    switch (action->type) {
    case APP_ACTION_SWITCH_MODE:
        if (action->data.mode < CHRONO_MODE_FOCUS ||
            action->data.mode >= CHRONO_MODE_COUNT)
            return FALSE;
        app_finish_edit(state);
        state->presentation.active_tab = action->data.mode;
        ui_invalidate_static(state);
        return TRUE;
    case APP_ACTION_BEGIN_NEW_TASK:
        app_finish_edit(state);
        state->presentation.active_tab = CHRONO_MODE_FOCUS;
        ui_invalidate_static(state);
        if (state->presentation.layout.show_add_form)
            app_begin_edit(state, EDIT_TITLE);
        else
            app_set_annunciator(state, ANNUNCIATOR_CAUTION,
                                "Expand the tile to create an allocation");
        return TRUE;
    case APP_ACTION_BEGIN_EDIT:
        if (action->data.edit_target != EDIT_TITLE &&
            action->data.edit_target != EDIT_DURATION)
            return FALSE;
        app_begin_edit(state, action->data.edit_target);
        return TRUE;
    case APP_ACTION_FINISH_EDIT:
        app_finish_edit(state);
        return TRUE;
    case APP_ACTION_COMMIT_NEW_TASK:
        commit_new_task(state);
        return TRUE;
    case APP_ACTION_RETRY_SAVE:
        app_retry_save(state);
        return TRUE;
    case APP_ACTION_ACTIVATE_FOCUS_PRIMARY: {
        ChronoTask *task = focused_task(state);
        if (task == NULL && state->presentation.layout.show_add_form)
            app_begin_edit(state, EDIT_TITLE);
        else if (task != NULL && chrono_task_get_status(task) == CHRONO_TASK_EXPIRED)
            add_task_time(state, task, 300);
        else
            start_or_pause_task(state, task, TRUE);
        return TRUE;
    }
    case APP_ACTION_TOGGLE_FOCUSED_TASK: {
        ChronoTask *task = focused_task(state);
        if (task != NULL && chrono_task_get_status(task) != CHRONO_TASK_EXPIRED)
            start_or_pause_task(state, task, FALSE);
        return TRUE;
    }
    case APP_ACTION_COMPLETE_FOCUSED_TASK:
        complete_task(state, focused_task(state));
        return TRUE;
    case APP_ACTION_SELECT_ADJACENT_OPEN_TASK:
        select_adjacent_open_task(state, action->data.direction);
        return TRUE;
    case APP_ACTION_SELECT_TASK: {
        ChronoTask *task = task_for_action(state, action->data.task.task_id);
        if (task == NULL)
            return FALSE;
        select_task(state, chrono_task_get_id(task));
        return TRUE;
    }
    case APP_ACTION_TOGGLE_TASK: {
        ChronoTask *task = task_for_action(state, action->data.task.task_id);
        if (task == NULL)
            return FALSE;
        start_or_pause_task(state, task, TRUE);
        return TRUE;
    }
    case APP_ACTION_COMPLETE_TASK: {
        ChronoTask *task = task_for_action(state, action->data.task.task_id);
        if (task == NULL)
            return FALSE;
        complete_task(state, task);
        return TRUE;
    }
    case APP_ACTION_ADD_TASK_TIME: {
        ChronoTask *task = task_for_action(
            state, action->data.task_time.task_id);
        if (task == NULL || action->data.task_time.seconds <= 0)
            return FALSE;
        add_task_time(state, task, action->data.task_time.seconds);
        return TRUE;
    }
    case APP_ACTION_DELETE_TASK: {
        ChronoTask *task = task_for_action(state, action->data.task.task_id);
        if (task == NULL)
            return FALSE;
        app_request_delete_task(state, chrono_task_get_id(task));
        return TRUE;
    }
    case APP_ACTION_IMPORT:
        app_request_import(state);
        return TRUE;
    case APP_ACTION_EXPORT:
        app_request_export(state);
        return TRUE;
    case APP_ACTION_CLEAR_COMPLETED:
        app_request_clear_completed(state);
        return TRUE;
    case APP_ACTION_APPLY_PRESENTATION:
        if (action->data.presentation < PRESENTATION_BALANCED ||
            action->data.presentation >= CHRONO_PRESENTATION_MODE_COUNT)
            return FALSE;
        apply_presentation_profile(state, action->data.presentation);
        return TRUE;
    case APP_ACTION_SET_SLIDER:
        if (action->data.slider.id < SLIDER_GLOW ||
            action->data.slider.id >= CHRONO_SLIDER_COUNT ||
            !isfinite(action->data.slider.normalized_value))
            return FALSE;
        set_slider(state,
                   action->data.slider.id,
                   action->data.slider.normalized_value);
        return TRUE;
    case APP_ACTION_TOGGLE_MASK:
        if (app_store(state) == NULL)
            return FALSE;
        toggle_mask(state);
        return TRUE;
    case APP_ACTION_TOGGLE_CURVATURE:
        if (app_store(state) == NULL)
            return FALSE;
        toggle_curvature(state);
        return TRUE;
    case APP_ACTION_TOGGLE_CURSOR_SHADER_EFFECTS:
        if (app_store(state) == NULL)
            return FALSE;
        toggle_cursor_shader_effects(state);
        return TRUE;
    case APP_ACTION_TOGGLE_NOTIFICATIONS:
        if (app_store(state) == NULL)
            return FALSE;
        app_preferences(state)->notifications_enabled =
            !app_preferences(state)->notifications_enabled;
        app_save_state(state);
        return TRUE;
    case APP_ACTION_TOGGLE_SOUND:
        if (app_store(state) == NULL)
            return FALSE;
        app_preferences(state)->sound_enabled = !app_preferences(state)->sound_enabled;
        app_save_state(state);
        return TRUE;
    case APP_ACTION_SET_AA:
        if (action->data.aa_mode < AA_OFF ||
            action->data.aa_mode >= CHRONO_AA_MODE_COUNT)
            return FALSE;
        state->presentation.profile_transition_active = FALSE;
        state->presentation.settings.aa_mode = action->data.aa_mode;
        renderer_request_history_reset(state->renderer);
        state->presentation.presentation_mode = PRESENTATION_BALANCED;
        ui_invalidate_static(state);
        return TRUE;
    case APP_ACTION_CYCLE_AA: {
        int current = (int)state->presentation.settings.aa_mode;
        if (current < AA_OFF || current >= CHRONO_AA_MODE_COUNT)
            current = AA_OFF;
        state->presentation.profile_transition_active = FALSE;
        state->presentation.settings.aa_mode = (AntiAliasingMode)(
            (current + 1) % CHRONO_AA_MODE_COUNT);
        renderer_request_history_reset(state->renderer);
        ui_invalidate_static(state);
        return TRUE;
    }
    case APP_ACTION_SET_PALETTE:
        if (action->data.palette < PALETTE_GREEN ||
            action->data.palette >= CHRONO_PALETTE_COUNT)
            return FALSE;
        state->presentation.profile_transition_active = FALSE;
        state->presentation.settings.palette = action->data.palette;
        renderer_request_history_reset(state->renderer);
        state->presentation.presentation_mode = PRESENTATION_BALANCED;
        ui_invalidate_dynamic(state);
        return TRUE;
    case APP_ACTION_RESET_CALIBRATION:
        apply_presentation_profile(state, PRESENTATION_BALANCED);
        app_set_annunciator(state, ANNUNCIATOR_SUCCESS,
                            "Balanced calibration restored");
        return TRUE;
    case APP_ACTION_NONE:
    default:
        return FALSE;
    }
}
