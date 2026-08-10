#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct {
    gboolean notifications_enabled;
    gboolean sound_enabled;
    gboolean phosphor_mask_enabled;
    gboolean curvature_enabled;
    gboolean cursor_shader_effects_enabled;
} ChronoPreferences;

void chrono_preferences_init_defaults(ChronoPreferences *preferences);
void chrono_preferences_copy(ChronoPreferences *destination,
                             const ChronoPreferences *source);

G_END_DECLS
