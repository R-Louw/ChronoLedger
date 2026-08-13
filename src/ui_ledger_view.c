#include "ui_views.h"
#include "ui_painter.h"
#include "time_parse.h"

void
draw_ledger_view(AppState *state,
                 const AppViewSnapshot *view,
                 cairo_t *cr,
                 PangoLayout *layout)
{
    ChronoStats stats = view->completed_stats;
    guint done_count = view->completed_task_count;
    Rect summary = state->presentation.layout.ledger_summary;
    char *completed = g_strdup_printf("%u", stats.completed_count);
    char *actual = chrono_format_compact_duration(stats.total_elapsed_seconds);
    char *delta = format_delta(stats.total_estimate_seconds,
                               stats.total_elapsed_seconds);

    if (state->presentation.layout.density == UI_DENSITY_FULL) {
        double metric_area_width = MAX(state->presentation.layout.import_button.x -
                                       summary.x - 34.0, 540.0);
        double metric_width = metric_area_width / 3.0;
        double metric_x[3] = {
            summary.x + 18.0,
            summary.x + 18.0 + metric_width,
            summary.x + 18.0 + metric_width * 2.0,
        };
        draw_text(state, cr, layout, FONT_MONO, completed, 33.0,
                  PANGO_WEIGHT_BOLD, metric_x[0], summary.y + 59.0,
                  metric_width - 18.0, TEXT_LEFT,
                  0.58, 1.0, 0.71, 0.98);
        draw_text(state, cr, layout, FONT_MONO, "COMPLETED", 9.2,
                  PANGO_WEIGHT_BOLD, metric_x[0], summary.y + 86.0,
                  metric_width - 18.0, TEXT_LEFT,
                  0.28, 0.62, 0.44, 0.68);
        draw_text(state, cr, layout, FONT_MONO, actual, 27.0,
                  PANGO_WEIGHT_BOLD, metric_x[1], summary.y + 59.0,
                  metric_width - 18.0, TEXT_LEFT,
                  0.52, 0.94, 0.66, 0.96);
        draw_text(state, cr, layout, FONT_MONO, "ACTUAL FOCUS", 9.2,
                  PANGO_WEIGHT_BOLD, metric_x[1], summary.y + 86.0,
                  metric_width - 18.0, TEXT_LEFT,
                  0.28, 0.62, 0.44, 0.68);
        draw_text(state, cr, layout, FONT_MONO, delta, 21.0,
                  PANGO_WEIGHT_BOLD, metric_x[2], summary.y + 59.0,
                  metric_width - 18.0, TEXT_LEFT,
                  stats.total_elapsed_seconds > stats.total_estimate_seconds
                      ? 1.0 : 0.46,
                  stats.total_elapsed_seconds > stats.total_estimate_seconds
                      ? 0.50 : 0.90,
                  stats.total_elapsed_seconds > stats.total_estimate_seconds
                      ? 0.28 : 0.62,
                  0.94);
        draw_text(state, cr, layout, FONT_MONO, "AGGREGATE DELTA", 9.2,
                  PANGO_WEIGHT_BOLD, metric_x[2], summary.y + 86.0,
                  metric_width - 18.0, TEXT_LEFT,
                  0.28, 0.62, 0.44, 0.68);
    } else {
        char *line = g_strdup_printf("%s LOGGED  ·  %s ACTUAL  ·  %s",
                                     completed, actual, delta);
        draw_text(state, cr, layout, FONT_MONO, line,
                  state->presentation.layout.density == UI_DENSITY_MINIMAL ? 10.8 : 12.3,
                  PANGO_WEIGHT_BOLD, summary.x + 16.0,
                  summary.y + (state->presentation.layout.density == UI_DENSITY_MINIMAL
                                   ? summary.height * 0.56
                                   : 48.0),
                  MAX(state->presentation.layout.import_button.x - summary.x - 28.0, 120.0),
                  TEXT_LEFT, 0.52, 0.94, 0.66, 0.94);
        g_free(line);
    }
    g_free(completed);
    g_free(actual);
    g_free(delta);

    double action_font = state->presentation.layout.density == UI_DENSITY_FULL ? 12.5 : 11.2;
    draw_button_sized(state, cr, layout, state->presentation.layout.import_button,
                      state->presentation.layout.density == UI_DENSITY_FULL
                          ? "IMPORT JSON" : "IMPORT",
                      FALSE, FALSE, FALSE, TRUE, action_font);
    draw_button_sized(state, cr, layout, state->presentation.layout.export_button,
                      state->presentation.layout.density == UI_DENSITY_FULL
                          ? "EXPORT JSON" : "EXPORT",
                      FALSE, FALSE, FALSE, TRUE, action_font);
    draw_button_sized(state, cr, layout, state->presentation.layout.clear_button,
                      state->presentation.layout.density == UI_DENSITY_FULL
                          ? "CLEAR LOGGED" : "CLEAR",
                      FALSE, FALSE, TRUE, done_count > 0, action_font);

    if (done_count > 0) {
        guint first = MIN(state->session.ledger_offset + 1u, done_count);
        guint last = MIN(state->session.ledger_offset +
                         state->presentation.layout.visible_ledger_rows, done_count);
        char *range = g_strdup_printf("SHOWING %u–%u OF %u · RECENT FIRST",
                                      first, last, done_count);
        draw_text(state, cr, layout, FONT_MONO, range,
                  state->presentation.layout.density == UI_DENSITY_FULL ? 9.2 : 8.4,
                  PANGO_WEIGHT_BOLD,
                  state->presentation.layout.ledger_region.x +
                      state->presentation.layout.ledger_region.width * 0.48,
                  state->presentation.layout.ledger_region.y +
                      (state->presentation.layout.density == UI_DENSITY_FULL ? 22.0 : 19.0),
                  state->presentation.layout.ledger_region.width * 0.48 - 16.0,
                  TEXT_RIGHT, 0.28, 0.62, 0.44, 0.64);
        g_free(range);
    }

    if (done_count == 0) {
        draw_text(state, cr, layout, FONT_SANS,
                  "Complete an allocation to begin the ledger.",
                  state->presentation.layout.density == UI_DENSITY_FULL ? 16.0 : 14.0,
                  PANGO_WEIGHT_MEDIUM,
                  state->presentation.layout.ledger_region.x + 18.0,
                  state->presentation.layout.ledger_region.y + 78.0,
                  state->presentation.layout.ledger_region.width - 36.0,
                  TEXT_LEFT, 0.36, 0.70, 0.51, 0.70);
        return;
    }

    for (guint i = 0; i < state->presentation.layout.visible_ledger_rows; ++i) {
        const AppTaskView *task = i < view->ledger_row_count
            ? &view->ledger_rows[i]
            : NULL;
        if (task == NULL)
            continue;
        Rect row = state->presentation.layout.ledger_rows[i];
        fill_rounded(cr, row, 7.0, 0.10, 0.28, 0.20, 0.022);
        draw_separator(cr, row.x + 4.0, row.y + row.height,
                       row.x + row.width - 4.0, row.y + row.height, 0.13);

        char *actual_text = chrono_format_compact_duration(task->elapsed_seconds);
        char *estimate_text = chrono_format_compact_duration(task->estimate_seconds);
        char *ratio = format_delta(task->estimate_seconds, task->elapsed_seconds);
        char *row_title = format_task_display_title(task);
        char *time_line = g_strdup_printf(
            state->presentation.layout.density == UI_DENSITY_FULL
                ? "ACTUAL %s  ·  ESTIMATE %s"
                : "ACT %s  ·  EST %s",
            actual_text, estimate_text);

        double ratio_width = state->presentation.layout.density == UI_DENSITY_FULL
            ? 174.0 : 132.0;
        double ratio_x = state->presentation.layout.ledger_delete[i].x - ratio_width - 14.0;
        double text_x = row.x + 14.0;
        double text_width = MAX(ratio_x - text_x - 18.0, 120.0);
        double title_y = row.y + row.height * 0.38;
        double metadata_y = row.y + row.height * 0.67;

        draw_text(state, cr, layout, FONT_SANS, row_title,
                  state->presentation.layout.density == UI_DENSITY_FULL ? 15.5 :
                  state->presentation.layout.density == UI_DENSITY_COMPACT ? 13.4 : 12.2,
                  PANGO_WEIGHT_BOLD, text_x, title_y, text_width,
                  TEXT_LEFT, 0.50, 0.91, 0.64, 0.94);
        draw_text(state, cr, layout, FONT_MONO, time_line,
                  state->presentation.layout.density == UI_DENSITY_FULL ? 10.8 : 9.8,
                  PANGO_WEIGHT_MEDIUM, text_x, metadata_y, text_width,
                  TEXT_LEFT, 0.36, 0.76, 0.54, 0.80);
        draw_text(state, cr, layout, FONT_MONO, ratio,
                  state->presentation.layout.density == UI_DENSITY_FULL ? 11.5 : 10.2,
                  PANGO_WEIGHT_BOLD, ratio_x,
                  row.y + row.height * 0.47, ratio_width,
                  TEXT_RIGHT,
                  task->elapsed_seconds > task->estimate_seconds ? 1.0 : 0.46,
                  task->elapsed_seconds > task->estimate_seconds ? 0.50 : 0.90,
                  task->elapsed_seconds > task->estimate_seconds ? 0.28 : 0.62,
                  0.92);
        if (state->presentation.layout.density == UI_DENSITY_FULL)
            draw_text(state, cr, layout, FONT_MONO, "ESTIMATE DELTA", 8.0,
                      PANGO_WEIGHT_BOLD, ratio_x,
                      row.y + row.height * 0.68, ratio_width,
                      TEXT_RIGHT, 0.28, 0.62, 0.44, 0.58);

        draw_button_sized(state, cr, layout, state->presentation.layout.ledger_delete[i],
                          "×",
                          FALSE, FALSE, TRUE, TRUE,
                          state->presentation.layout.density == UI_DENSITY_FULL ? 13.0 : 12.0);
        g_free(row_title);
        g_free(time_line);
        g_free(actual_text);
        g_free(estimate_text);
        g_free(ratio);
    }
}
