#pragma once

#include <glib.h>

#include "ui_control_types.h"

#ifndef CHRONOLEDGER_VERSION
#define CHRONOLEDGER_VERSION "development"
#endif

#define CHRONO_WINDOW_MIN_WIDTH 360
#define CHRONO_WINDOW_MIN_HEIGHT 240
#define CHRONO_TITLEBAR_HEIGHT 44.0
#define CHRONO_FULL_WIDTH 1280
#define CHRONO_FULL_HEIGHT 700
#define CHRONO_COMPACT_WIDTH 760
#define CHRONO_COMPACT_HEIGHT 480
#define CHRONO_MINIMAL_WIDTH 560
#define CHRONO_MINIMAL_HEIGHT 360
#define CHRONO_SLIDER_COUNT 5
#define CHRONO_PALETTE_COUNT 12
#define CHRONO_FONT_MODE_COUNT 2
#define CHRONO_AA_MODE_COUNT 4
#define CHRONO_MODE_COUNT 3
#define CHRONO_MAX_SURFACE_BYTES ((gsize)384 * 1024 * 1024)
#define CHRONO_FONT_CACHE_SLOTS 128
#define CHRONO_CURSOR_TRAIL_SAMPLES 4096
#define CHRONO_CURSOR_EVENT_QUEUE 512
#define CHRONO_CURSOR_GLOW_PADDING_PX 18
/* Cursor phosphor geometry and persistence constants. */
#define CHRONO_REFERENCE_CURSOR_PERSISTENCE 0.27f
#define CHRONO_REFERENCE_CURSOR_SAMPLE_SPACING 2.0
#define CHRONO_REFERENCE_CURSOR_FADE_EXPONENT 4.1
#define CHRONO_REFERENCE_CURSOR_TRAIL_ALPHA 0.42
#define CHRONO_REFERENCE_CURSOR_TRAIL_SHADOW_WIDTH 6.2
#define CHRONO_REFERENCE_CURSOR_TRAIL_CORE_WIDTH 3.6
#define CHRONO_REFERENCE_CURSOR_CROSS_SHADOW_WIDTH 5.2
#define CHRONO_REFERENCE_CURSOR_CROSS_CORE_WIDTH 3.0
#define CHRONO_REFERENCE_CURSOR_CROSS_INNER 2.2
#define CHRONO_REFERENCE_CURSOR_CROSS_OUTER 9.0
#define CHRONO_REFERENCE_CURSOR_TRAIL_R 0.62f
#define CHRONO_REFERENCE_CURSOR_TRAIL_G 1.00f
#define CHRONO_REFERENCE_CURSOR_TRAIL_B 0.78f
#define CHRONO_REFERENCE_CURSOR_CROSS_R 0.70f
#define CHRONO_REFERENCE_CURSOR_CROSS_G 1.00f
#define CHRONO_REFERENCE_CURSOR_CROSS_B 0.84f
#define CHRONO_CURVATURE_STRENGTH 0.32f
#define CHRONO_GAUGE_EDGE_FEATHER_PX 1.15
#define CHRONO_GAUGE_SEGMENT_LENGTH_PX 1.10
#define CHRONO_GAUGE_MAX_SEGMENTS 2048
#define CHRONO_BACKGROUND_CLOCK_INTERVAL_MS 1000
#define CHRONO_UPLOAD_PBO_COUNT 3
#define CHRONO_VISIBLE_QUEUE_ROWS 6
#define CHRONO_VISIBLE_LEDGER_ROWS 6
#define CHRONO_PRESENTATION_MODE_COUNT 4

typedef struct {
    double x;
    double y;
    double width;
    double height;
} Rect;

typedef enum {
    FONT_MONO,
    FONT_SANS,
} FontMode;

typedef enum {
    AA_OFF,
    AA_FXAA,
    AA_FXAA_HIGH,
    AA_FXAA_ULTRA,
} AntiAliasingMode;

typedef enum {
    PALETTE_GREEN,
    PALETTE_AMBER,
    PALETTE_ICE,
    PALETTE_VECTOR,
    PALETTE_AQUA,
    PALETTE_SUNSET,
    PALETTE_PLASMA,
    PALETTE_RADAR,
    PALETTE_AURORA,
    PALETTE_CYBER,
    PALETTE_INFERNO,
    PALETTE_GALAXY,
} Palette;

typedef enum {
    SLIDER_NONE = -1,
    SLIDER_GLOW,
    SLIDER_PERSISTENCE,
    SLIDER_CURSOR_PERSISTENCE,
    SLIDER_VIGNETTE,
    SLIDER_MASK,
} SliderId;

typedef enum {
    CHRONO_MODE_FOCUS,
    CHRONO_MODE_LEDGER,
    CHRONO_MODE_CALIBRATION,
} ChronoMode;

typedef enum {
    UI_DENSITY_FULL,
    UI_DENSITY_COMPACT,
    UI_DENSITY_MINIMAL,
    UI_DENSITY_TOO_SMALL,
} UiDensity;

typedef enum {
    FOCUS_COMPOSITION_DESKTOP,
    FOCUS_COMPOSITION_WIDE_COMPACT,
    FOCUS_COMPOSITION_STACKED,
    FOCUS_COMPOSITION_MINIMAL,
} FocusComposition;

typedef enum {
    PRESENTATION_BALANCED,
    PRESENTATION_AUTHENTIC,
    PRESENTATION_READABLE,
    PRESENTATION_OFF,
} PresentationMode;

typedef enum {
    EDIT_NONE,
    EDIT_TITLE,
    EDIT_DURATION,
} EditTarget;

typedef enum {
    SAVE_STATE_SAVED,
    SAVE_STATE_FAILED,
} SaveState;

typedef enum {
    ANNUNCIATOR_INFO,
    ANNUNCIATOR_SUCCESS,
    ANNUNCIATOR_CAUTION,
    ANNUNCIATOR_FAILURE,
} AnnunciatorKind;

typedef struct {
    float glow;
    float persistence;
    float cursor_persistence;
    float vignette;
    float mask_strength;
    AntiAliasingMode aa_mode;
    Palette palette;
} ShaderSettings;

typedef struct {
    double x;
    double y;
    gint64 timestamp_us;
} CursorTrailPoint;

typedef struct {
    double x;
    double y;
    gint64 timestamp_us;
} CursorMotionPoint;

typedef struct {
    UiDensity density;
    FocusComposition focus_composition;
    guint visible_queue_rows;
    guint visible_ledger_rows;
    gboolean show_add_form;
    gboolean add_form_stacked;
    gboolean show_ledger_chart;
    gboolean show_advanced_calibration;

    Rect titlebar;
    Rect title_drag;
    Rect window_resize;
    Rect modes[CHRONO_MODE_COUNT];

    Rect status_rail;
    Rect save_status;
    Rect annunciator;

    Rect focus_chamber;
    Rect focus_dial;
    Rect focus_details;
    Rect focus_primary;
    Rect focus_secondary;
    Rect focus_add_5;
    Rect focus_add_15;
    Rect queue_region;
    Rect queue_rows[CHRONO_VISIBLE_QUEUE_ROWS];
    Rect queue_primary[CHRONO_VISIBLE_QUEUE_ROWS];
    Rect queue_done[CHRONO_VISIBLE_QUEUE_ROWS];
    Rect queue_delete[CHRONO_VISIBLE_QUEUE_ROWS];
    Rect add_region;
    Rect title_input;
    Rect duration_input;
    Rect add_button;

    Rect ledger_summary;
    Rect ledger_chart;
    Rect ledger_region;
    Rect ledger_rows[CHRONO_VISIBLE_LEDGER_ROWS];
    Rect ledger_delete[CHRONO_VISIBLE_LEDGER_ROWS];
    Rect import_button;
    Rect export_button;
    Rect clear_button;

    Rect presentation_button[CHRONO_PRESENTATION_MODE_COUNT];
    Rect slider_track[CHRONO_SLIDER_COUNT];
    Rect mask_checkbox;
    Rect curvature_checkbox;
    Rect cursor_shader_effects_toggle;
    Rect notifications_toggle;
    Rect sound_toggle;
    Rect aa_button[CHRONO_AA_MODE_COUNT];
    Rect palette_button[CHRONO_PALETTE_COUNT];
    Rect reset_button;
    double safe_inset;
} UiLayout;
