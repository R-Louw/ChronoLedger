#include "ui_controls.h"

#include <math.h>

static gboolean
rect_contains(Rect rect, double x, double y)
{
    return isfinite(x) && isfinite(y) && rect.width > 0.0 && rect.height > 0.0 &&
           x >= rect.x && y >= rect.y &&
           x <= rect.x + rect.width && y <= rect.y + rect.height;
}

static UiControlRef
control(UiControlId id, guint index)
{
    return (UiControlRef){id, index};
}

static Rect
slider_hit_rect(const UiLayout *layout, guint index)
{
    Rect rect = layout->slider_track[index];
    rect.y -= 4.0;
    rect.height += 8.0;
    return rect;
}

UiControlRef
ui_control_hit_test(const AppPresentationState *presentation,
                    const AppSessionState *session,
                    double x,
                    double y)
{
    if (presentation == NULL || session == NULL ||
        !isfinite(x) || !isfinite(y))
        return ui_control_none();

    const UiLayout *layout = &presentation->layout;
    if (rect_contains(layout->window_resize, x, y))
        return control(UI_CONTROL_WINDOW_RESIZE, 0);

    for (guint i = 0; i < CHRONO_MODE_COUNT; ++i) {
        if (rect_contains(layout->modes[i], x, y))
            return control(UI_CONTROL_MODE, i);
    }

    if (session->save_state == SAVE_STATE_FAILED &&
        rect_contains(layout->save_status, x, y))
        return control(UI_CONTROL_SAVE_RETRY, 0);

    if (presentation->active_tab == CHRONO_MODE_FOCUS) {
        if (rect_contains(layout->focus_primary, x, y))
            return control(UI_CONTROL_FOCUS_PRIMARY, 0);
        if (rect_contains(layout->focus_secondary, x, y))
            return control(UI_CONTROL_FOCUS_SECONDARY, 0);
        if (rect_contains(layout->focus_add_5, x, y))
            return control(UI_CONTROL_FOCUS_ADD_5, 0);
        if (rect_contains(layout->focus_add_15, x, y))
            return control(UI_CONTROL_FOCUS_ADD_15, 0);

        guint visible = MIN(layout->visible_queue_rows,
                            (guint)CHRONO_VISIBLE_QUEUE_ROWS);
        for (guint i = 0; i < visible; ++i) {
            if (rect_contains(layout->queue_delete[i], x, y))
                return control(UI_CONTROL_QUEUE_DELETE, i);
            if (rect_contains(layout->queue_done[i], x, y))
                return control(UI_CONTROL_QUEUE_DONE, i);
            if (rect_contains(layout->queue_primary[i], x, y))
                return control(UI_CONTROL_QUEUE_PRIMARY, i);
            if (rect_contains(layout->queue_rows[i], x, y))
                return control(UI_CONTROL_QUEUE_ROW, i);
        }

        if (rect_contains(layout->title_input, x, y))
            return control(UI_CONTROL_TITLE_INPUT, 0);
        if (rect_contains(layout->duration_input, x, y))
            return control(UI_CONTROL_DURATION_INPUT, 0);
        if (rect_contains(layout->add_button, x, y))
            return control(UI_CONTROL_ADD_TASK, 0);
    } else if (presentation->active_tab == CHRONO_MODE_LEDGER) {
        if (rect_contains(layout->import_button, x, y))
            return control(UI_CONTROL_IMPORT, 0);
        if (rect_contains(layout->export_button, x, y))
            return control(UI_CONTROL_EXPORT, 0);
        if (rect_contains(layout->clear_button, x, y))
            return control(UI_CONTROL_CLEAR_COMPLETED, 0);

        guint visible = MIN(layout->visible_ledger_rows,
                            (guint)CHRONO_VISIBLE_LEDGER_ROWS);
        for (guint i = 0; i < visible; ++i) {
            if (rect_contains(layout->ledger_delete[i], x, y))
                return control(UI_CONTROL_LEDGER_DELETE, i);
            if (rect_contains(layout->ledger_rows[i], x, y))
                return control(UI_CONTROL_LEDGER_ROW, i);
        }
    } else if (presentation->active_tab == CHRONO_MODE_CALIBRATION) {
        for (guint i = 0; i < CHRONO_PRESENTATION_MODE_COUNT; ++i) {
            if (rect_contains(layout->presentation_button[i], x, y))
                return control(UI_CONTROL_PRESENTATION, i);
        }
        for (guint i = 0; i < CHRONO_SLIDER_COUNT; ++i) {
            if (rect_contains(slider_hit_rect(layout, i), x, y))
                return control(UI_CONTROL_SLIDER, i);
        }
        if (rect_contains(layout->mask_checkbox, x, y))
            return control(UI_CONTROL_MASK, 0);
        if (rect_contains(layout->curvature_checkbox, x, y))
            return control(UI_CONTROL_CURVATURE, 0);
        if (rect_contains(layout->cursor_shader_effects_toggle, x, y))
            return control(UI_CONTROL_CURSOR_SHADER_EFFECTS, 0);
        if (rect_contains(layout->notifications_toggle, x, y))
            return control(UI_CONTROL_NOTIFICATIONS, 0);
        if (rect_contains(layout->sound_toggle, x, y))
            return control(UI_CONTROL_SOUND, 0);
        for (guint i = 0; i < CHRONO_AA_MODE_COUNT; ++i) {
            if (rect_contains(layout->aa_button[i], x, y))
                return control(UI_CONTROL_AA, i);
        }
        for (guint i = 0; i < CHRONO_PALETTE_COUNT; ++i) {
            if (rect_contains(layout->palette_button[i], x, y))
                return control(UI_CONTROL_PALETTE, i);
        }
        if (rect_contains(layout->reset_button, x, y))
            return control(UI_CONTROL_RESET_CALIBRATION, 0);
    }

    return ui_control_none();
}

gboolean
ui_control_get_bounds(const AppPresentationState *presentation,
                      const AppSessionState *session,
                      UiControlRef control_ref,
                      Rect *rect_out,
                      gboolean *danger_out)
{
    if (presentation == NULL || session == NULL ||
        control_ref.id == UI_CONTROL_NONE)
        return FALSE;

    const UiLayout *layout = &presentation->layout;
    Rect rect = {0};
    gboolean danger = FALSE;

    switch (control_ref.id) {
    case UI_CONTROL_WINDOW_RESIZE:
        rect = layout->window_resize;
        break;
    case UI_CONTROL_MODE:
        if (control_ref.index >= CHRONO_MODE_COUNT)
            return FALSE;
        rect = layout->modes[control_ref.index];
        break;
    case UI_CONTROL_SAVE_RETRY:
        if (session->save_state != SAVE_STATE_FAILED)
            return FALSE;
        rect = layout->save_status;
        danger = TRUE;
        break;
    case UI_CONTROL_FOCUS_PRIMARY:
        if (presentation->active_tab != CHRONO_MODE_FOCUS)
            return FALSE;
        rect = layout->focus_primary;
        break;
    case UI_CONTROL_FOCUS_SECONDARY:
        if (presentation->active_tab != CHRONO_MODE_FOCUS)
            return FALSE;
        rect = layout->focus_secondary;
        break;
    case UI_CONTROL_FOCUS_ADD_5:
        if (presentation->active_tab != CHRONO_MODE_FOCUS)
            return FALSE;
        rect = layout->focus_add_5;
        break;
    case UI_CONTROL_FOCUS_ADD_15:
        if (presentation->active_tab != CHRONO_MODE_FOCUS)
            return FALSE;
        rect = layout->focus_add_15;
        break;
    case UI_CONTROL_QUEUE_ROW:
    case UI_CONTROL_QUEUE_PRIMARY:
    case UI_CONTROL_QUEUE_DONE:
    case UI_CONTROL_QUEUE_DELETE:
        if (presentation->active_tab != CHRONO_MODE_FOCUS ||
            control_ref.index >= layout->visible_queue_rows ||
            control_ref.index >= CHRONO_VISIBLE_QUEUE_ROWS)
            return FALSE;
        if (control_ref.id == UI_CONTROL_QUEUE_ROW)
            rect = layout->queue_rows[control_ref.index];
        else if (control_ref.id == UI_CONTROL_QUEUE_PRIMARY)
            rect = layout->queue_primary[control_ref.index];
        else if (control_ref.id == UI_CONTROL_QUEUE_DONE)
            rect = layout->queue_done[control_ref.index];
        else {
            rect = layout->queue_delete[control_ref.index];
            danger = TRUE;
        }
        break;
    case UI_CONTROL_TITLE_INPUT:
        if (presentation->active_tab != CHRONO_MODE_FOCUS)
            return FALSE;
        rect = layout->title_input;
        break;
    case UI_CONTROL_DURATION_INPUT:
        if (presentation->active_tab != CHRONO_MODE_FOCUS)
            return FALSE;
        rect = layout->duration_input;
        break;
    case UI_CONTROL_ADD_TASK:
        if (presentation->active_tab != CHRONO_MODE_FOCUS)
            return FALSE;
        rect = layout->add_button;
        break;
    case UI_CONTROL_IMPORT:
        if (presentation->active_tab != CHRONO_MODE_LEDGER)
            return FALSE;
        rect = layout->import_button;
        break;
    case UI_CONTROL_EXPORT:
        if (presentation->active_tab != CHRONO_MODE_LEDGER)
            return FALSE;
        rect = layout->export_button;
        break;
    case UI_CONTROL_CLEAR_COMPLETED:
        if (presentation->active_tab != CHRONO_MODE_LEDGER)
            return FALSE;
        rect = layout->clear_button;
        danger = TRUE;
        break;
    case UI_CONTROL_LEDGER_ROW:
    case UI_CONTROL_LEDGER_DELETE:
        if (presentation->active_tab != CHRONO_MODE_LEDGER ||
            control_ref.index >= layout->visible_ledger_rows ||
            control_ref.index >= CHRONO_VISIBLE_LEDGER_ROWS)
            return FALSE;
        if (control_ref.id == UI_CONTROL_LEDGER_ROW)
            rect = layout->ledger_rows[control_ref.index];
        else {
            rect = layout->ledger_delete[control_ref.index];
            danger = TRUE;
        }
        break;
    case UI_CONTROL_PRESENTATION:
        if (presentation->active_tab != CHRONO_MODE_CALIBRATION ||
            control_ref.index >= CHRONO_PRESENTATION_MODE_COUNT)
            return FALSE;
        rect = layout->presentation_button[control_ref.index];
        break;
    case UI_CONTROL_SLIDER:
        if (presentation->active_tab != CHRONO_MODE_CALIBRATION ||
            control_ref.index >= CHRONO_SLIDER_COUNT)
            return FALSE;
        rect = slider_hit_rect(layout, control_ref.index);
        break;
    case UI_CONTROL_MASK:
        if (presentation->active_tab != CHRONO_MODE_CALIBRATION)
            return FALSE;
        rect = layout->mask_checkbox;
        break;
    case UI_CONTROL_CURVATURE:
        if (presentation->active_tab != CHRONO_MODE_CALIBRATION)
            return FALSE;
        rect = layout->curvature_checkbox;
        break;
    case UI_CONTROL_CURSOR_SHADER_EFFECTS:
        if (presentation->active_tab != CHRONO_MODE_CALIBRATION)
            return FALSE;
        rect = layout->cursor_shader_effects_toggle;
        break;
    case UI_CONTROL_NOTIFICATIONS:
        if (presentation->active_tab != CHRONO_MODE_CALIBRATION)
            return FALSE;
        rect = layout->notifications_toggle;
        break;
    case UI_CONTROL_SOUND:
        if (presentation->active_tab != CHRONO_MODE_CALIBRATION)
            return FALSE;
        rect = layout->sound_toggle;
        break;
    case UI_CONTROL_AA:
        if (presentation->active_tab != CHRONO_MODE_CALIBRATION ||
            control_ref.index >= CHRONO_AA_MODE_COUNT)
            return FALSE;
        rect = layout->aa_button[control_ref.index];
        break;
    case UI_CONTROL_PALETTE:
        if (presentation->active_tab != CHRONO_MODE_CALIBRATION ||
            control_ref.index >= CHRONO_PALETTE_COUNT)
            return FALSE;
        rect = layout->palette_button[control_ref.index];
        break;
    case UI_CONTROL_RESET_CALIBRATION:
        if (presentation->active_tab != CHRONO_MODE_CALIBRATION)
            return FALSE;
        rect = layout->reset_button;
        break;
    case UI_CONTROL_NONE:
    default:
        return FALSE;
    }

    if (rect.width <= 0.0 || rect.height <= 0.0)
        return FALSE;
    if (rect_out != NULL)
        *rect_out = rect;
    if (danger_out != NULL)
        *danger_out = danger;
    return TRUE;
}
