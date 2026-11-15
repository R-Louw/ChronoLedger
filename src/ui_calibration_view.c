#include "ui_views.h"
#include "ui_painter.h"
#include "ui_palette.h"

void
draw_calibration_view(AppState *state,
                      const AppViewSnapshot *view,
                      cairo_t *cr,
                      PangoLayout *layout)
{
    static const char *profiles[] = {
        "Balanced", "Authentic", "Readable", "Shader off"
    };
    Rect viewport = state->presentation.layout.calibration_viewport;
    cairo_save(cr);
    cairo_rectangle(cr, viewport.x, viewport.y, viewport.width, viewport.height);
    cairo_clip(cr);
    cairo_t *mask = state->scene.active_text_mask_cr;
    if (mask != NULL) {
        cairo_save(mask);
        cairo_rectangle(mask, viewport.x, viewport.y, viewport.width, viewport.height);
        cairo_clip(mask);
    }
    for (int i = 0; i < CHRONO_PRESENTATION_MODE_COUNT; ++i)
        draw_button(state, cr, layout, state->presentation.layout.presentation_button[i],
                    profiles[i],
                    state->presentation.presentation_mode == (PresentationMode)i,
                    state->presentation.presentation_mode == (PresentationMode)i,
                    FALSE, TRUE);

    gboolean minimal = state->presentation.layout.density == UI_DENSITY_MINIMAL;
    draw_toggle(state, cr, layout, state->presentation.layout.mask_checkbox,
                minimal ? "Mask" : "Phosphor mask", view->preferences.phosphor_mask_enabled);
    draw_toggle(state, cr, layout, state->presentation.layout.curvature_checkbox,
                minimal ? "Curvature" : "Curvature · strength below",
                view->preferences.curvature_enabled);
    draw_toggle(state, cr, layout,
                state->presentation.layout.cursor_shader_effects_toggle,
                minimal ? "Cursor effects" : "Cursor shader effects",
                view->preferences.cursor_shader_effects_enabled);

    if (state->presentation.layout.show_advanced_calibration) {
        for (int i = 0; i < CHRONO_SLIDER_COUNT; ++i)
            draw_slider(state, cr, layout, (SliderId)i);
        draw_toggle(state, cr, layout, state->presentation.layout.notifications_toggle,
                    "Desktop notifications", view->preferences.notifications_enabled);
        draw_toggle(state, cr, layout, state->presentation.layout.sound_toggle,
                    "Expiration sound", view->preferences.sound_enabled);
    }

    {
        const char *aa_labels[] = {"Off", "FXAA", "Sel HQ", "Sel Ultra"};
        for (int i = 0; i < CHRONO_AA_MODE_COUNT; ++i)
            draw_button(state, cr, layout, state->presentation.layout.aa_button[i],
                        aa_labels[i],
                        state->presentation.settings.aa_mode == (AntiAliasingMode)i,
                        state->presentation.settings.aa_mode == (AntiAliasingMode)i,
                        FALSE, TRUE);
    }

    draw_text(state, cr, layout, FONT_SANS, "Color scheme", 10.0, PANGO_WEIGHT_BOLD,
        state->presentation.layout.palette_button[0].x,
        state->presentation.layout.palette_button[0].y - 10.0, 180, TEXT_LEFT, .42, .80, .58, .78);

    for (int i = 0; i < CHRONO_PALETTE_COUNT; ++i)
        draw_button(state, cr, layout, state->presentation.layout.palette_button[i],
                    palette_name((Palette)i),
                    state->presentation.settings.palette == (Palette)i,
                    state->presentation.settings.palette == (Palette)i,
                    FALSE, TRUE);

    draw_button(state, cr, layout, state->presentation.layout.reset_button,
                state->presentation.layout.density == UI_DENSITY_MINIMAL
                    ? "BALANCED" : "RESTORE BALANCED",
                FALSE, FALSE, FALSE, TRUE);
    if (mask != NULL) cairo_restore(mask);
    cairo_restore(cr);
    const UiLayout *l = &state->presentation.layout;
    if (l->calibration_max_scroll > 0) {
        double h = MAX(24.0, viewport.height * viewport.height /
            (viewport.height + l->calibration_max_scroll));
        Rect thumb = {viewport.x + viewport.width - 4.0,
            viewport.y + (viewport.height - h) * l->calibration_scroll / l->calibration_max_scroll, 3, h};
        fill_rounded(cr, thumb, 1.5, .5, 1, .65, .8);
    }
}
