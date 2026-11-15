#pragma once

#include "app_types.h"

G_BEGIN_DECLS

/* Recomputes the complete logical layout from window dimensions. Returns TRUE
 * when the density class changed and static typography/geometry must rebuild. */
gboolean ui_layout_compute(UiLayout *layout,
                           int logical_width,
                           int logical_height);

G_END_DECLS

Rect app_routine_control(const UiLayout *layout, guint index);

void ui_layout_scroll_calibration(UiLayout *layout, double amount);
