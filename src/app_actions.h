#pragma once

#include "app_state.h"

typedef enum {
    APP_ACTION_NONE = 0,
    APP_ACTION_SWITCH_MODE,
    APP_ACTION_BEGIN_NEW_TASK,
    APP_ACTION_BEGIN_EDIT,
    APP_ACTION_FINISH_EDIT,
    APP_ACTION_COMMIT_NEW_TASK,
    APP_ACTION_RETRY_SAVE,
    APP_ACTION_ACTIVATE_FOCUS_PRIMARY,
    APP_ACTION_TOGGLE_FOCUSED_TASK,
    APP_ACTION_COMPLETE_FOCUSED_TASK,
    APP_ACTION_SELECT_ADJACENT_OPEN_TASK,
    APP_ACTION_SELECT_TASK,
    APP_ACTION_TOGGLE_TASK,
    APP_ACTION_COMPLETE_TASK,
    APP_ACTION_ADD_TASK_TIME,
    APP_ACTION_DELETE_TASK,
    APP_ACTION_IMPORT,
    APP_ACTION_EXPORT,
    APP_ACTION_CLEAR_COMPLETED,
    APP_ACTION_APPLY_PRESENTATION,
    APP_ACTION_SET_SLIDER,
    APP_ACTION_TOGGLE_MASK,
    APP_ACTION_TOGGLE_CURVATURE,
    APP_ACTION_TOGGLE_CURSOR_SHADER_EFFECTS,
    APP_ACTION_TOGGLE_NOTIFICATIONS,
    APP_ACTION_TOGGLE_SOUND,
    APP_ACTION_SET_AA,
    APP_ACTION_CYCLE_AA,
    APP_ACTION_SET_PALETTE,
    APP_ACTION_RESET_CALIBRATION,
} AppActionType;

typedef struct {
    AppActionType type;
    union {
        ChronoMode mode;
        EditTarget edit_target;
        struct {
            const char *task_id;
        } task;
        struct {
            const char *task_id;
            gint64 seconds;
        } task_time;
        int direction;
        PresentationMode presentation;
        struct {
            SliderId id;
            double normalized_value;
        } slider;
        AntiAliasingMode aa_mode;
        Palette palette;
    } data;
} AppAction;

/* Returns TRUE when the action type and payload are accepted. Domain
 * failures are reported through application state and remain handled. */
gboolean app_dispatch_action(AppState *state, const AppAction *action);
