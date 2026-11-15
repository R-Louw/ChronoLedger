#include "ui.h"
#include "app_routines.h"
#include "ui_painter.h"
#include "ui_views.h"
#include "renderer.h"

#include <pango/pangocairo.h>

void
ui_release_resources(AppState *state)
{
    for (int font_slot = 0; font_slot < CHRONO_FONT_MODE_COUNT; ++font_slot) {
        for (int size_slot = 0; size_slot < CHRONO_FONT_CACHE_SLOTS; ++size_slot) {
            for (int weight_slot = 0; weight_slot < 2; ++weight_slot) {
                g_clear_pointer(&state->scene.font_cache[font_slot][size_slot][weight_slot],
                                pango_font_description_free);
            }
        }
    }
    state->scene.active_text_mask_cr = NULL;
    g_clear_pointer(&state->scene.static_surface, cairo_surface_destroy);
    g_clear_pointer(&state->scene.scene_surface, cairo_surface_destroy);
    g_clear_pointer(&state->scene.static_text_mask_surface, cairo_surface_destroy);
    g_clear_pointer(&state->scene.text_mask_surface, cairo_surface_destroy);
}

static cairo_surface_t *
create_scene_surface(int width_px, int height_px, int scale)
{
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                           width_px,
                                                           height_px);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return NULL;
    }
    cairo_surface_set_device_scale(surface, scale, scale);
    return surface;
}

static cairo_surface_t *
create_text_mask_surface(int width_px, int height_px, int scale)
{
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_A8,
                                                           width_px,
                                                           height_px);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return NULL;
    }
    cairo_surface_set_device_scale(surface, scale, scale);
    return surface;
}

gboolean
ui_ensure_surfaces(AppState *state)
{
    int scale = MAX(gtk_widget_get_scale_factor(GTK_WIDGET(state->shell.gl_area)), 1);
    if (state->scene.logical_width <= 0 || state->scene.logical_height <= 0)
        return FALSE;

    gint64 width_px_64 = (gint64)state->scene.logical_width * scale;
    gint64 height_px_64 = (gint64)state->scene.logical_height * scale;
    gint64 max_texture_size = renderer_get_max_texture_size(state->renderer) > 0
        ? renderer_get_max_texture_size(state->renderer)
        : 16384;
    if (width_px_64 <= 0 || height_px_64 <= 0 ||
        width_px_64 > max_texture_size || height_px_64 > max_texture_size)
        return FALSE;

    int width_px = (int)width_px_64;
    int height_px = (int)height_px_64;
    int rgba_stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width_px);
    int mask_stride = cairo_format_stride_for_width(CAIRO_FORMAT_A8, width_px);
    if (rgba_stride <= 0 || mask_stride <= 0)
        return FALSE;
    if ((gsize)height_px > G_MAXSIZE / (gsize)rgba_stride ||
        (gsize)height_px > G_MAXSIZE / (gsize)mask_stride)
        return FALSE;
    gsize rgba_bytes = (gsize)rgba_stride * height_px;
    gsize mask_bytes = (gsize)mask_stride * height_px;
    if (rgba_bytes > CHRONO_MAX_SURFACE_BYTES / 2u)
        return FALSE;
    gsize total_bytes = rgba_bytes * 2u;
    if (mask_bytes > (CHRONO_MAX_SURFACE_BYTES - total_bytes) / 2u)
        return FALSE;

    if (state->scene.static_surface != NULL && state->scene.scene_surface != NULL &&
        state->scene.static_text_mask_surface != NULL && state->scene.text_mask_surface != NULL &&
        state->scene.surface_width_px == width_px &&
        state->scene.surface_height_px == height_px &&
        state->scene.surface_scale == scale)
        return TRUE;

    cairo_surface_t *new_static = create_scene_surface(width_px, height_px, scale);
    cairo_surface_t *new_scene = create_scene_surface(width_px, height_px, scale);
    cairo_surface_t *new_static_mask = create_text_mask_surface(width_px, height_px, scale);
    cairo_surface_t *new_mask = create_text_mask_surface(width_px, height_px, scale);
    if (new_static == NULL || new_scene == NULL ||
        new_static_mask == NULL || new_mask == NULL) {
        g_clear_pointer(&new_static, cairo_surface_destroy);
        g_clear_pointer(&new_scene, cairo_surface_destroy);
        g_clear_pointer(&new_static_mask, cairo_surface_destroy);
        g_clear_pointer(&new_mask, cairo_surface_destroy);
        return FALSE;
    }

    g_clear_pointer(&state->scene.static_surface, cairo_surface_destroy);
    g_clear_pointer(&state->scene.scene_surface, cairo_surface_destroy);
    g_clear_pointer(&state->scene.static_text_mask_surface, cairo_surface_destroy);
    g_clear_pointer(&state->scene.text_mask_surface, cairo_surface_destroy);
    state->scene.static_surface = new_static;
    state->scene.scene_surface = new_scene;
    state->scene.static_text_mask_surface = new_static_mask;
    state->scene.text_mask_surface = new_mask;
    state->scene.surface_width_px = width_px;
    state->scene.surface_height_px = height_px;
    state->scene.surface_scale = scale;
    state->scene.static_dirty = TRUE;
    state->scene.scene_dirty = TRUE;
    renderer_invalidate_filtered(state->renderer);
    return TRUE;
}

static gboolean
create_text_context(cairo_surface_t *surface,
                    cairo_t **cr_out,
                    PangoLayout **layout_out)
{
    cairo_t *cr = cairo_create(surface);
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        cairo_destroy(cr);
        return FALSE;
    }
    cairo_font_options_t *font_options = cairo_font_options_create();
    cairo_font_options_set_antialias(font_options, CAIRO_ANTIALIAS_GRAY);
    cairo_font_options_set_hint_style(font_options, CAIRO_HINT_STYLE_FULL);
    cairo_font_options_set_hint_metrics(font_options, CAIRO_HINT_METRICS_ON);
    cairo_set_font_options(cr, font_options);
    PangoLayout *layout = pango_cairo_create_layout(cr);
    if (layout == NULL) {
        cairo_font_options_destroy(font_options);
        cairo_destroy(cr);
        return FALSE;
    }
    pango_cairo_context_set_font_options(pango_layout_get_context(layout), font_options);
    cairo_font_options_destroy(font_options);
    *cr_out = cr;
    *layout_out = layout;
    return TRUE;
}

static cairo_t *
create_text_mask_context(cairo_surface_t *surface)
{
    cairo_t *cr = cairo_create(surface);
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        cairo_destroy(cr);
        return NULL;
    }
    cairo_font_options_t *font_options = cairo_font_options_create();
    cairo_font_options_set_antialias(font_options, CAIRO_ANTIALIAS_GRAY);
    cairo_font_options_set_hint_style(font_options, CAIRO_HINT_STYLE_FULL);
    cairo_font_options_set_hint_metrics(font_options, CAIRO_HINT_METRICS_ON);
    cairo_set_font_options(cr, font_options);
    cairo_font_options_destroy(font_options);
    return cr;
}

static gboolean
finish_text_mask_context(cairo_surface_t *surface, cairo_t *cr)
{
    cairo_status_t status = cairo_status(cr);
    cairo_destroy(cr);
    if (status != CAIRO_STATUS_SUCCESS ||
        cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
        return FALSE;
    cairo_surface_flush(surface);
    return TRUE;
}

static gboolean
finish_text_context(cairo_surface_t *surface,
                    cairo_t *cr,
                    PangoLayout *layout)
{
    g_object_unref(layout);
    cairo_status_t status = cairo_status(cr);
    cairo_destroy(cr);
    if (status != CAIRO_STATUS_SUCCESS ||
        cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
        return FALSE;
    cairo_surface_flush(surface);
    return TRUE;
}

static gboolean
selective_aa_enabled(const AppState *state)
{
    return state->presentation.settings.aa_mode >= AA_FXAA_HIGH;
}
static void
draw_window_chrome(AppState *state, cairo_t *cr, PangoLayout *layout)
{
    fill_rect(cr, state->presentation.layout.titlebar, 0.006, 0.012, 0.010, 1.0);
    draw_separator(cr, 0.0, state->presentation.layout.titlebar.height - 0.5,
                   state->scene.logical_width, state->presentation.layout.titlebar.height - 0.5, 0.24);
    double brand_size = state->presentation.layout.density == UI_DENSITY_FULL ? 20.5 :
                        state->presentation.layout.density == UI_DENSITY_TOO_SMALL ? 14.5 : 18.5;
    double brand_x = state->presentation.layout.density == UI_DENSITY_TOO_SMALL
        ? 18.0 : state->presentation.layout.safe_inset;
    draw_text(state, cr, layout, FONT_SANS, "ChronoLedger", brand_size,
              PANGO_WEIGHT_BOLD, brand_x, 21.0, 180.0, TEXT_LEFT,
              0.60, 1.0, 0.72, 0.98);
    if (state->presentation.layout.density == UI_DENSITY_FULL ||
        state->presentation.layout.density == UI_DENSITY_COMPACT) {
        draw_text(state, cr, layout, FONT_MONO, "PRECISION TIME INSTRUMENT",
                  state->presentation.layout.density == UI_DENSITY_FULL ? 10.2 : 9.2,
                  PANGO_WEIGHT_MEDIUM, brand_x + 140.0, 21.0, 250.0, TEXT_LEFT,
                  0.31, 0.62, 0.45, 0.72);
    }
}

static void
draw_too_small_message(AppState *state, cairo_t *cr, PangoLayout *layout)
{
    double width = state->scene.logical_width;
    double height = state->scene.logical_height;
    Rect panel = {MAX(16.0, width * 0.08), MAX(64.0, height * 0.24),
                  MAX(width * 0.84, 1.0), MAX(height * 0.48, 1.0)};
    fill_rounded(cr, panel, 12.0, 0.010, 0.024, 0.019, 0.96);
    draw_corner_frame(cr, panel, TRUE);
    draw_text(state, cr, layout, FONT_MONO, "DISPLAY AREA TOO SMALL", 11.0,
              PANGO_WEIGHT_BOLD, panel.x + 18.0, panel.y + 28.0,
              panel.width - 36.0, TEXT_CENTER,
              0.96, 0.66, 0.30, 0.96);
    char *size = g_strdup_printf("CURRENT  %d × %d", state->scene.logical_width,
                                 state->scene.logical_height);
    draw_text(state, cr, layout, FONT_MONO, size, 18.0,
              PANGO_WEIGHT_BOLD, panel.x + 18.0,
              panel.y + panel.height * 0.51,
              panel.width - 36.0, TEXT_CENTER,
              0.56, 1.0, 0.70, 0.96);
    g_free(size);
    draw_text(state, cr, layout, FONT_SANS,
              "Expand the Hyprland tile to at least 560 × 360 logical pixels.",
              11.0, PANGO_WEIGHT_MEDIUM, panel.x + 24.0,
              panel.y + panel.height - 30.0,
              panel.width - 48.0, TEXT_CENTER,
              0.39, 0.76, 0.55, 0.78);
}
static void
draw_status_rail_static(AppState *state, cairo_t *cr, PangoLayout *layout)
{
    Rect rail = state->presentation.layout.status_rail;
    if (rail.width <= 1.0)
        return;
    fill_rounded(cr, rail, 7.0, 0.018, 0.036, 0.029, 0.90);
    stroke_rounded(cr, rail, 7.0, 1.0, 0.28, 0.70, 0.48, 0.28);

    if (state->presentation.layout.density == UI_DENSITY_FULL) {
        const char *labels[] = {"ENGINE", "ALLOCATION", "STORE", "ALERTS", "FRAME"};
        double positions[] = {rail.x + 18.0, rail.x + rail.width * 0.25,
                              rail.x + rail.width * 0.50,
                              rail.x + rail.width * 0.70,
                              rail.x + rail.width * 0.86};
        for (int i = 0; i < 5; ++i)
            draw_text(state, cr, layout, FONT_MONO, labels[i], 10.0,
                      PANGO_WEIGHT_BOLD, positions[i], rail.y + 10.0,
                      100.0, TEXT_LEFT, 0.26, 0.58, 0.42, 0.66);
    } else if (state->presentation.layout.density == UI_DENSITY_COMPACT) {
        const char *labels[] = {"ENGINE", "OPEN", "STORE", "FRAME"};
        double positions[] = {rail.x + 14.0, rail.x + rail.width * 0.28,
                              rail.x + rail.width * 0.55,
                              rail.x + rail.width * 0.84};
        for (int i = 0; i < 4; ++i)
            draw_text(state, cr, layout, FONT_MONO, labels[i], 9.0,
                      PANGO_WEIGHT_BOLD, positions[i], rail.y + 9.0,
                      90.0, TEXT_LEFT, 0.26, 0.58, 0.42, 0.66);
    } else {
        draw_text(state, cr, layout, FONT_MONO, "ENGINE", 7.8,
                  PANGO_WEIGHT_BOLD, rail.x + 14.0, rail.y + 9.0,
                  90.0, TEXT_LEFT, 0.26, 0.58, 0.42, 0.66);
        draw_text(state, cr, layout, FONT_MONO, "STORE", 7.8,
                  PANGO_WEIGHT_BOLD, rail.x + rail.width * 0.55, rail.y + 9.0,
                  90.0, TEXT_LEFT, 0.26, 0.58, 0.42, 0.66);
    }
}

static void
draw_mode_tabs(AppState *state, cairo_t *cr, PangoLayout *layout)
{
    const char *labels[] = {"F1 Focus", "F2 Ledger", "F3 Calibrate", "F4 Routines"};
    for (int i = 0; i < CHRONO_MODE_COUNT; ++i) {
        Rect rect = state->presentation.layout.modes[i];
        gboolean selected = state->presentation.active_tab == i;
        draw_button_sized(state, cr, layout, rect, labels[i],
                          selected, selected, FALSE, TRUE,
                          state->presentation.layout.density == UI_DENSITY_FULL ? 13.5 : 13.0);
    }
}
static void
draw_focus_structure(AppState *state, cairo_t *cr, PangoLayout *layout)
{
    Rect chamber = state->presentation.layout.focus_chamber;
    fill_rounded(cr, chamber, 13.0, 0.010, 0.025, 0.020, 0.92);
    draw_corner_frame(cr, chamber, TRUE);
    draw_text(state, cr, layout, FONT_MONO,
              state->presentation.layout.density == UI_DENSITY_MINIMAL
                  ? "FOCUS MONITOR" : "FOCUS CHAMBER",
              state->presentation.layout.density == UI_DENSITY_FULL ? 11.2 : 10.2,
              PANGO_WEIGHT_BOLD,
              chamber.x + 18.0, chamber.y + 19.0,
              220.0, TEXT_LEFT, 0.32, 0.72, 0.50, 0.75);

    Rect queue = state->presentation.layout.queue_region;
    if (queue.width > 1.0) {
        fill_rounded(cr, queue, 10.0, 0.009, 0.020, 0.017, 0.90);
        draw_corner_frame(cr, queue, FALSE);
        draw_text(state, cr, layout, FONT_SANS, "Allocation queue",
                  state->presentation.layout.density == UI_DENSITY_FULL ? 16.5 : 13.5,
                  PANGO_WEIGHT_BOLD, queue.x + 12.0,
                  queue.y + (state->presentation.layout.density == UI_DENSITY_FULL ? 22.0 : 17.0),
                  queue.width - 24.0, TEXT_LEFT,
                  0.52, 0.95, 0.67, 0.92);
        if (state->presentation.layout.density == UI_DENSITY_FULL)
            draw_text(state, cr, layout, FONT_MONO, "SCROLL TO REVIEW", 9.5,
                      PANGO_WEIGHT_MEDIUM, queue.x + 14.0, queue.y + 43.0,
                      queue.width - 28.0, TEXT_LEFT,
                      0.24, 0.54, 0.39, 0.62);
    }

    Rect add = state->presentation.layout.add_region;
    if (state->presentation.layout.show_add_form && add.width > 1.0) {
        fill_rounded(cr, add, 10.0, 0.012, 0.027, 0.022, 0.94);
        draw_corner_frame(cr, add, FALSE);
        draw_text(state, cr, layout, FONT_SANS, "New allocation",
                  state->presentation.layout.density == UI_DENSITY_FULL ? 15.5 : 12.5,
                  PANGO_WEIGHT_BOLD, add.x + 16.0,
                  add.y + (state->presentation.layout.density == UI_DENSITY_FULL ? 19.0 : 14.0),
                  add.width - 32.0, TEXT_LEFT, 0.52, 0.95, 0.67, 0.90);
        if (state->presentation.layout.density == UI_DENSITY_FULL) {
            draw_text(state, cr, layout, FONT_MONO,
                      "Original estimate stays fixed when time is extended.",
                      9.0, PANGO_WEIGHT_MEDIUM, add.x + 18.0, add.y + 39.0,
                      add.width - 36.0, TEXT_LEFT,
                      0.26, 0.58, 0.42, 0.65);
        }
    }
}
static void
draw_ledger_structure(AppState *state, cairo_t *cr, PangoLayout *layout)
{
    Rect summary = state->presentation.layout.ledger_summary;
    fill_rounded(cr, summary, 11.0, 0.011, 0.027, 0.021, 0.93);
    draw_corner_frame(cr, summary, TRUE);
    draw_text(state, cr, layout, FONT_MONO, "COMPLETION LEDGER",
              state->presentation.layout.density == UI_DENSITY_FULL ? 9.8 : 8.8,
              PANGO_WEIGHT_BOLD, summary.x + 18.0,
              summary.y + (state->presentation.layout.density == UI_DENSITY_FULL ? 19.0 : 15.0),
              260.0, TEXT_LEFT, 0.32, 0.72, 0.50, 0.76);

    if (state->presentation.layout.show_ledger_chart && state->presentation.layout.ledger_chart.width > 1.0) {
        Rect chart = state->presentation.layout.ledger_chart;
        fill_rounded(cr, chart, 8.0, 0.008, 0.020, 0.016, 0.88);
        draw_corner_frame(cr, chart, FALSE);
        draw_text(state, cr, layout, FONT_MONO,
                  "ESTIMATE ACCURACY · RECENT COMPLETIONS",
                  state->presentation.layout.density == UI_DENSITY_FULL ? 9.2 : 8.3,
                  PANGO_WEIGHT_BOLD, chart.x + 12.0, chart.y + 14.0,
                  360.0, TEXT_LEFT, 0.28, 0.64, 0.45, 0.70);
        draw_text(state, cr, layout, FONT_MONO, "OVER", 7.5,
                  PANGO_WEIGHT_BOLD, chart.x + 12.0, chart.y + 35.0,
                  40.0, TEXT_LEFT, 0.78, 0.48, 0.28, 0.56);
        draw_text(state, cr, layout, FONT_MONO, "UNDER", 7.5,
                  PANGO_WEIGHT_BOLD, chart.x + 12.0,
                  chart.y + chart.height - 12.0,
                  46.0, TEXT_LEFT, 0.34, 0.82, 0.55, 0.60);
    }

    Rect region = state->presentation.layout.ledger_region;
    fill_rounded(cr, region, 10.0, 0.009, 0.021, 0.017, 0.90);
    draw_corner_frame(cr, region, FALSE);
    draw_text(state, cr, layout, FONT_SANS, "Recorded work",
              state->presentation.layout.density == UI_DENSITY_FULL ? 16.0 : 14.0,
              PANGO_WEIGHT_BOLD, region.x + 14.0,
              region.y + (state->presentation.layout.density == UI_DENSITY_FULL ? 22.0 : 19.0),
              240.0, TEXT_LEFT, 0.52, 0.95, 0.67, 0.94);
}

static void
draw_resize_grip(AppState *state, cairo_t *cr)
{
    Rect rect = state->presentation.layout.window_resize;
    for (int i = 0; i < 3; ++i) {
        cairo_move_to(cr, rect.x + 8.0 + i * 5.0, rect.y + rect.height - 5.0);
        cairo_line_to(cr, rect.x + rect.width - 5.0,
                      rect.y + 8.0 + i * 5.0);
    }
    cairo_set_line_width(cr, 1.0);
    set_source_rgba(cr, 0.26, 0.64, 0.44, 0.38);
    cairo_stroke(cr);
}

gboolean
ui_render_static_layer(AppState *state)
{
    if (!state->scene.static_dirty)
        return TRUE;

    cairo_t *cr = NULL;
    PangoLayout *layout = NULL;
    if (!create_text_context(state->scene.static_surface, &cr, &layout))
        return FALSE;

    gboolean selective = selective_aa_enabled(state);
    cairo_t *mask_cr = selective
        ? create_text_mask_context(state->scene.static_text_mask_surface)
        : NULL;
    if (selective && mask_cr == NULL) {
        g_object_unref(layout);
        cairo_destroy(cr);
        return FALSE;
    }

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    set_source_rgba(cr, 0.003, 0.008, 0.007, 1.0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    if (selective) {
        cairo_set_operator(mask_cr, CAIRO_OPERATOR_SOURCE);
        cairo_set_source_rgba(mask_cr, 0.0, 0.0, 0.0, 0.0);
        cairo_paint(mask_cr);
        cairo_set_operator(mask_cr, CAIRO_OPERATOR_OVER);
        state->scene.active_text_mask_cr = mask_cr;
    }

    draw_window_chrome(state, cr, layout);
    if (state->presentation.layout.density == UI_DENSITY_TOO_SMALL) {
        draw_too_small_message(state, cr, layout);
    } else {
        draw_status_rail_static(state, cr, layout);
        draw_mode_tabs(state, cr, layout);
        if (state->presentation.active_tab == CHRONO_MODE_FOCUS)
            draw_focus_structure(state, cr, layout);
        else if (state->presentation.active_tab == CHRONO_MODE_LEDGER)
            draw_ledger_structure(state, cr, layout);
    }
    draw_resize_grip(state, cr);

    state->scene.active_text_mask_cr = NULL;
    gboolean color_ok = finish_text_context(state->scene.static_surface, cr, layout);
    gboolean mask_ok = !selective ||
        finish_text_mask_context(state->scene.static_text_mask_surface, mask_cr);
    if (!color_ok || !mask_ok)
        return FALSE;
    state->scene.static_dirty = FALSE;
    return TRUE;
}

gboolean
ui_compose_static_layer(AppState *state)
{
    cairo_t *cr = cairo_create(state->scene.scene_surface);
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        cairo_destroy(cr);
        return FALSE;
    }
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(cr, state->scene.static_surface, 0.0, 0.0);
    cairo_paint(cr);
    cairo_status_t status = cairo_status(cr);
    cairo_destroy(cr);
    if (status != CAIRO_STATUS_SUCCESS)
        return FALSE;

    if (!selective_aa_enabled(state))
        return TRUE;
    cairo_t *mask_cr = cairo_create(state->scene.text_mask_surface);
    if (cairo_status(mask_cr) != CAIRO_STATUS_SUCCESS) {
        cairo_destroy(mask_cr);
        return FALSE;
    }
    cairo_set_operator(mask_cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(mask_cr, state->scene.static_text_mask_surface, 0.0, 0.0);
    cairo_paint(mask_cr);
    status = cairo_status(mask_cr);
    cairo_destroy(mask_cr);
    return status == CAIRO_STATUS_SUCCESS;
}
static void
draw_status_rail_dynamic(AppState *state,
                         const AppViewSnapshot *view,
                         cairo_t *cr,
                         PangoLayout *layout)
{
    Rect rail = state->presentation.layout.status_rail;
    if (rail.width <= 1.0)
        return;
    gboolean has_active = view->has_active_task;
    gboolean has_expired = view->has_expired_task;
    const char *engine = has_active ? "RUNNING" :
                         has_expired ? "TIME UP" : "IDLE";
    double er = has_expired ? 1.0 : 0.40;
    double eg = has_expired ? 0.38 : 0.96;
    double eb = has_expired ? 0.23 : 0.60;
    double baseline = rail.y + rail.height * 0.69;
    draw_lamp(cr, rail.x + 14.0, baseline, er, eg, eb,
              has_active || has_expired);
    draw_text(state, cr, layout, FONT_MONO, engine,
              state->presentation.layout.density == UI_DENSITY_FULL ? 13.5 : 11.8,
              PANGO_WEIGHT_BOLD, rail.x + 25.0, baseline,
              120.0, TEXT_LEFT, er, eg, eb, 0.96);

    guint open = view->open_task_count;
    const char *store_word = state->session.data_load_failed ? "RECOVERY" :
        state->session.save_state == SAVE_STATE_SAVED
        ? "SAVED"
        : "SAVE FAILED";

    if (state->presentation.layout.density == UI_DENSITY_FULL) {
        char *allocation = g_strdup_printf("%u OPEN", open);
        draw_text(state, cr, layout, FONT_MONO, allocation, 13.5,
                  PANGO_WEIGHT_BOLD, rail.x + rail.width * 0.25,
                  baseline, 130.0, TEXT_LEFT,
                  0.49, 0.92, 0.64, 0.92);
        g_free(allocation);
        draw_text(state, cr, layout, FONT_MONO, store_word, 13.5,
                  PANGO_WEIGHT_BOLD, rail.x + rail.width * 0.50,
                  baseline, 150.0, TEXT_LEFT,
                  state->session.save_state == SAVE_STATE_SAVED ? 0.42 : 1.0,
                  state->session.save_state == SAVE_STATE_SAVED ? 0.86 : 0.36,
                  state->session.save_state == SAVE_STATE_SAVED ? 0.62 : 0.23,
                  0.94);
        const char *alerts = has_expired ? "1 ACTION" : "CLEAR";
        draw_text(state, cr, layout, FONT_MONO, alerts, 13.5,
                  PANGO_WEIGHT_BOLD, rail.x + rail.width * 0.70,
                  baseline, 130.0, TEXT_LEFT,
                  has_expired ? 1.0 : 0.42,
                  has_expired ? 0.42 : 0.82,
                  has_expired ? 0.24 : 0.62,
                  0.94);
        char *frame_status = state->runtime.power_mode == CHRONO_POWER_INTERACTIVE
            ? g_strdup("NATIVE")
            : g_strdup(state->runtime.power_mode == CHRONO_POWER_BACKGROUND_CLOCK
                       ? "1 HZ"
                       : "SLEEP");
        draw_text(state, cr, layout, FONT_MONO, frame_status, 13.5,
                  PANGO_WEIGHT_BOLD, rail.x + rail.width * 0.86,
                  baseline, rail.width * 0.13, TEXT_LEFT,
                  0.42, 0.82, 0.62, 0.86);
        g_free(frame_status);
    } else if (state->presentation.layout.density == UI_DENSITY_COMPACT) {
        char *allocation = g_strdup_printf("%u", open);
        draw_text(state, cr, layout, FONT_MONO, allocation, 11.8,
                  PANGO_WEIGHT_BOLD, rail.x + rail.width * 0.28,
                  baseline, 80.0, TEXT_LEFT,
                  0.49, 0.92, 0.64, 0.92);
        g_free(allocation);
        draw_text(state, cr, layout, FONT_MONO, store_word, 11.8,
                  PANGO_WEIGHT_BOLD, rail.x + rail.width * 0.55,
                  baseline, 130.0, TEXT_LEFT,
                  state->session.save_state == SAVE_STATE_SAVED ? 0.42 : 1.0,
                  state->session.save_state == SAVE_STATE_SAVED ? 0.86 : 0.36,
                  state->session.save_state == SAVE_STATE_SAVED ? 0.62 : 0.23,
                  0.94);
        char *frame_status = state->runtime.power_mode == CHRONO_POWER_INTERACTIVE
            ? g_strdup("NATIVE")
            : g_strdup(state->runtime.power_mode == CHRONO_POWER_BACKGROUND_CLOCK
                       ? "1HZ"
                       : "SLEEP");
        draw_text(state, cr, layout, FONT_MONO, frame_status, 11.8,
                  PANGO_WEIGHT_BOLD, rail.x + rail.width * 0.84,
                  baseline, 70.0, TEXT_LEFT,
                  0.42, 0.82, 0.62, 0.86);
        g_free(frame_status);
    } else {
        draw_text(state, cr, layout, FONT_MONO, store_word, 10.0,
                  PANGO_WEIGHT_BOLD, rail.x + rail.width * 0.55,
                  baseline, rail.width * 0.42, TEXT_LEFT,
                  state->session.save_state == SAVE_STATE_SAVED ? 0.42 : 1.0,
                  state->session.save_state == SAVE_STATE_SAVED ? 0.86 : 0.36,
                  state->session.save_state == SAVE_STATE_SAVED ? 0.62 : 0.23,
                  0.94);
    }
}

static void
draw_annunciator(AppState *state,
                 const AppViewSnapshot *view,
                 cairo_t *cr,
                 PangoLayout *layout)
{
    Rect rect = state->presentation.layout.annunciator;
    const char *text = state->session.annunciator_text;
    AnnunciatorKind kind = state->session.annunciator_kind;
    if (state->session.data_load_failed) {
        text = "RECOVERY IS READ-ONLY · Import a valid ledger in Ledger to resume editing";
        kind = ANNUNCIATOR_FAILURE;
    } else if (state->presentation.active_tab == CHRONO_MODE_FOCUS &&
               state->session.validation_message != NULL) {
        text = state->session.validation_message;
        kind = ANNUNCIATOR_FAILURE;
    } else if (text == NULL || *text == '\0' ||
        (state->session.annunciator_until_us > 0 &&
         view->monotonic_us > state->session.annunciator_until_us)) {
        text = "READY · F1 Focus · F2 Ledger · F3 Calibration · F4 Routines";
        kind = ANNUNCIATOR_INFO;
    }
    double r = 0.38, g = 0.82, b = 0.58;
    if (kind == ANNUNCIATOR_SUCCESS) {
        r = 0.48; g = 0.96; b = 0.64;
    } else if (kind == ANNUNCIATOR_CAUTION) {
        r = 0.96; g = 0.70; b = 0.30;
    } else if (kind == ANNUNCIATOR_FAILURE) {
        r = 1.0; g = 0.38; b = 0.24;
    }
    fill_rounded(cr, rect, 6.0, r, g, b, 0.055);
    draw_lamp(cr, rect.x + 10.0, rect.y + rect.height * 0.5,
              r, g, b, kind != ANNUNCIATOR_INFO);
    draw_text(state, cr, layout, FONT_MONO, text, 9.0,
              PANGO_WEIGHT_BOLD, rect.x + 23.0,
              rect.y + rect.height * 0.5,
              rect.width - 32.0, TEXT_LEFT,
              r, g, b, 0.84);
}

gboolean
ui_render_dynamic_layer(AppState *state,
                        const AppViewSnapshot *view)
{
    g_return_val_if_fail(state != NULL, FALSE);
    g_return_val_if_fail(view != NULL, FALSE);

    cairo_t *cr = NULL;
    PangoLayout *layout = NULL;
    if (!create_text_context(state->scene.scene_surface, &cr, &layout))
        return FALSE;

    gboolean selective = selective_aa_enabled(state);
    cairo_t *mask_cr = selective
        ? create_text_mask_context(state->scene.text_mask_surface)
        : NULL;
    if (selective && mask_cr == NULL) {
        g_object_unref(layout);
        cairo_destroy(cr);
        return FALSE;
    }
    if (selective) {
        cairo_set_operator(mask_cr, CAIRO_OPERATOR_OVER);
        state->scene.active_text_mask_cr = mask_cr;
    }
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    if (state->presentation.layout.density != UI_DENSITY_TOO_SMALL) {
        draw_status_rail_dynamic(state, view, cr, layout);
        if (state->presentation.active_tab == CHRONO_MODE_FOCUS)
            draw_focus_view(state, view, cr, layout);
        else if (state->presentation.active_tab == CHRONO_MODE_LEDGER)
            draw_ledger_view(state, view, cr, layout);
        else if (state->presentation.active_tab == CHRONO_MODE_ROUTINES)
            app_routines_draw(state, view, cr, layout);
        else
            draw_calibration_view(state, view, cr, layout);
        draw_annunciator(state, view, cr, layout);
    }

    state->scene.active_text_mask_cr = NULL;
    gboolean color_ok = finish_text_context(state->scene.scene_surface, cr, layout);
    gboolean mask_ok = !selective ||
        finish_text_mask_context(state->scene.text_mask_surface, mask_cr);
    return color_ok && mask_ok;
}
