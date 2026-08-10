#pragma once

#include "app_state.h"

UiControlRef ui_control_hit_test(const AppPresentationState *presentation,
                                 const AppSessionState *session,
                                 double x,
                                 double y);
gboolean ui_control_get_bounds(const AppPresentationState *presentation,
                               const AppSessionState *session,
                               UiControlRef control,
                               Rect *rect_out,
                               gboolean *danger_out);
