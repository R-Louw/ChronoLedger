#include <glib.h>

#include "ui_controls.h"
#include "ui_layout.h"

void register_layout_tests(void);

static void
test_stacked_focus_content_clearance(void)
{
    for (int width = 760; width <= 1280; width += 20) {
        for (int height = 480; height <= 1080; ++height) {
            UiLayout layout = {0};
            ui_layout_compute(&layout, width, height);
            if (layout.focus_composition != FOCUS_COMPOSITION_STACKED)
                continue;
            if (height == 650 || height % 100 == 0)
                g_test_message("Stacked Focus at %dx%d", width, height);
            /* The content includes a status, a 22px title, and two metadata
             * lines. The gauge ticks extend beyond the nominal dial box. */
            g_assert_cmpfloat(layout.focus_details.height, >=, 156.0);
            g_assert_cmpfloat(layout.focus_details.y + layout.focus_details.height,
                              <=, layout.focus_primary.y - 8.0);
            double gauge_bottom = layout.focus_dial.y +
                layout.focus_dial.height * 0.5 +
                layout.focus_dial.width * 0.42 + 31.0;
            g_assert_cmpfloat(gauge_bottom, <=, layout.focus_primary.y - 4.0);
            g_assert_cmpfloat(layout.focus_primary.y + layout.focus_primary.height,
                              <=, layout.focus_chamber.y + layout.focus_chamber.height);
        }
    }
}

static void
test_calibration_controls_fit(void)
{
    const int widths[] = {560, 759, 760, 1279, 1280, 1920};
    const int heights[] = {360, 479, 480, 650, 659, 660, 699, 700, 715, 800, 1080};
    AppSessionState session = {0};
    for (guint w = 0; w < G_N_ELEMENTS(widths); ++w) {
        for (guint h = 0; h < G_N_ELEMENTS(heights); ++h) {
            AppPresentationState presentation = {.active_tab = CHRONO_MODE_CALIBRATION};
            ui_layout_compute(&presentation.layout, widths[w], heights[h]);
            UiLayout *layout = &presentation.layout;
            UiControlRef controls[40];
            guint count = 0;
            for (guint i = 0; i < CHRONO_PRESENTATION_MODE_COUNT; ++i)
                controls[count++] = (UiControlRef){UI_CONTROL_PRESENTATION, i};
            controls[count++] = (UiControlRef){UI_CONTROL_MASK, 0};
            controls[count++] = (UiControlRef){UI_CONTROL_CURVATURE, 0};
            controls[count++] = (UiControlRef){UI_CONTROL_CURSOR_SHADER_EFFECTS, 0};
            for (guint i = 0; i < CHRONO_PALETTE_COUNT; ++i)
                controls[count++] = (UiControlRef){UI_CONTROL_PALETTE, i};
            controls[count++] = (UiControlRef){UI_CONTROL_RESET_CALIBRATION, 0};
            for (guint i = 0; i < CHRONO_AA_MODE_COUNT; ++i)
                controls[count++] = (UiControlRef){UI_CONTROL_AA, i};
            if (layout->show_advanced_calibration) {
                for (guint i = 0; i < CHRONO_SLIDER_COUNT; ++i)
                    controls[count++] = (UiControlRef){UI_CONTROL_SLIDER, i};
                controls[count++] = (UiControlRef){UI_CONTROL_NOTIFICATIONS, 0};
                controls[count++] = (UiControlRef){UI_CONTROL_SOUND, 0};
            }
            g_assert_cmpfloat(layout->palette_button[12].y, >, layout->palette_button[11].y);
            if (layout->density != UI_DENSITY_MINIMAL)
                g_assert_cmpfloat(layout->palette_button[12].y, ==, layout->palette_button[16].y);
            Rect bounds[40];
            for (guint i = 0; i < count; ++i) {
                g_test_message("%dx%d control %d/%u", widths[w], heights[h], controls[i].id, controls[i].index);
                g_assert_true(ui_control_get_bounds(&presentation, &session, controls[i], &bounds[i], NULL));
                Rect rect = bounds[i];
                g_assert_cmpfloat(rect.x, >=, 0.0);
                g_assert_cmpfloat(rect.x + rect.width, <=, widths[w] + 0.000001);
                g_assert_cmpfloat(rect.height, >=, 24.0);
                // Every control can be scrolled completely into the viewport.
                double original_scroll = layout->calibration_scroll;
                ui_layout_scroll_calibration(layout, original_scroll + rect.y - layout->calibration_viewport.y - 16);
                Rect visible;
                g_assert_true(ui_control_get_bounds(&presentation, &session, controls[i], &visible, NULL));
                g_assert_cmpfloat(visible.y, >=, layout->calibration_viewport.y);
                g_assert_cmpfloat(visible.y + visible.height, <=,
                    layout->calibration_viewport.y + layout->calibration_viewport.height);
                UiControlRef hit = ui_control_hit_test(&presentation, &session,
                    visible.x + visible.width * 0.5, visible.y + visible.height * 0.5);
                g_assert_true(ui_control_equal(hit, controls[i]));
                ui_layout_scroll_calibration(layout, original_scroll);
                for (guint j = 0; j < i; ++j) {
                    gboolean overlaps = rect.x < bounds[j].x + bounds[j].width &&
                        bounds[j].x < rect.x + rect.width &&
                        rect.y < bounds[j].y + bounds[j].height &&
                        bounds[j].y < rect.y + rect.height;
                    g_assert_false(overlaps);
                }
            }
        }
    }
}

void
register_layout_tests(void)
{
    g_test_add_func("/layout/stacked-focus-content-clearance", test_stacked_focus_content_clearance);
    g_test_add_func("/layout/calibration-controls-fit", test_calibration_controls_fit);
}
