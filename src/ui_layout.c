#include "ui_layout.h"

#include <math.h>
#include <string.h>

gboolean
ui_layout_compute(UiLayout *layout,
                  int logical_width,
                  int logical_height)
{
    g_return_val_if_fail(layout != NULL, FALSE);
    UiDensity previous_density = layout->density;
    double previous_scroll = layout->calibration_scroll;
    double width = MAX(logical_width, 1);
    double height = MAX(logical_height, 1);

    memset(layout, 0, sizeof(*layout));
    if (width < CHRONO_MINIMAL_WIDTH || height < CHRONO_MINIMAL_HEIGHT)
        layout->density = UI_DENSITY_TOO_SMALL;
    else if (width < CHRONO_COMPACT_WIDTH || height < CHRONO_COMPACT_HEIGHT)
        layout->density = UI_DENSITY_MINIMAL;
    else if (width < CHRONO_FULL_WIDTH || height < CHRONO_FULL_HEIGHT)
        layout->density = UI_DENSITY_COMPACT;
    else
        layout->density = UI_DENSITY_FULL;

    gboolean density_changed = layout->density != previous_density;

    double margin = layout->density == UI_DENSITY_FULL
        ? MAX(CLAMP(width * 0.075, 64.0, 144.0),
              MAX((width - 1600.0) * 0.5, 0.0))
        : layout->density == UI_DENSITY_COMPACT
            ? CLAMP(width * 0.045, 24.0, 48.0)
            : CLAMP(width * 0.035, 18.0, 30.0);
    layout->safe_inset = margin;
    layout->titlebar = (Rect){0.0, 0.0, width, CHRONO_TITLEBAR_HEIGHT};
    layout->title_drag = layout->titlebar;
    layout->window_resize = (Rect){width - 28.0, height - 28.0, 28.0, 28.0};

    if (layout->density == UI_DENSITY_TOO_SMALL)
        return density_changed;

    double rail_y = layout->density == UI_DENSITY_FULL ? 58.0 : 52.0;
    double rail_h = layout->density == UI_DENSITY_FULL ? 38.0 : 34.0;
    layout->status_rail = (Rect){margin, rail_y, width - margin * 2.0, rail_h};
    layout->save_status = (Rect){layout->status_rail.x +
                                 layout->status_rail.width *
                                 (layout->density == UI_DENSITY_FULL ? 0.49 : 0.54),
                                 rail_y,
                                 layout->status_rail.width *
                                 (layout->density == UI_DENSITY_FULL ? 0.17 : 0.25),
                                 rail_h};
    double bottom_inset = layout->density == UI_DENSITY_FULL ? 62.0 :
                          layout->density == UI_DENSITY_COMPACT ? 39.0 : 32.0;
    layout->annunciator = (Rect){margin, height - bottom_inset,
                                 width - margin * 2.0, 21.0};

    double mode_y = rail_y + rail_h + 8.0;
    double mode_gap = 6.0;
    double mode_width = layout->density == UI_DENSITY_FULL
        ? 130.0
        : (width - margin * 2.0 - mode_gap * (CHRONO_MODE_COUNT - 1)) / CHRONO_MODE_COUNT;
    double mode_height = layout->density == UI_DENSITY_FULL ? 34.0 : 30.0;
    double mode_group_width = mode_width * CHRONO_MODE_COUNT +
                              mode_gap * (CHRONO_MODE_COUNT - 1);
    double mode_start_x = layout->density == UI_DENSITY_FULL
        ? margin + (width - margin * 2.0 - mode_group_width) * 0.5
        : margin;
    for (int i = 0; i < CHRONO_MODE_COUNT; ++i)
        layout->modes[i] = (Rect){mode_start_x + i * (mode_width + mode_gap),
                                  mode_y, mode_width, mode_height};

    double content_y = mode_y + mode_height +
                       (layout->density == UI_DENSITY_FULL ? 16.0 : 10.0);
    double content_bottom = layout->annunciator.y -
                            (layout->density == UI_DENSITY_FULL ? 14.0 : 9.0);
    double content_height = MAX(content_bottom - content_y, 1.0);

    if (layout->density == UI_DENSITY_FULL) {
        layout->focus_composition = FOCUS_COMPOSITION_DESKTOP;
        layout->visible_queue_rows = CHRONO_VISIBLE_QUEUE_ROWS;
        layout->visible_ledger_rows = CHRONO_VISIBLE_LEDGER_ROWS;
        layout->show_add_form = TRUE;
        layout->add_form_stacked = TRUE;
        layout->show_ledger_chart = TRUE;
        layout->show_advanced_calibration = TRUE;

        double field_height = MIN(content_height, 760.0);
        double field_y = content_y + (content_height - field_height) * 0.5;
        double field_width = width - margin * 2.0;
        double right_width = CLAMP(field_width * 0.275, 390.0, 440.0);
        double gap = 22.0;
        double add_height = 168.0;
        layout->focus_chamber = (Rect){margin, field_y,
                                       field_width - right_width - gap,
                                       field_height};
        layout->queue_region = (Rect){layout->focus_chamber.x +
                                      layout->focus_chamber.width + gap,
                                      field_y, right_width,
                                      field_height - add_height - 12.0};
        layout->add_region = (Rect){layout->queue_region.x,
                                    layout->queue_region.y +
                                    layout->queue_region.height + 12.0,
                                    right_width, add_height};
        if (layout->queue_region.height < 370.0)
            layout->visible_queue_rows = 3;
        else if (layout->queue_region.height < 460.0)
            layout->visible_queue_rows = 4;
        else if (layout->queue_region.height < 548.0)
            layout->visible_queue_rows = 5;
    } else if (layout->density == UI_DENSITY_COMPACT) {
        /* A stacked chamber needs 300px for its dial, text and action row,
         * plus 116px for the queue and a 10px gap. Short tiles use the
         * side-by-side composition even when their aspect ratio is narrow. */
        const double minimum_chamber = 300.0;
        const double minimum_queue = 116.0;
        const double gap = 10.0;
        gboolean wide_compact = width / height >= 1.42 ||
            content_height < minimum_chamber + minimum_queue + gap;
        layout->focus_composition = wide_compact
            ? FOCUS_COMPOSITION_WIDE_COMPACT
            : FOCUS_COMPOSITION_STACKED;
        layout->visible_ledger_rows = height >= 760.0 ? 5 : 2;
        layout->show_ledger_chart = height >= 760.0;
        layout->show_advanced_calibration = height >= 660.0;

        if (wide_compact) {
            double field_width = width - margin * 2.0;
            double right_width = CLAMP(field_width * 0.35, 288.0, 324.0);
            double add_height = 120.0;
            layout->show_add_form = content_height >= 270.0;
            layout->add_form_stacked = TRUE;
            layout->visible_queue_rows = 2;
            layout->focus_chamber = (Rect){margin, content_y,
                                           field_width - right_width - gap,
                                           content_height};
            layout->queue_region = (Rect){layout->focus_chamber.x +
                                          layout->focus_chamber.width + gap,
                                          content_y, right_width,
                                          content_height -
                                          (layout->show_add_form
                                               ? add_height + 8.0
                                               : 0.0)};
            if (layout->show_add_form)
                layout->add_region = (Rect){layout->queue_region.x,
                                            layout->queue_region.y +
                                            layout->queue_region.height + 8.0,
                                            right_width, add_height};
        } else {
            layout->show_add_form = content_height >=
                minimum_chamber + minimum_queue + 120.0 + gap * 2.0;
            layout->add_form_stacked = layout->show_add_form;
            double add_height = layout->show_add_form ? 120.0 : 0.0;
            double desired_chamber = CLAMP(width * 0.47, minimum_chamber, 440.0);
            double chamber_limit = content_height - minimum_queue - add_height -
                                   gap * (layout->show_add_form ? 2.0 : 1.0);
            double chamber_height = CLAMP(MIN(desired_chamber, chamber_limit),
                                          minimum_chamber, 440.0);
            double queue_height = content_height - chamber_height - add_height -
                                  gap * (layout->show_add_form ? 2.0 : 1.0);
            layout->visible_queue_rows = queue_height >= 250.0 ? 4 :
                                         queue_height >= 170.0 ? 3 : 2;
            layout->focus_chamber = (Rect){margin, content_y,
                                           width - margin * 2.0,
                                           chamber_height};
            layout->queue_region = (Rect){margin,
                                          layout->focus_chamber.y +
                                          chamber_height + gap,
                                          width - margin * 2.0, queue_height};
            if (layout->show_add_form)
                layout->add_region = (Rect){margin,
                                            layout->queue_region.y +
                                            queue_height + gap,
                                            width - margin * 2.0, add_height};
        }
    } else {
        layout->focus_composition = FOCUS_COMPOSITION_MINIMAL;
        layout->visible_queue_rows = 0;
        layout->visible_ledger_rows = 2;
        layout->show_add_form = FALSE;
        layout->show_ledger_chart = FALSE;
        layout->show_advanced_calibration = FALSE;
        layout->focus_chamber = (Rect){margin, content_y,
                                       width - margin * 2.0, content_height};
    }

    if (layout->focus_chamber.width > 1.0) {
        Rect chamber = layout->focus_chamber;
        double dial_size;
        double dial_x;
        double dial_y;
        double details_x;
        double details_y;
        double details_width;
        double details_height;

        if (layout->focus_composition == FOCUS_COMPOSITION_DESKTOP) {
            dial_size = CLAMP(MIN(chamber.width * 0.42,
                                  chamber.height * 0.62),
                              290.0, 450.0);
            dial_x = chamber.x + CLAMP(chamber.width * 0.048, 46.0, 58.0);
            dial_y = chamber.y + (chamber.height - dial_size) * 0.5;
            details_x = dial_x + dial_size +
                        CLAMP(chamber.width * 0.045, 42.0, 56.0);
            details_width = chamber.x + chamber.width - details_x - 42.0;
            details_height = MIN(374.0, chamber.height - 96.0);
            details_y = chamber.y + (chamber.height - details_height) * 0.5;
        } else if (layout->focus_composition ==
                   FOCUS_COMPOSITION_WIDE_COMPACT) {
            dial_size = CLAMP(MIN(chamber.width * 0.31,
                                  chamber.height - 122.0),
                              124.0, 172.0);
            dial_x = chamber.x + 18.0;
            dial_y = chamber.y + 44.0;
            details_x = dial_x + dial_size + 24.0;
            details_y = chamber.y + 46.0;
            details_width = chamber.x + chamber.width - details_x - 18.0;
            details_height = chamber.height - 100.0;
        } else if (layout->focus_composition == FOCUS_COMPOSITION_STACKED) {
            dial_size = CLAMP(MIN(chamber.width * 0.30,
                                  chamber.height - 130.0),
                              170.0, 250.0);
            dial_x = chamber.x + 28.0;
            /* Centre the dial above the action band, leaving room for the
             * gauge ticks outside its nominal rectangle. */
            dial_y = chamber.y + 40.0 + (chamber.height - 114.0 - dial_size) * 0.5;
            details_x = dial_x + dial_size + 36.0;
            details_y = chamber.y + 52.0;
            details_width = chamber.x + chamber.width - details_x - 30.0;
            details_height = chamber.height - 126.0;
        } else {
            dial_size = CLAMP(MIN(chamber.width * 0.25,
                                  chamber.height - 78.0),
                              78.0, 112.0);
            dial_x = chamber.x + 14.0;
            dial_y = chamber.y + 28.0;
            details_x = dial_x + dial_size + 18.0;
            details_y = chamber.y + 40.0;
            details_width = chamber.x + chamber.width - details_x - 14.0;
            details_height = chamber.height - 86.0;
        }

        layout->focus_dial = (Rect){dial_x, dial_y, dial_size, dial_size};
        layout->focus_details = (Rect){details_x, details_y,
                                       MAX(details_width, 72.0),
                                       MAX(details_height, 64.0)};

        double button_y = chamber.y + chamber.height -
                          (layout->focus_composition == FOCUS_COMPOSITION_MINIMAL
                               ? 42.0
                               : layout->focus_composition ==
                                     FOCUS_COMPOSITION_WIDE_COMPACT
                                   ? 50.0
                                   : 58.0);
        double primary_width = layout->focus_composition ==
                                       FOCUS_COMPOSITION_WIDE_COMPACT
                                   ? 112.0
                                   : layout->density == UI_DENSITY_MINIMAL
                                       ? 110.0
                                       : 138.0;
        double secondary_width = layout->focus_composition ==
                                         FOCUS_COMPOSITION_WIDE_COMPACT
                                     ? 102.0
                                     : layout->density == UI_DENSITY_MINIMAL
                                         ? 100.0
                                         : 120.0;
        double action_width = primary_width + secondary_width + 8.0;
        if (layout->density != UI_DENSITY_MINIMAL)
            action_width += (layout->focus_composition ==
                                     FOCUS_COMPOSITION_WIDE_COMPACT
                                 ? 60.0 + 64.0
                                 : 72.0 + 78.0) + 16.0;
        if (layout->focus_composition == FOCUS_COMPOSITION_DESKTOP) {
            button_y = MIN(layout->focus_details.y +
                               layout->focus_details.height - 42.0,
                           chamber.y + chamber.height - 58.0);
        }
        double button_x = layout->focus_details.width >= action_width + 4.0 &&
                          layout->focus_composition !=
                              FOCUS_COMPOSITION_WIDE_COMPACT
            ? layout->focus_details.x
            : chamber.x + 14.0;
        layout->focus_primary = (Rect){button_x,
                                       button_y, primary_width, 42.0};
        layout->focus_secondary = (Rect){layout->focus_primary.x +
                                         primary_width + 8.0,
                                         button_y, secondary_width, 42.0};
        if (layout->density != UI_DENSITY_MINIMAL) {
            double add_5_width = layout->focus_composition ==
                                         FOCUS_COMPOSITION_WIDE_COMPACT
                                     ? 60.0
                                     : 72.0;
            double add_15_width = layout->focus_composition ==
                                          FOCUS_COMPOSITION_WIDE_COMPACT
                                      ? 64.0
                                      : 78.0;
            layout->focus_add_5 = (Rect){layout->focus_secondary.x +
                                         secondary_width + 8.0,
                                         button_y, add_5_width, 42.0};
            layout->focus_add_15 = (Rect){layout->focus_add_5.x +
                                          add_5_width + 8.0,
                                          button_y, add_15_width, 42.0};
        }
    }

    if (layout->queue_region.width > 1.0 && layout->visible_queue_rows > 0) {
        double queue_header = layout->density == UI_DENSITY_FULL ? 58.0 : 36.0;
        double row_height = (layout->queue_region.height - queue_header - 4.0) /
                            layout->visible_queue_rows;
        for (guint i = 0; i < layout->visible_queue_rows; ++i) {
            layout->queue_rows[i] = (Rect){layout->queue_region.x + 8.0,
                                           layout->queue_region.y + queue_header +
                                           i * row_height,
                                           layout->queue_region.width - 16.0,
                                           row_height - 2.0};
            double action_height = layout->density == UI_DENSITY_FULL ? 32.0 : 30.0;
            double action_y = layout->queue_rows[i].y +
                              (layout->queue_rows[i].height - action_height) * 0.5;
            double delete_width = layout->focus_composition ==
                                          FOCUS_COMPOSITION_WIDE_COMPACT
                                      ? 26.0
                                      : 30.0;
            double done_width = layout->focus_composition ==
                                        FOCUS_COMPOSITION_WIDE_COMPACT
                                    ? 50.0
                                    : 56.0;
            double primary_width = layout->focus_composition ==
                                           FOCUS_COMPOSITION_WIDE_COMPACT
                                       ? 58.0
                                       : 64.0;
            layout->queue_delete[i] = (Rect){layout->queue_rows[i].x +
                                             layout->queue_rows[i].width -
                                             delete_width - 2.0,
                                             action_y, delete_width, action_height};
            layout->queue_done[i] = (Rect){layout->queue_delete[i].x -
                                           done_width - 6.0,
                                           action_y, done_width, action_height};
            layout->queue_primary[i] = (Rect){layout->queue_done[i].x -
                                              primary_width - 6.0,
                                              action_y, primary_width, action_height};
        }
    }

    if (layout->show_add_form) {
        double field_height = layout->density == UI_DENSITY_FULL ? 42.0 : 36.0;
        if (layout->add_form_stacked) {
            double top_pad = layout->density == UI_DENSITY_FULL ? 54.0 : 28.0;
            double field_x = layout->add_region.x + 12.0;
            double field_width = layout->add_region.width - 24.0;
            layout->title_input = (Rect){field_x,
                                         layout->add_region.y + top_pad,
                                         field_width, field_height};
            double second_y = layout->title_input.y + field_height + 8.0;
            double duration_width = layout->density == UI_DENSITY_FULL
                ? MIN(132.0, field_width * 0.38)
                : MIN(104.0, field_width * 0.38);
            layout->duration_input = (Rect){field_x, second_y,
                                            duration_width, field_height};
            layout->add_button = (Rect){field_x + duration_width + 8.0,
                                        second_y,
                                        field_width - duration_width - 8.0,
                                        field_height};
        } else {
            double field_y = layout->add_region.y +
                             (layout->density == UI_DENSITY_FULL ? 38.0 : 14.0);
            double add_width = layout->density == UI_DENSITY_FULL ? 122.0 : 96.0;
            double duration_width = layout->density == UI_DENSITY_FULL ? 142.0 : 112.0;
            layout->title_input = (Rect){layout->add_region.x + 14.0, field_y,
                                         layout->add_region.width - add_width -
                                         duration_width - 50.0,
                                         field_height};
            layout->duration_input = (Rect){layout->title_input.x +
                                            layout->title_input.width + 8.0,
                                            field_y, duration_width, field_height};
            layout->add_button = (Rect){layout->duration_input.x +
                                        duration_width + 8.0,
                                        field_y, add_width, field_height};
        }
    }

    /* Ledger layout is density-aware but row-height driven.  The previous
     * implementation divided all remaining height among a small fixed row count,
     * producing vast empty rows on tall tiles and tiny, widely separated text on
     * desktop.  Keep the analytical header compact, constrain the chart, and
     * allocate ledger rows at a stable reading height. */
    double summary_height = layout->density == UI_DENSITY_FULL
        ? 112.0
        : layout->density == UI_DENSITY_COMPACT
            ? (layout->show_ledger_chart ? 88.0 : 78.0)
            : 70.0;
    layout->ledger_summary = (Rect){margin, content_y,
                                    width - margin * 2.0, summary_height};

    double action_gap = layout->density == UI_DENSITY_FULL ? 8.0 : 6.0;
    double action_height = layout->density == UI_DENSITY_FULL ? 40.0 : 34.0;
    double import_width = layout->density == UI_DENSITY_FULL ? 104.0 : 72.0;
    double export_width = layout->density == UI_DENSITY_FULL ? 104.0 : 72.0;
    double clear_width = layout->density == UI_DENSITY_FULL ? 126.0 : 88.0;
    double ledger_actions_width = import_width + export_width +
                                  clear_width + action_gap * 2.0;
    double action_y = layout->ledger_summary.y +
                      (summary_height - action_height) * 0.5;
    layout->import_button = (Rect){layout->ledger_summary.x +
                                   layout->ledger_summary.width -
                                   ledger_actions_width - 10.0,
                                   action_y, import_width, action_height};
    layout->export_button = (Rect){layout->import_button.x + import_width +
                                   action_gap, action_y,
                                   export_width, action_height};
    layout->clear_button = (Rect){layout->export_button.x + export_width +
                                  action_gap, action_y,
                                  clear_width, action_height};

    double section_gap = layout->density == UI_DENSITY_FULL ? 12.0 : 8.0;
    double ledger_y = layout->ledger_summary.y + summary_height + section_gap;
    if (layout->show_ledger_chart) {
        double chart_height = layout->density == UI_DENSITY_FULL ? 96.0 : 82.0;
        layout->ledger_chart = (Rect){margin + 14.0, ledger_y,
                                      width - margin * 2.0 - 28.0,
                                      chart_height};
        ledger_y += chart_height + section_gap;
    }
    layout->ledger_region = (Rect){margin, ledger_y,
                                   width - margin * 2.0,
                                   MAX(content_bottom - ledger_y, 1.0)};

    double ledger_header = layout->density == UI_DENSITY_FULL ? 48.0 : 42.0;
    double preferred_row_height = layout->density == UI_DENSITY_FULL
        ? 108.0
        : layout->density == UI_DENSITY_COMPACT ? 92.0 : 84.0;
    double row_space = MAX(layout->ledger_region.height - ledger_header - 8.0,
                           1.0);
    guint row_capacity = (guint)floor(row_space / preferred_row_height);
    if (row_space >= 148.0)
        row_capacity = MAX(row_capacity, 2u);
    row_capacity = CLAMP(row_capacity, 1u, (guint)CHRONO_VISIBLE_LEDGER_ROWS);
    layout->visible_ledger_rows = row_capacity;
    double ledger_row_height = MIN(preferred_row_height,
                                   row_space / row_capacity);
    for (guint i = 0; i < layout->visible_ledger_rows; ++i) {
        layout->ledger_rows[i] = (Rect){layout->ledger_region.x + 10.0,
                                        layout->ledger_region.y + ledger_header +
                                        i * ledger_row_height,
                                        layout->ledger_region.width - 20.0,
                                        ledger_row_height - 3.0};
        double delete_size = layout->density == UI_DENSITY_FULL ? 32.0 : 30.0;
        layout->ledger_delete[i] = (Rect){layout->ledger_rows[i].x +
                                          layout->ledger_rows[i].width -
                                          delete_size - 4.0,
                                          layout->ledger_rows[i].y +
                                          (layout->ledger_rows[i].height -
                                           delete_size) * 0.5,
                                          delete_size, delete_size};
    }

    /* Calibration is a control surface, not a continuously animated scene.
     * Keep the controls centred and bounded on 1080p-class desktops instead
     * of spending half the screen on a reference graph. */
    double available_control_width = width - margin * 2.0 - 12.0;
    double control_width = layout->density == UI_DENSITY_FULL
        ? MIN(available_control_width, 1120.0)
        : available_control_width;
    double control_x = margin + (available_control_width - control_width) * 0.5;
    gboolean minimal_calibration = layout->density == UI_DENSITY_MINIMAL;
    layout->show_advanced_calibration = TRUE;
    layout->calibration_viewport = (Rect){margin, content_y - 4.0,
        width - margin * 2.0, content_height + 4.0};

    double profile_gap = 6.0;
    double profile_width = (control_width - profile_gap * 3.0) / 4.0;
    for (int i = 0; i < CHRONO_PRESENTATION_MODE_COUNT; ++i)
        layout->presentation_button[i] =
            (Rect){control_x + i * (profile_width + profile_gap),
                   content_y, profile_width, minimal_calibration ? 28.0 : 30.0};

    double cursor_y = content_y + (minimal_calibration ? 34.0 : 42.0);
    if (minimal_calibration) {
        double toggle_width = (control_width - 12.0) / 3.0;
        layout->mask_checkbox = (Rect){control_x, cursor_y, toggle_width, 24.0};
        layout->curvature_checkbox = (Rect){control_x + toggle_width + 6.0,
                                             cursor_y, toggle_width, 24.0};
        layout->cursor_shader_effects_toggle =
            (Rect){control_x + 2.0 * (toggle_width + 6.0), cursor_y,
                   toggle_width, 24.0};
        cursor_y += 40.0;
    } else {
        layout->mask_checkbox = (Rect){control_x, cursor_y, control_width, 24.0};
        layout->curvature_checkbox = (Rect){control_x, cursor_y + 27.0,
                                            control_width, 24.0};
        layout->cursor_shader_effects_toggle =
            (Rect){control_x, cursor_y + 54.0, control_width, 24.0};
        cursor_y += 84.0;
    }
    if (layout->show_advanced_calibration) {
        cursor_y += 16.0;
        double slider_spacing = 34.0;
        for (int i = 0; i < CHRONO_SLIDER_COUNT; ++i) {
            layout->slider_track[i] =
                (Rect){control_x, cursor_y + i * slider_spacing,
                       control_width, 16.0};
        }
        cursor_y += CHRONO_SLIDER_COUNT * slider_spacing + 4.0;
        layout->notifications_toggle = (Rect){control_x, cursor_y,
                                              control_width, 24.0};
        layout->sound_toggle = (Rect){control_x, cursor_y + 27.0,
                                      control_width, 24.0};
        cursor_y += 57.0;
    }

    {
        double aa_width = (control_width - 18.0) / 4.0;
        for (int i = 0; i < CHRONO_AA_MODE_COUNT; ++i)
            layout->aa_button[i] =
                (Rect){control_x + i * (aa_width + 6.0),
                       cursor_y, aa_width, 25.0};
        cursor_y += 50.0;
    }

    int palette_columns = minimal_calibration ? 4 : 6;
    double palette_gap = layout->density == UI_DENSITY_MINIMAL ? 3.0 : 5.0;
    double palette_width = (control_width -
                            palette_gap * (palette_columns - 1)) /
                           palette_columns;
    double palette_height = layout->density == UI_DENSITY_MINIMAL ? 24.0 : 25.0;
    // Preserve the original two rows; the five additions start a new row.
    int old_rows = (12 + palette_columns - 1) / palette_columns;
    int new_columns = minimal_calibration ? 4 : 5;
    for (int i = 0; i < CHRONO_PALETTE_COUNT; ++i) {
        int column = i < 12 ? i % palette_columns : (i - 12) % new_columns;
        int row = i < 12 ? i / palette_columns : old_rows + (i - 12) / new_columns;
        double button_width = i < 12 ? palette_width :
            (control_width - palette_gap * (new_columns - 1)) / new_columns;
        layout->palette_button[i] = (Rect){
            control_x + column * (button_width + palette_gap),
            cursor_y + row * (palette_height + 4.0), button_width, palette_height};
    }
    int palette_rows = old_rows + (5 + new_columns - 1) / new_columns;
    cursor_y += palette_rows * (palette_height + 4.0) + 8.0;
    double reset_width = layout->density == UI_DENSITY_MINIMAL
        ? control_width
        : MAX(control_width * 0.34, 116.0);
    layout->reset_button = (Rect){control_x, cursor_y,
                                  reset_width, minimal_calibration ? 24.0 : 28.0};
    layout->calibration_max_scroll = MAX(0.0, cursor_y + layout->reset_button.height + 4.0 - content_bottom);
    ui_layout_scroll_calibration(layout, previous_scroll);
    return density_changed;
}

Rect app_routine_control(const UiLayout *l, guint i)
{
    double x = l->status_rail.x;
    double w = l->status_rail.width;
    double y = l->modes[0].y + l->modes[0].height + 14;
    /* Three navigation controls, four operation controls. */
    if (i < 3) return (Rect){x + i * (w + 6) / 3, y, (w - 12) / 3, 30};
    return (Rect){x + (i - 3) * (w + 6) / 4,
        l->annunciator.y - 43, (w - 18) / 4, 30};
}

void ui_layout_scroll_calibration(UiLayout *l, double amount)
{
    double next = CLAMP(isfinite(amount) ? amount : 0, 0, l->calibration_max_scroll);
    double dy = l->calibration_scroll - next;
    l->calibration_scroll = next;
    for (guint i = 0; i < CHRONO_PRESENTATION_MODE_COUNT; ++i) l->presentation_button[i].y += dy;
    for (guint i = 0; i < CHRONO_SLIDER_COUNT; ++i) l->slider_track[i].y += dy;
    for (guint i = 0; i < CHRONO_PALETTE_COUNT; ++i) l->palette_button[i].y += dy;
    for (guint i = 0; i < CHRONO_AA_MODE_COUNT; ++i) l->aa_button[i].y += dy;
    l->mask_checkbox.y += dy; l->curvature_checkbox.y += dy;
    l->cursor_shader_effects_toggle.y += dy; l->notifications_toggle.y += dy;
    l->sound_toggle.y += dy; l->reset_button.y += dy;
}
