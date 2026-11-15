#pragma once
#include "app_state.h"
#include "app_view.h"
#include <pango/pangocairo.h>
Rect app_routine_control(const UiLayout *layout, guint index);
void app_routine_command(AppState *state, guint command);
void app_routines_draw(AppState *state, const AppViewSnapshot *view,
                       cairo_t *cr, PangoLayout *layout);
