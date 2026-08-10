#include "chrono_preferences.h"

void
chrono_preferences_init_defaults(ChronoPreferences *preferences)
{
    g_return_if_fail(preferences != NULL);

    *preferences = (ChronoPreferences){
        .notifications_enabled = TRUE,
        .sound_enabled = FALSE,
        .phosphor_mask_enabled = TRUE,
        .curvature_enabled = FALSE,
        .cursor_shader_effects_enabled = TRUE,
    };
}

void
chrono_preferences_copy(ChronoPreferences *destination,
                         const ChronoPreferences *source)
{
    g_return_if_fail(destination != NULL);
    g_return_if_fail(source != NULL);

    *destination = (ChronoPreferences){
        .notifications_enabled = !!source->notifications_enabled,
        .sound_enabled = !!source->sound_enabled,
        .phosphor_mask_enabled = !!source->phosphor_mask_enabled,
        .curvature_enabled = !!source->curvature_enabled,
        .cursor_shader_effects_enabled =
            !!source->cursor_shader_effects_enabled,
    };
}
