#pragma once

#include <cairo.h>
#include <gtk/gtk.h>
#include <pango/pango.h>

#include "app_types.h"
#include "chrono_document.h"
#include "power_policy.h"

typedef struct _Renderer Renderer;

/* Window-system objects and lifecycle flags. Owned by the GTK main thread. */
typedef struct {
    GtkApplication *application;
    GtkWindow *window;
    GtkGLArea *gl_area;
    GtkWidget *root_overlay;
    GtkWidget *diagnostic_overlay;
    GtkLabel *diagnostic_label;
    GtkSpinner *diagnostic_spinner;
    GtkEntry *input_proxy;
    gboolean window_mapped;
    gboolean window_active;
    gboolean window_suspended;
    gboolean closing;
} AppShellState;

/* User-visible mode, layout, and CRT presentation state. */
typedef struct {
    ShaderSettings settings;
    ShaderSettings profile_from;
    ShaderSettings profile_target;
    PresentationMode presentation_mode;
    gboolean profile_transition_active;
    double profile_transition_elapsed;
    double profile_transition_duration;
    UiLayout layout;
    int active_tab;
} AppPresentationState;

/* Document ownership plus controller/session state derived from it. */
typedef struct {
    ChronoDocument *document;
    char *data_path;
    SaveState save_state;
    char *save_error;
    gboolean data_load_failed;
    gint64 next_checkpoint_us;
    char *selected_task_id;
    guint queue_offset;
    guint ledger_offset;

    EditTarget edit_target;
    gboolean input_syncing;
    char *draft_title;
    char *draft_duration;
    char *validation_message;

    char *annunciator_text;
    AnnunciatorKind annunciator_kind;
    gint64 annunciator_until_us;
} AppSessionState;

/* CPU scene caches and their logical/physical dimensions. */
typedef struct {
    cairo_surface_t *static_surface;
    cairo_surface_t *scene_surface;
    cairo_surface_t *static_text_mask_surface;
    cairo_surface_t *text_mask_surface;
    cairo_t *active_text_mask_cr;
    PangoFontDescription *font_cache[CHRONO_FONT_MODE_COUNT][CHRONO_FONT_CACHE_SLOTS][2];
    int surface_width_px;
    int surface_height_px;
    int surface_scale;
    gboolean static_dirty;
    gboolean scene_dirty;
    int logical_width;
    int logical_height;
} AppSceneState;

/* Pointer sampling, hit state, and bounded cursor trail storage. */
typedef struct {
    double raw_pointer_x;
    double raw_pointer_y;
    double pointer_x;
    double pointer_y;
    gboolean pointer_inside;
    gboolean pointer_down;
    gboolean pointer_motion_pending;
    double pending_raw_pointer_x;
    double pending_raw_pointer_y;
    CursorMotionPoint cursor_motion_queue[CHRONO_CURSOR_EVENT_QUEUE];
    guint cursor_motion_head;
    guint cursor_motion_count;
    gint64 cursor_phosphor_active_until_us;
    SliderId active_slider;
    UiControlRef hover_control;

    CursorTrailPoint cursor_trail[CHRONO_CURSOR_TRAIL_SAMPLES];
    unsigned int cursor_trail_head;
    unsigned int cursor_trail_count;
    gint64 cursor_trail_last_sample_us;
    double cursor_trail_last_x;
    double cursor_trail_last_y;
} AppInputState;

/* Frame cadence, invalidation timing, and power-policy scheduling. */
typedef struct {
    double animation_time;
    double frame_delta;
    double render_interval;
    gint64 last_frame_us;
    gint64 last_render_us;
    double frame_clock_fps;
    double monitor_refresh_hz;
    gint64 last_monitor_probe_us;
    gint64 last_dynamic_second;
    int focus_progress_step;
    gint64 focus_progress_last_update_us;
    int last_caret_phase;
    gboolean annunciator_visible;
    gint64 current_render_frame_time_us;
    gint64 current_render_frame_counter;
    gint64 last_draw_frame_counter;
    int last_draw_width;
    int last_draw_height;
    guint tick_callback_id;
    guint low_power_source_id;
    ChronoPowerMode power_mode;
} AppRuntimeState;

/* Aggregate lifetime root. Modules should accept a narrower substate whenever
 * they do not need to coordinate multiple subsystems. */
typedef struct {
    AppShellState shell;
    AppPresentationState presentation;
    Renderer *renderer;
    AppSessionState session;
    AppSceneState scene;
    AppInputState input;
    AppRuntimeState runtime;
} AppState;

static inline ChronoStore *
app_store(AppState *state)
{
    return state != NULL
        ? chrono_document_get_store(state->session.document)
        : NULL;
}

static inline const ChronoStore *
app_store_const(const AppState *state)
{
    return state != NULL
        ? chrono_document_get_store_const(state->session.document)
        : NULL;
}

static inline ChronoPreferences *
app_preferences(AppState *state)
{
    return state != NULL
        ? chrono_document_get_preferences(state->session.document)
        : NULL;
}

static inline const ChronoPreferences *
app_preferences_const(const AppState *state)
{
    return state != NULL
        ? chrono_document_get_preferences_const(state->session.document)
        : NULL;
}
