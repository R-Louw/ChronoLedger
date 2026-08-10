#pragma once

#include "app_state.h"
#include "app_view.h"

#include <pango/pangocairo.h>

typedef enum {
    TEXT_LEFT,
    TEXT_CENTER,
    TEXT_RIGHT,
} TextAlign;

void rounded_rectangle(cairo_t *cr, Rect rect, double radius);
void set_source_rgba(cairo_t *cr, double r, double g, double b, double a);
void fill_rect(cairo_t *cr, Rect rect, double r, double g, double b, double a);
void fill_rounded(cairo_t *cr,
                  Rect rect,
                  double radius,
                  double r,
                  double g,
                  double b,
                  double a);
void stroke_rounded(cairo_t *cr,
                    Rect rect,
                    double radius,
                    double line_width,
                    double r,
                    double g,
                    double b,
                    double a);
void draw_separator(cairo_t *cr,
                    double x0,
                    double y0,
                    double x1,
                    double y1,
                    double alpha);
void draw_text(AppState *state,
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
               double a);
void draw_corner_frame(cairo_t *cr, Rect rect, gboolean prominent);
void draw_lamp(cairo_t *cr,
               double x,
               double y,
               double r,
               double g,
               double b,
               gboolean active);
void draw_status_word(AppState *state,
                      cairo_t *cr,
                      PangoLayout *layout,
                      const char *word,
                      Rect rect,
                      double r,
                      double g,
                      double b);
void draw_button_sized(AppState *state,
                       cairo_t *cr,
                       PangoLayout *layout,
                       Rect rect,
                       const char *label,
                       gboolean selected,
                       gboolean primary,
                       gboolean destructive,
                       gboolean enabled,
                       double font_size);
void draw_button(AppState *state,
                 cairo_t *cr,
                 PangoLayout *layout,
                 Rect rect,
                 const char *label,
                 gboolean selected,
                 gboolean primary,
                 gboolean destructive,
                 gboolean enabled);
const char *status_word(ChronoTaskStatus status);
void status_colour(ChronoTaskStatus status, double *r, double *g, double *b);
char *format_task_display_title(const AppTaskView *task);
char *format_delta(gint64 estimate, gint64 actual);
void draw_input_field(AppState *state,
                      cairo_t *cr,
                      PangoLayout *layout,
                      Rect rect,
                      const char *label,
                      const char *value,
                      const char *placeholder,
                      EditTarget target,
                      gint64 now_us);
const char *slider_name(SliderId slider);
float *slider_value(ShaderSettings *settings, SliderId slider);
void draw_slider(AppState *state,
                 cairo_t *cr,
                 PangoLayout *layout,
                 SliderId slider);
void draw_toggle(AppState *state,
                 cairo_t *cr,
                 PangoLayout *layout,
                 Rect rect,
                 const char *label,
                 gboolean enabled);
