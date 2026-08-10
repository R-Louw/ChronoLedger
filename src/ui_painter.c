#include "ui_painter.h"
#include "time_parse.h"

#include <math.h>
#include <pango/pangocairo.h>
#include <stdlib.h>

#define PI 3.14159265358979323846

void
rounded_rectangle(cairo_t *cr, Rect rect, double radius)
{
    double r = MIN(radius, MIN(rect.width, rect.height) * 0.5);
    double x2 = rect.x + rect.width;
    double y2 = rect.y + rect.height;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x2 - r, rect.y + r, r, -PI / 2.0, 0.0);
    cairo_arc(cr, x2 - r, y2 - r, r, 0.0, PI / 2.0);
    cairo_arc(cr, rect.x + r, y2 - r, r, PI / 2.0, PI);
    cairo_arc(cr, rect.x + r, rect.y + r, r, PI, 3.0 * PI / 2.0);
    cairo_close_path(cr);
}

void
set_source_rgba(cairo_t *cr, double r, double g, double b, double a)
{
    cairo_set_source_rgba(cr, r, g, b, a);
}

void
fill_rect(cairo_t *cr, Rect rect, double r, double g, double b, double a)
{
    cairo_rectangle(cr, rect.x, rect.y, rect.width, rect.height);
    set_source_rgba(cr, r, g, b, a);
    cairo_fill(cr);
}

void
fill_rounded(cairo_t *cr,
             Rect rect,
             double radius,
             double r,
             double g,
             double b,
             double a)
{
    rounded_rectangle(cr, rect, radius);
    set_source_rgba(cr, r, g, b, a);
    cairo_fill(cr);
}

void
stroke_rounded(cairo_t *cr,
               Rect rect,
               double radius,
               double line_width,
               double r,
               double g,
               double b,
               double a)
{
    rounded_rectangle(cr, rect, radius);
    set_source_rgba(cr, r, g, b, a);
    cairo_set_line_width(cr, line_width);
    cairo_stroke(cr);
}

void
draw_separator(cairo_t *cr,
               double x0,
               double y0,
               double x1,
               double y1,
               double alpha)
{
    cairo_move_to(cr, x0, y0);
    cairo_line_to(cr, x1, y1);
    cairo_set_line_width(cr, 1.0);
    set_source_rgba(cr, 0.28, 0.72, 0.49, alpha);
    cairo_stroke(cr);
}

PangoFontDescription *
font_description_for(AppState *state,
                     FontMode mode,
                     double size,
                     PangoWeight weight)
{
    double safe_size = isfinite(size) && size > 0.0 ? size : 1.0;
    double scaled_size = safe_size * 2.0;
    int slot = (int)CLAMP(lround(CLAMP(scaled_size,
                                      0.0,
                                      (double)(CHRONO_FONT_CACHE_SLOTS - 1))),
                          0,
                          CHRONO_FONT_CACHE_SLOTS - 1);
    int weight_slot = weight >= PANGO_WEIGHT_BOLD ? 1 : 0;
    int font_slot = CLAMP((int)mode, 0, CHRONO_FONT_MODE_COUNT - 1);
    PangoFontDescription **cached =
        &state->scene.font_cache[font_slot][slot][weight_slot];
    if (*cached == NULL) {
        *cached = pango_font_description_new();
        pango_font_description_set_family(*cached,
                                           mode == FONT_MONO
                                               ? "Monospace"
                                               : "Sans");
        pango_font_description_set_weight(*cached,
                                           weight_slot
                                               ? PANGO_WEIGHT_BOLD
                                               : PANGO_WEIGHT_MEDIUM);
        pango_font_description_set_absolute_size(*cached,
                                                  safe_size * PANGO_SCALE);
    }
    return *cached;
}

void
draw_text(AppState *state,
          cairo_t *cr,
          PangoLayout *layout,
          FontMode mode,
          const char *text,
          double size,
          PangoWeight weight,
          double x,
          double y,
          double width,
          TextAlign alignment,
          double r,
          double g,
          double b,
          double a)
{
    PangoFontDescription *font = font_description_for(state, mode, size, weight);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, text != NULL ? text : "", -1);
    if (isfinite(width) && width > 0.0) {
        double pango_width = CLAMP(width * PANGO_SCALE, 1.0, (double)G_MAXINT);
        pango_layout_set_width(layout, (int)lround(pango_width));
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        pango_layout_set_single_paragraph_mode(layout, TRUE);
    } else {
        pango_layout_set_width(layout, -1);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
    }

    int text_width = 0;
    int text_height = 0;
    pango_layout_get_pixel_size(layout, &text_width, &text_height);
    double draw_x = x;
    if (alignment == TEXT_CENTER)
        draw_x = x + (width - text_width) * 0.5;
    else if (alignment == TEXT_RIGHT)
        draw_x = x + width - text_width;

    cairo_move_to(cr, draw_x, y - text_height * 0.5);
    set_source_rgba(cr, r, g, b, a);
    pango_cairo_show_layout(cr, layout);

    if (state->scene.active_text_mask_cr != NULL) {
        cairo_t *mask_cr = state->scene.active_text_mask_cr;
        cairo_move_to(mask_cr, draw_x, y - text_height * 0.5);
        cairo_set_source_rgba(mask_cr, 1.0, 1.0, 1.0, 1.0);
        pango_cairo_show_layout(mask_cr, layout);
    }
}

void
draw_corner_frame(cairo_t *cr, Rect rect, gboolean prominent)
{
    double length = prominent ? 22.0 : 14.0;
    double alpha = prominent ? 0.68 : 0.34;
    double width = prominent ? 1.7 : 1.0;
    set_source_rgba(cr, 0.35, 0.92, 0.59, alpha);
    cairo_set_line_width(cr, width);

    cairo_move_to(cr, rect.x, rect.y + length);
    cairo_line_to(cr, rect.x, rect.y);
    cairo_line_to(cr, rect.x + length, rect.y);

    cairo_move_to(cr, rect.x + rect.width - length, rect.y);
    cairo_line_to(cr, rect.x + rect.width, rect.y);
    cairo_line_to(cr, rect.x + rect.width, rect.y + length);

    cairo_move_to(cr, rect.x + rect.width, rect.y + rect.height - length);
    cairo_line_to(cr, rect.x + rect.width, rect.y + rect.height);
    cairo_line_to(cr, rect.x + rect.width - length, rect.y + rect.height);

    cairo_move_to(cr, rect.x + length, rect.y + rect.height);
    cairo_line_to(cr, rect.x, rect.y + rect.height);
    cairo_line_to(cr, rect.x, rect.y + rect.height - length);
    cairo_stroke(cr);
}

void
draw_lamp(cairo_t *cr,
          double x,
          double y,
          double r,
          double g,
          double b,
          gboolean active)
{
    cairo_arc(cr, x, y, 4.2, 0.0, 2.0 * PI);
    set_source_rgba(cr, r, g, b, active ? 0.98 : 0.20);
    cairo_fill(cr);
    cairo_arc(cr, x, y, 8.0, 0.0, 2.0 * PI);
    set_source_rgba(cr, r, g, b, active ? 0.13 : 0.025);
    cairo_fill(cr);
}

void
draw_status_word(AppState *state,
                 cairo_t *cr,
                 PangoLayout *layout,
                 const char *word,
                 Rect rect,
                 double r,
                 double g,
                 double b)
{
    fill_rounded(cr, rect, rect.height * 0.5, r, g, b, 0.10);
    stroke_rounded(cr, rect, rect.height * 0.5, 1.0, r, g, b, 0.46);
    draw_text(state, cr, layout, FONT_MONO, word,
              rect.height >= 26.0 ? 12.5 : 11.0, PANGO_WEIGHT_BOLD,
              rect.x, rect.y + rect.height * 0.5, rect.width,
              TEXT_CENTER, r, g, b, 0.98);
}

void
draw_button_sized(AppState *state,
                  cairo_t *cr,
                  PangoLayout *layout,
                  Rect rect,
                  const char *label,
                  gboolean selected,
                  gboolean primary,
                  gboolean destructive,
                  gboolean enabled,
                  double font_size)
{
    double r = destructive ? 0.96 : 0.36;
    double g = destructive ? 0.36 : 0.95;
    double b = destructive ? 0.24 : 0.58;
    double fill_alpha = selected || primary ? 0.18 : 0.045;
    double border_alpha = selected || primary ? 0.82 : 0.28;
    double text_alpha = enabled ? 0.98 : 0.34;
    if (!enabled) {
        fill_alpha *= 0.35;
        border_alpha *= 0.35;
    }

    fill_rounded(cr, rect, 7.0, r, g, b, fill_alpha);
    stroke_rounded(cr, rect, 7.0, selected || primary ? 1.5 : 1.0,
                   r, g, b, border_alpha);
    if (selected) {
        Rect mark = {rect.x + 5.0, rect.y + 5.0, 3.0, rect.height - 10.0};
        fill_rounded(cr, mark, 1.5, r, g, b, 0.90);
    }
    draw_text(state, cr, layout, FONT_SANS, label, font_size,
              primary || selected ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_MEDIUM,
              rect.x + 8.0, rect.y + rect.height * 0.5,
              rect.width - 16.0, TEXT_CENTER,
              r, g, b, text_alpha);
}

void
draw_button(AppState *state,
            cairo_t *cr,
            PangoLayout *layout,
            Rect rect,
            const char *label,
            gboolean selected,
            gboolean primary,
            gboolean destructive,
            gboolean enabled)
{
    draw_button_sized(state, cr, layout, rect, label,
                      selected, primary, destructive, enabled, 12.0);
}

const char *
status_word(ChronoTaskStatus status)
{
    switch (status) {
    case CHRONO_TASK_READY: return "READY";
    case CHRONO_TASK_ACTIVE: return "RUNNING";
    case CHRONO_TASK_PAUSED: return "HELD";
    case CHRONO_TASK_EXPIRED: return "TIME UP";
    case CHRONO_TASK_DONE: return "LOGGED";
    }
    return "READY";
}

void
status_colour(ChronoTaskStatus status, double *r, double *g, double *b)
{
    *r = 0.36;
    *g = 0.95;
    *b = 0.58;
    if (status == CHRONO_TASK_PAUSED) {
        *r = 0.82;
        *g = 0.76;
        *b = 0.42;
    } else if (status == CHRONO_TASK_EXPIRED) {
        *r = 1.0;
        *g = 0.38;
        *b = 0.23;
    } else if (status == CHRONO_TASK_DONE) {
        *r = 0.40;
        *g = 0.82;
        *b = 0.96;
    }
}

char *
format_task_display_title(const AppTaskView *task)
{
    g_return_val_if_fail(task != NULL, g_strdup(""));
    return g_strdup(task->title);
}

char *
format_delta(gint64 estimate, gint64 actual)
{
    gint64 delta = actual - estimate;
    if (delta == 0)
        return g_strdup("ON ESTIMATE");
    char *duration = chrono_format_compact_duration(llabs(delta));
    char *text = g_strdup_printf("%s %s", duration, delta > 0 ? "OVER" : "UNDER");
    g_free(duration);
    return text;
}



const char *
slider_name(SliderId slider)
{
    static const char *names[CHRONO_SLIDER_COUNT] = {
        "Glow", "Persistence", "Cursor trail", "Vignette", "Mask strength",
    };
    if (slider < SLIDER_GLOW || slider >= CHRONO_SLIDER_COUNT)
        return "Unknown";
    return names[slider];
}

float *
slider_value(ShaderSettings *settings, SliderId slider)
{
    if (settings == NULL)
        return NULL;
    switch (slider) {
    case SLIDER_GLOW: return &settings->glow;
    case SLIDER_PERSISTENCE: return &settings->persistence;
    case SLIDER_CURSOR_PERSISTENCE: return &settings->cursor_persistence;
    case SLIDER_VIGNETTE: return &settings->vignette;
    case SLIDER_MASK: return &settings->mask_strength;
    case SLIDER_NONE: return NULL;
    }
    return NULL;
}
void
draw_input_field(AppState *state,
                 cairo_t *cr,
                 PangoLayout *layout,
                 Rect rect,
                 const char *label,
                 const char *value,
                 const char *placeholder,
                 EditTarget target,
                 gint64 now_us)
{
    gboolean focused = state->session.edit_target == target;
    fill_rounded(cr, rect, 7.0, 0.015, 0.038, 0.029,
                 focused ? 0.98 : 0.82);
    stroke_rounded(cr, rect, 7.0, focused ? 1.8 : 1.0,
                   focused ? 0.55 : 0.30,
                   focused ? 1.0 : 0.70,
                   focused ? 0.72 : 0.49,
                   focused ? 0.88 : 0.32);
    double label_size = state->presentation.layout.density == UI_DENSITY_FULL ? 9.5 : 8.8;
    double value_size = state->presentation.layout.density == UI_DENSITY_FULL ? 14.5 : 13.5;
    draw_text(state, cr, layout, FONT_MONO, label, label_size, PANGO_WEIGHT_BOLD,
              rect.x + 10.0, rect.y + 10.0, rect.width - 20.0,
              TEXT_LEFT, 0.26, 0.60, 0.42, 0.70);
    const char *text = value != NULL && *value != '\0' ? value : placeholder;
    draw_text(state, cr, layout,
              target == EDIT_DURATION ? FONT_MONO : FONT_SANS,
              text, value_size, PANGO_WEIGHT_MEDIUM,
              rect.x + 10.0, rect.y + rect.height - 12.0, rect.width - 20.0,
              TEXT_LEFT,
              0.52, 0.94, 0.66,
              value != NULL && *value != '\0' ? 0.96 : 0.42);
    if (focused && ((now_us / 500000) % 2 == 0)) {
        cairo_move_to(cr, rect.x + rect.width - 12.0, rect.y + 12.0);
        cairo_line_to(cr, rect.x + rect.width - 12.0, rect.y + rect.height - 9.0);
        cairo_set_line_width(cr, 1.4);
        set_source_rgba(cr, 0.58, 1.0, 0.72, 0.86);
        cairo_stroke(cr);
    }
}
void
draw_slider(AppState *state,
            cairo_t *cr,
            PangoLayout *layout,
            SliderId slider)
{
    if (slider < 0 || slider >= CHRONO_SLIDER_COUNT)
        return;
    Rect track = state->presentation.layout.slider_track[slider];
    float *value = slider_value(&state->presentation.settings, slider);
    double normalized = value != NULL && isfinite(*value)
        ? CLAMP(*value, 0.0f, 1.0f)
        : 0.0;
    draw_text(state, cr, layout, FONT_SANS, slider_name(slider), 10.0,
              PANGO_WEIGHT_MEDIUM, track.x, track.y - 8.0,
              track.width * 0.60, TEXT_LEFT,
              0.39, 0.78, 0.56, 0.78);
    char *value_text = g_strdup_printf("%3.0f%%", normalized * 100.0);
    draw_text(state, cr, layout, FONT_MONO, value_text, 9.5,
              PANGO_WEIGHT_BOLD, track.x + track.width * 0.65,
              track.y - 8.0, track.width * 0.35,
              TEXT_RIGHT, 0.54, 0.96, 0.67, 0.90);
    g_free(value_text);

    Rect trough = {track.x, track.y + 3.0, track.width, 5.0};
    fill_rounded(cr, trough, 2.5, 0.20, 0.48, 0.34, 0.18);
    Rect fill = trough;
    fill.width *= normalized;
    fill_rounded(cr, fill, 2.5, 0.38, 0.94, 0.58, 0.72);
    double handle_x = trough.x + trough.width * normalized;
    cairo_arc(cr, handle_x, trough.y + trough.height * 0.5,
              state->input.active_slider == slider ? 6.0 : 4.5,
              0.0, 2.0 * PI);
    set_source_rgba(cr, 0.62, 1.0, 0.72, 0.96);
    cairo_fill(cr);
}

void
draw_toggle(AppState *state,
            cairo_t *cr,
            PangoLayout *layout,
            Rect rect,
            const char *label,
            gboolean enabled)
{
    draw_text(state, cr, layout, FONT_SANS, label, 10.8,
              PANGO_WEIGHT_MEDIUM, rect.x + 2.0,
              rect.y + rect.height * 0.5,
              rect.width - 62.0, TEXT_LEFT,
              0.44, 0.84, 0.60, 0.86);
    Rect switch_rect = {rect.x + rect.width - 52.0,
                        rect.y + 4.0, 48.0, rect.height - 8.0};
    fill_rounded(cr, switch_rect, switch_rect.height * 0.5,
                 enabled ? 0.34 : 0.18,
                 enabled ? 0.84 : 0.38,
                 enabled ? 0.53 : 0.29,
                 enabled ? 0.28 : 0.14);
    stroke_rounded(cr, switch_rect, switch_rect.height * 0.5, 1.0,
                   0.34, 0.82, 0.55, enabled ? 0.60 : 0.26);
    double knob_x = enabled
        ? switch_rect.x + switch_rect.width - switch_rect.height * 0.5
        : switch_rect.x + switch_rect.height * 0.5;
    cairo_arc(cr, knob_x, switch_rect.y + switch_rect.height * 0.5,
              switch_rect.height * 0.29, 0.0, 2.0 * PI);
    set_source_rgba(cr, 0.62, 1.0, 0.72, enabled ? 0.96 : 0.52);
    cairo_fill(cr);
}
