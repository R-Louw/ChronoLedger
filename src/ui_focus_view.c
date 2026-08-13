#include "ui_views.h"
#include "ui_painter.h"
#include "time_parse.h"

void
draw_focus_view(AppState *state,
                const AppViewSnapshot *view,
                cairo_t *cr,
                PangoLayout *layout)
{
    const AppTaskView *task = view->has_focus_task
        ? &view->focus_task
        : NULL;
    Rect chamber = state->presentation.layout.focus_chamber;
    Rect dial = state->presentation.layout.focus_dial;
    Rect details = state->presentation.layout.focus_details;
    double text_x = details.x;
    double text_width = MAX(details.width, 80.0);
    double status_y = details.y + 12.0;
    double title_y = details.y +
        (state->presentation.layout.focus_composition == FOCUS_COMPOSITION_DESKTOP ? 76.0 :
         state->presentation.layout.focus_composition == FOCUS_COMPOSITION_WIDE_COMPACT ? 58.0 :
         state->presentation.layout.focus_composition == FOCUS_COMPOSITION_STACKED ? 66.0 : 42.0);
    double title_size =
        state->presentation.layout.focus_composition == FOCUS_COMPOSITION_DESKTOP
            ? (text_width >= 500.0 ? 30.0 : text_width >= 380.0 ? 27.0 : 24.0) :
        state->presentation.layout.focus_composition == FOCUS_COMPOSITION_WIDE_COMPACT ? 19.5 :
        state->presentation.layout.focus_composition == FOCUS_COMPOSITION_STACKED ? 22.0 : 15.5;
    double timer_size = CLAMP(dial.width * 0.11, 28.0, 50.0);

    if (task == NULL) {
        draw_text(state, cr, layout, FONT_MONO, "CHAMBER IDLE",
                  state->presentation.layout.density == UI_DENSITY_FULL ? 12.0 : 10.8,
                  PANGO_WEIGHT_BOLD, text_x, status_y,
                  text_width, TEXT_LEFT,
                  0.34, 0.72, 0.50, 0.75);
        draw_text(state, cr, layout, FONT_SANS,
                  state->presentation.layout.density == UI_DENSITY_MINIMAL
                      ? "No allocation selected."
                      : "Ready for the next allocation.",
                  title_size, PANGO_WEIGHT_BOLD, text_x, title_y,
                  text_width, TEXT_LEFT,
                  0.52, 0.94, 0.66, 0.88);
        if (state->presentation.layout.focus_composition == FOCUS_COMPOSITION_DESKTOP) {
            draw_text(state, cr, layout, FONT_MONO,
                      text_width >= 430.0
                          ? "01  COMMIT     02  ENGAGE     03  COMPLETE"
                          : "COMMIT  →  ENGAGE  →  COMPLETE",
                      12.5, PANGO_WEIGHT_BOLD, text_x, title_y + 88.0,
                      text_width, TEXT_LEFT,
                      0.34, 0.74, 0.51, 0.76);
            if (text_width >= 430.0)
                draw_text(state, cr, layout, FONT_SANS,
                          "Create a task in the allocation bay, then run it through the chamber.",
                          13.0, PANGO_WEIGHT_MEDIUM, text_x, title_y + 124.0,
                          text_width, TEXT_LEFT,
                          0.35, 0.68, 0.50, 0.66);
        } else if (state->presentation.layout.density != UI_DENSITY_MINIMAL) {
            draw_text(state, cr, layout, FONT_MONO,
                      "COMMIT  →  ENGAGE  →  COMPLETE",
                      10.0, PANGO_WEIGHT_BOLD, text_x, title_y + 54.0,
                      text_width, TEXT_LEFT,
                      0.31, 0.67, 0.47, 0.66);
        }
        draw_text(state, cr, layout, FONT_MONO, "00:00", timer_size,
                  PANGO_WEIGHT_BOLD, dial.x,
                  dial.y + dial.height * 0.5,
                  dial.width, TEXT_CENTER,
                  0.28, 0.62, 0.44, 0.48);
    } else {
        double sr, sg, sb;
        status_colour(task->status, &sr, &sg, &sb);
        Rect status_rect = {text_x, details.y,
                            state->presentation.layout.density == UI_DENSITY_MINIMAL ? 82.0 :
                            state->presentation.layout.density == UI_DENSITY_FULL ? 116.0 : 106.0,
                            state->presentation.layout.density == UI_DENSITY_MINIMAL ? 22.0 :
                            state->presentation.layout.density == UI_DENSITY_FULL ? 28.0 : 26.0};
        draw_status_word(state, cr, layout, status_word(task->status),
                         status_rect, sr, sg, sb);
        char *display_title = format_task_display_title(task);
        draw_text(state, cr, layout, FONT_SANS, display_title, title_size,
                  PANGO_WEIGHT_BOLD, text_x, title_y,
                  text_width, TEXT_LEFT, 0.62, 1.0, 0.73, 0.98);
        g_free(display_title);

        gint64 remaining = task->effective_remaining_seconds;
        gint64 elapsed = task->effective_elapsed_seconds;
        char *remaining_text = chrono_format_duration(remaining);
        draw_text(state, cr, layout, FONT_MONO, remaining_text, timer_size,
                  PANGO_WEIGHT_BOLD, dial.x,
                  dial.y + dial.height * 0.5,
                  dial.width, TEXT_CENTER,
                  sr, sg, sb, 0.98);
        g_free(remaining_text);

        char *elapsed_text = chrono_format_compact_duration(elapsed);
        char *estimate_text = chrono_format_compact_duration(task->estimate_seconds);
        char *allocated_text = chrono_format_compact_duration(task->allocated_seconds);
        char *line1 = g_strdup_printf("ACTUAL %s  ·  EST %s",
                                      elapsed_text, estimate_text);
        char *line2 = g_strdup_printf("ALLOCATED %s", allocated_text);
        double meta_y = title_y +
            (state->presentation.layout.focus_composition == FOCUS_COMPOSITION_DESKTOP ? 64.0 :
             state->presentation.layout.focus_composition == FOCUS_COMPOSITION_STACKED ? 49.0 : 40.0);
        draw_text(state, cr, layout, FONT_MONO, line1,
                  state->presentation.layout.density == UI_DENSITY_MINIMAL ? 8.8 :
                  state->presentation.layout.density == UI_DENSITY_FULL ? 13.0 : 11.5,
                  PANGO_WEIGHT_BOLD, text_x, meta_y,
                  text_width, TEXT_LEFT, 0.40, 0.82, 0.58, 0.82);
        if (state->presentation.layout.density != UI_DENSITY_MINIMAL)
            draw_text(state, cr, layout, FONT_MONO, line2,
                      state->presentation.layout.density == UI_DENSITY_FULL ? 11.5 : 10.5,
                      PANGO_WEIGHT_MEDIUM, text_x,
                      meta_y + (state->presentation.layout.density == UI_DENSITY_FULL ? 30.0 : 26.0),
                      text_width, TEXT_LEFT, 0.30, 0.66, 0.47, 0.68);
        if (state->presentation.layout.density == UI_DENSITY_FULL) {
            double fraction = task->allocated_seconds > 0
                ? CLAMP((double)elapsed / (double)task->allocated_seconds, 0.0, 1.0)
                : 0.0;
            char *progress = g_strdup_printf("ALLOCATION USED  %3.0f%%", fraction * 100.0);
            draw_text(state, cr, layout, FONT_MONO, progress, 11.0,
                      PANGO_WEIGHT_BOLD, text_x, meta_y + 61.0,
                      text_width, TEXT_LEFT, sr, sg, sb, 0.78);
            g_free(progress);
        }
        g_free(elapsed_text);
        g_free(estimate_text);
        g_free(allocated_text);
        g_free(line1);
        g_free(line2);
    }

    gboolean can_act = task != NULL && task->status != CHRONO_TASK_DONE;
    gboolean can_create = task == NULL && state->presentation.layout.show_add_form;
    const char *primary = task == NULL ? "NEW" : "ENGAGE";
    if (task != NULL && task->status == CHRONO_TASK_ACTIVE)
        primary = "HOLD";
    else if (task != NULL && task->status == CHRONO_TASK_EXPIRED)
        primary = "+05:00";
    double focus_button_font = state->presentation.layout.density == UI_DENSITY_FULL ? 14.0 :
                               state->presentation.layout.density == UI_DENSITY_COMPACT ? 13.5 : 12.5;
    draw_button_sized(state, cr, layout, state->presentation.layout.focus_primary, primary,
                      FALSE, TRUE, FALSE, can_act || can_create,
                      focus_button_font);
    draw_button_sized(state, cr, layout, state->presentation.layout.focus_secondary,
                      "COMPLETE", FALSE, FALSE, FALSE, can_act,
                      focus_button_font);
    if (state->presentation.layout.focus_add_5.width > 1.0)
        draw_button_sized(state, cr, layout, state->presentation.layout.focus_add_5,
                          "+5m", FALSE, FALSE, FALSE,
                          task != NULL && task->status != CHRONO_TASK_DONE,
                          focus_button_font);
    if (state->presentation.layout.focus_add_15.width > 1.0)
        draw_button_sized(state, cr, layout, state->presentation.layout.focus_add_15,
                          "+15m", FALSE, FALSE, FALSE,
                          task != NULL && task->status != CHRONO_TASK_DONE,
                          focus_button_font);

    guint open_count = view->open_task_count;
    for (guint i = 0; i < state->presentation.layout.visible_queue_rows; ++i) {
        const AppTaskView *row_task = i < view->queue_row_count
            ? &view->queue_rows[i]
            : NULL;
        Rect row = state->presentation.layout.queue_rows[i];
        if (row_task == NULL)
            continue;
        gboolean selected = task != NULL && g_strcmp0(task->id, row_task->id) == 0;
        if (selected)
            fill_rounded(cr, row, 7.0, 0.28, 0.78, 0.51, 0.085);
        draw_separator(cr, row.x, row.y + row.height,
                       row.x + row.width, row.y + row.height, 0.15);
        double sr, sg, sb;
        status_colour(row_task->status, &sr, &sg, &sb);
        draw_lamp(cr, row.x + 7.0, row.y + row.height * 0.5,
                  sr, sg, sb, row_task->status == CHRONO_TASK_ACTIVE ||
                              row_task->status == CHRONO_TASK_EXPIRED);
        char *remaining = chrono_format_compact_duration(
            row_task->effective_remaining_seconds);
        if (state->presentation.layout.density == UI_DENSITY_FULL) {
            char *row_title = format_task_display_title(row_task);
            draw_text(state, cr, layout, FONT_SANS, row_title, 14.5,
                      selected ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_MEDIUM,
                      row.x + 20.0, row.y + row.height * 0.36,
                      row.width - 270.0, TEXT_LEFT,
                      0.50, 0.91, 0.64, selected ? 0.98 : 0.84);
            g_free(row_title);
            char *meta = g_strdup_printf("%s · %s", status_word(row_task->status),
                                         remaining);
            draw_text(state, cr, layout, FONT_MONO, meta, 10.5,
                      PANGO_WEIGHT_MEDIUM, row.x + 20.0,
                      row.y + row.height * 0.70,
                      row.width - 270.0, TEXT_LEFT, sr, sg, sb, 0.68);
            g_free(meta);
        } else {
            char *row_title = format_task_display_title(row_task);
            char *line = g_strdup_printf("%s  ·  %s", row_title, remaining);
            g_free(row_title);
            double text_reserve = state->presentation.layout.focus_composition ==
                                          FOCUS_COMPOSITION_WIDE_COMPACT
                                      ? 182.0
                                      : 230.0;
            draw_text(state, cr, layout, FONT_SANS, line, 11.8,
                      selected ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_MEDIUM,
                      row.x + 18.0, row.y + row.height * 0.5,
                      row.width - text_reserve, TEXT_LEFT,
                      0.50, 0.91, 0.64, selected ? 0.98 : 0.84);
            g_free(line);
        }
        g_free(remaining);
        double queue_button_font = state->presentation.layout.density == UI_DENSITY_FULL
            ? 12.5 : 11.5;
        draw_button_sized(state, cr, layout, state->presentation.layout.queue_primary[i],
                          row_task->status == CHRONO_TASK_ACTIVE ? "HOLD" : "START",
                          FALSE,
                          row_task->status == CHRONO_TASK_ACTIVE,
                          FALSE, row_task->status != CHRONO_TASK_EXPIRED,
                          queue_button_font);
        draw_button_sized(state, cr, layout, state->presentation.layout.queue_done[i], "DONE",
                          FALSE, FALSE, FALSE, TRUE,
                          queue_button_font);
        draw_button_sized(state, cr, layout, state->presentation.layout.queue_delete[i], "×",
                          FALSE, FALSE, TRUE, TRUE,
                          queue_button_font);
    }

    if (state->presentation.layout.visible_queue_rows > 0 && open_count == 0) {
        Rect queue = state->presentation.layout.queue_region;
        double empty_y = queue.y + queue.height * 0.50;
        draw_text(state, cr, layout, FONT_MONO, "QUEUE CLEAR", 11.0,
                  PANGO_WEIGHT_BOLD, queue.x + 18.0, empty_y - 22.0,
                  queue.width - 36.0, TEXT_CENTER,
                  0.30, 0.66, 0.46, 0.70);
        draw_text(state, cr, layout, FONT_SANS, "No allocations are waiting.", 14.0,
                  PANGO_WEIGHT_MEDIUM, queue.x + 18.0, empty_y + 3.0,
                  queue.width - 36.0, TEXT_CENTER,
                  0.40, 0.77, 0.56, 0.72);
        if (state->presentation.layout.show_add_form)
            draw_text(state, cr, layout, FONT_MONO,
                      "Commit one in the bay below.", 9.5,
                      PANGO_WEIGHT_MEDIUM, queue.x + 18.0, empty_y + 30.0,
                      queue.width - 36.0, TEXT_CENTER,
                      0.25, 0.55, 0.40, 0.58);
    }

    if (state->presentation.layout.show_add_form) {
        draw_input_field(state, cr, layout, state->presentation.layout.title_input,
                         "TASK", state->session.draft_title, "What requires focus?",
                         EDIT_TITLE, view->monotonic_us);
        draw_input_field(state, cr, layout, state->presentation.layout.duration_input,
                         "ESTIMATE", state->session.draft_duration, "25m",
                         EDIT_DURATION, view->monotonic_us);
        draw_button_sized(state, cr, layout, state->presentation.layout.add_button, "COMMIT",
                          FALSE, TRUE, FALSE, TRUE,
                          state->presentation.layout.density == UI_DENSITY_FULL ? 14.0 : 13.0);
    } else if (state->presentation.layout.density == UI_DENSITY_MINIMAL) {
        draw_text(state, cr, layout, FONT_MONO,
                  "EXPAND TILE FOR QUEUE AND NEW ALLOCATIONS",
                  7.8, PANGO_WEIGHT_BOLD,
                  chamber.x + 14.0,
                  chamber.y + chamber.height - 56.0,
                  chamber.width - 28.0, TEXT_RIGHT,
                  0.28, 0.62, 0.44, 0.58);
    }
}
