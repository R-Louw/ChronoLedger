#pragma once

#include "app_state.h"
#include "app_view.h"

#include <pango/pangocairo.h>

void draw_focus_view(AppState *state,
                     const AppViewSnapshot *view,
                     cairo_t *cr,
                     PangoLayout *layout);
void draw_ledger_view(AppState *state,
                      const AppViewSnapshot *view,
                      cairo_t *cr,
                      PangoLayout *layout);
void draw_calibration_view(AppState *state,
                           const AppViewSnapshot *view,
                           cairo_t *cr,
                           PangoLayout *layout);
