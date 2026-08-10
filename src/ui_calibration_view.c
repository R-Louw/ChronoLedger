#include "ui_views.h"
#include "ui_painter.h"
#include "time_parse.h"

#include <math.h>
#include <stdlib.h>

#define PI 3.14159265358979323846

void
draw_calibration_view(AppState *state,
                      const AppViewSnapshot *view,
                      cairo_t *cr,
                      PangoLayout *layout)
{
    static const char *profiles[] = {
        "Balanced", "Authentic", "Readable", "Shader off"
    };
    static const char *palette_labels[CHRONO_PALETTE_COUNT] = {
        "Green", "Amber", "Ice", "Vector",
        "Aqua", "Sunset", "Plasma", "Radar",
        "Aurora", "Cyber", "Inferno", "Galaxy",
    };

    for (int i = 0; i < CHRONO_PRESENTATION_MODE_COUNT; ++i)
        draw_button(state, cr, layout, state->presentation.layout.presentation_button[i],
                    profiles[i],
                    state->presentation.presentation_mode == (PresentationMode)i,
                    state->presentation.presentation_mode == (PresentationMode)i,
                    FALSE, TRUE);

    draw_toggle(state, cr, layout, state->presentation.layout.mask_checkbox,
                "Phosphor mask", view->preferences.phosphor_mask_enabled);
    draw_toggle(state, cr, layout, state->presentation.layout.curvature_checkbox,
                "Curvature · HQ reconstruction",
                view->preferences.curvature_enabled);
    draw_toggle(state, cr, layout,
                state->presentation.layout.cursor_shader_effects_toggle,
                "Cursor shader effects",
                view->preferences.cursor_shader_effects_enabled);

    if (state->presentation.layout.show_advanced_calibration) {
        for (int i = 0; i < CHRONO_SLIDER_COUNT; ++i)
            draw_slider(state, cr, layout, (SliderId)i);
        draw_toggle(state, cr, layout, state->presentation.layout.notifications_toggle,
                    "Desktop notifications", view->preferences.notifications_enabled);
        draw_toggle(state, cr, layout, state->presentation.layout.sound_toggle,
                    "Expiration sound", view->preferences.sound_enabled);
    }

    if (state->presentation.layout.density != UI_DENSITY_MINIMAL) {
        const char *aa_labels[] = {"Off", "FXAA", "Sel HQ", "Sel Ultra"};
        for (int i = 0; i < CHRONO_AA_MODE_COUNT; ++i)
            draw_button(state, cr, layout, state->presentation.layout.aa_button[i],
                        aa_labels[i],
                        state->presentation.settings.aa_mode == (AntiAliasingMode)i,
                        state->presentation.settings.aa_mode == (AntiAliasingMode)i,
                        FALSE, TRUE);
    }

    for (int i = 0; i < CHRONO_PALETTE_COUNT; ++i)
        draw_button(state, cr, layout, state->presentation.layout.palette_button[i],
                    palette_labels[i],
                    state->presentation.settings.palette == (Palette)i,
                    state->presentation.settings.palette == (Palette)i,
                    FALSE, TRUE);

    draw_button(state, cr, layout, state->presentation.layout.reset_button,
                state->presentation.layout.density == UI_DENSITY_MINIMAL
                    ? "BALANCED" : "RESTORE BALANCED",
                FALSE, FALSE, FALSE, TRUE);
}
