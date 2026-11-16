#include "app_routines.h"
#include "app.h"
#include "app_modal.h"
#include "ui.h"
#include "ui_painter.h"
#include <string.h>

typedef struct {
    GtkEntry *title;
    GtkSpinButton *hours;
    GtkDropDown *kind;
    GtkSpinButton *clock_hour;
    GtkSpinButton *clock_minute;
    GtkSpinButton *window;
    GtkWidget *interval_fields;
    GtkWidget *clock_fields;
    gboolean locked;
    GtkLabel *error;
    guint index;
} RoutineEditor;

static void kind_changed(GObject *object, GParamSpec *pspec, gpointer data);

static gboolean editor_response(AppState *s, gboolean accepted, gpointer data)
{
    RoutineEditor *e = data;
    if (accepted && s != NULL && !s->shell.closing &&
        !s->session.data_load_failed) {
        const char *title = gtk_editable_get_text(GTK_EDITABLE(e->title));
        if (*title == '\0' || strlen(title) > 256) {
            gtk_label_set_text(e->error, "Enter a title of 1–256 bytes.");
            gtk_widget_add_css_class(GTK_WIDGET(e->title), "error");
            gtk_widget_grab_focus(GTK_WIDGET(e->title));
            return FALSE;
        }
        ChronoPreferences *p = app_preferences(s);
        if (e->index <= p->routine_count && e->index < CHRONO_MAX_ROUTINES) {
            ChronoRoutine *r = &p->routines[e->index];
            gboolean fresh = e->index == p->routine_count;
            if (fresh) *r = (ChronoRoutine){0};
            gint64 now = g_get_real_time() / G_USEC_PER_SEC;
            gboolean changed = chrono_routine_maintain(r, now);
            if (!e->locked && r->deadline != 0) {
                e->locked = TRUE;
                gtk_widget_set_sensitive(GTK_WIDGET(e->kind), FALSE);
                kind_changed(NULL, NULL, e);
                gtk_label_set_text(e->error,
                    "This item became due while editing. Schedule retained; confirm it first. Save again to keep title edits.");
                if (changed) app_save_state(s);
                ui_invalidate_dynamic(s);
                return FALSE;
            }
            if (!e->locked && r->deadline == 0) {
                guint kind = gtk_drop_down_get_selected(e->kind);
                guint hours = kind == 0 ? (guint)gtk_spin_button_get_value_as_int(e->hours) : 0;
                guint minute = (guint)gtk_spin_button_get_value_as_int(e->clock_hour) * 60 +
                    (guint)gtk_spin_button_get_value_as_int(e->clock_minute);
                gboolean scheduled = kind == 2;
                if (fresh || r->hours != hours || r->scheduled != scheduled ||
                    (scheduled && r->clock_minute != minute)) {
                    r->due = scheduled ? chrono_routine_next_clock(minute, now) :
                        chrono_routine_next(hours, now);
                }
                r->hours = hours;
                r->scheduled = scheduled;
                r->clock_minute = minute;
                r->window_minutes = (guint)gtk_spin_button_get_value_as_int(e->window);
            }
            g_strlcpy(r->title, title, sizeof(r->title));
            if (fresh) {
                r->enabled = TRUE;
                s->session.routine_notified_due[e->index] = 0;
                p->routine_count++;
            }
            s->session.routine_index = e->index;
            app_save_state(s);
            app_refresh_power_policy(s);
            ui_invalidate_dynamic(s);
        }
    }
    return TRUE;
}

static GtkWidget *editor_label(const char *text)
{
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 48);
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    return label;
}

static void kind_changed(GObject *object, GParamSpec *pspec, gpointer data)
{
    (void)object;
    (void)pspec;
    RoutineEditor *e = data;
    guint kind = gtk_drop_down_get_selected(e->kind);
    gtk_widget_set_visible(e->interval_fields, kind == 0);
    gtk_widget_set_visible(e->clock_fields, kind == 2);
    gtk_widget_set_sensitive(GTK_WIDGET(e->hours), !e->locked && kind == 0);
    gtk_widget_set_sensitive(GTK_WIDGET(e->clock_hour), !e->locked && kind == 2);
    gtk_widget_set_sensitive(GTK_WIDGET(e->clock_minute), !e->locked && kind == 2);
    gtk_widget_set_sensitive(GTK_WIDGET(e->window), !e->locked && kind == 2);
}

static void edit_routine(AppState *s, guint index)
{
    ChronoPreferences *p = app_preferences(s);
    if (index >= CHRONO_MAX_ROUTINES) {
        app_set_annunciator(s, ANNUNCIATOR_CAUTION, "Routine limit: 32");
        return;
    }
    RoutineEditor *e = g_new0(RoutineEditor, 1);
    e->index = index;
    GtkBox *box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 10));
    gtk_box_append(box, editor_label("Title (1–256 bytes)"));
    e->title = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_max_length(e->title, 256);
    gtk_box_append(box, GTK_WIDGET(e->title));
    const char *kinds[] = {"Interval · hours after acknowledgment",
        "Daily · next local midnight", "Scheduled confirmation · daily clock", NULL};
    e->kind = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(kinds));
    gtk_box_append(box, GTK_WIDGET(e->kind));
    GtkBox *interval = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 6));
    e->interval_fields = GTK_WIDGET(interval);
    gtk_box_append(box, e->interval_fields);
    gtk_box_append(interval, editor_label("Interval hours (1–8760)"));
    e->hours = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(1, 8760, 1));
    gtk_box_append(interval, GTK_WIDGET(e->hours));
    GtkBox *scheduled = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 6));
    e->clock_fields = GTK_WIDGET(scheduled);
    gtk_box_append(box, e->clock_fields);
    gtk_box_append(scheduled, editor_label("Local time · hour (0–23) / minute (0–59)"));
    GtkBox *clock = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8));
    e->clock_hour = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(0, 23, 1));
    e->clock_minute = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(0, 59, 1));
    gtk_box_append(clock, GTK_WIDGET(e->clock_hour));
    gtk_box_append(clock, GTK_WIDGET(e->clock_minute));
    gtk_box_append(scheduled, GTK_WIDGET(clock));
    gtk_spin_button_set_value(e->clock_hour, 9);
    gtk_box_append(scheduled, editor_label("Confirmation window · minutes (1–120)"));
    e->window = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(1, 120, 1));
    gtk_spin_button_set_value(e->window, 10);
    gtk_box_append(scheduled, GTK_WIDGET(e->window));
    if (index < p->routine_count) {
        ChronoRoutine *r = &p->routines[index];
        if (chrono_routine_maintain(r, g_get_real_time() / G_USEC_PER_SEC)) app_save_state(s);
        e->locked = r->scheduled && r->deadline != 0;
        gtk_editable_set_text(GTK_EDITABLE(e->title), r->title);
        gtk_drop_down_set_selected(e->kind, r->scheduled ? 2 : r->hours == 0 ? 1 : 0);
        gtk_spin_button_set_value(e->hours, MAX(r->hours, 1));
        if (r->scheduled) {
            gtk_spin_button_set_value(e->clock_hour, r->clock_minute / 60);
            gtk_spin_button_set_value(e->clock_minute, r->clock_minute % 60);
            gtk_spin_button_set_value(e->window, r->window_minutes);
        }
    }
    gtk_widget_set_sensitive(GTK_WIDGET(e->kind), !e->locked);
    kind_changed(NULL, NULL, e);
    g_signal_connect(e->kind, "notify::selected", G_CALLBACK(kind_changed), e);
    gtk_box_append(box, editor_label(e->locked ?
        "Confirm the outstanding item before changing its schedule. Title remains editable." :
        "Confirmations follow the daily clock; expired windows stay overdue until confirmed. "
        "Other types repeat after acknowledgment. Changing a waiting schedule restarts it."));
    e->error = GTK_LABEL(editor_label(""));
    gtk_widget_add_css_class(GTK_WIDGET(e->error), "error");
    gtk_box_append(box, GTK_WIDGET(e->error));
    app_modal_present(s, "Routine schedule", NULL, GTK_WIDGET(box), "Save", FALSE,
                      editor_response, e, g_free);
    gtk_widget_grab_focus(GTK_WIDGET(e->title));
}

typedef struct {
    guint index;
    char title[257];
} RoutineRemoval;

static gboolean remove_response(AppState *s, gboolean accepted, gpointer data)
{
    RoutineRemoval *removal = data;
    if (!accepted || s == NULL || s->shell.closing || s->session.data_load_failed) return TRUE;
    ChronoPreferences *p = app_preferences(s);
    guint index = removal->index;
    if (index >= p->routine_count || strcmp(p->routines[index].title, removal->title) != 0)
        return TRUE;
    memmove(&p->routines[index], &p->routines[index + 1],
        (p->routine_count - index - 1) * sizeof(ChronoRoutine));
    memmove(&s->session.routine_notified_due[index], &s->session.routine_notified_due[index + 1],
        (p->routine_count - index - 1) * sizeof(gint64));
    p->routine_count--;
    if (s->session.routine_index > 0 && s->session.routine_index >= p->routine_count)
        --s->session.routine_index;
    app_save_state(s);
    app_refresh_power_policy(s);
    ui_invalidate_dynamic(s);
    return TRUE;
}

void app_routine_command(AppState *s, guint c)
{
    ChronoPreferences *p = app_preferences(s);
    guint *i = &s->session.routine_index;
    if (c == 0) { if (*i > 0) --*i; }
    else if (c == 1) { if (*i + 1 < p->routine_count) ++*i; }
    else if (!s->session.data_load_failed) {
        if (c == 2) { edit_routine(s, p->routine_count); return; }
        if (*i >= p->routine_count) return;
        ChronoRoutine *r = &p->routines[*i];
        if (c == 3) {
            if (!chrono_routine_confirm(r, g_get_real_time() / G_USEC_PER_SEC)) return;
        } else if (c == 4) { edit_routine(s, *i); return; }
        else if (c == 5) {
            chrono_routine_maintain(r, g_get_real_time() / G_USEC_PER_SEC);
            r->enabled = !r->enabled;
        }
        else if (c == 6) {
            RoutineRemoval *removal = g_new0(RoutineRemoval, 1);
            removal->index = *i;
            g_strlcpy(removal->title, r->title, sizeof(removal->title));
            char *detail = g_strdup_printf("Remove “%s” and its reminder schedule?", r->title);
            app_modal_present(s, "Remove routine?", detail, NULL, "Remove", TRUE,
                remove_response, removal, g_free);
            g_free(detail);
            return;
        }
        gboolean saved = app_save_state(s);
        if (c == 3 && saved)
            app_set_annunciator(s, ANNUNCIATOR_SUCCESS, "Confirmed · waiting for next occurrence");
    }
    app_refresh_power_policy(s);
    ui_invalidate_dynamic(s);
}

void app_routines_draw(AppState *s, const AppViewSnapshot *v, cairo_t *cr, PangoLayout *layout)
{
    const ChronoPreferences *p = &v->preferences;
    guint i = s->session.routine_index;
    const ChronoRoutine *r = i < p->routine_count ? &p->routines[i] : NULL;
    gboolean due = r != NULL && chrono_routine_due(r, g_get_real_time() / G_USEC_PER_SEC);
    const char *labels[] = {"Previous", "Next", "+ New", r != NULL && r->scheduled ? "Confirm" : "Acknowledge", "Edit", r != NULL && r->enabled ? "Disable" : "Enable", "Remove"};
    for (guint c = 0; c < 7; c++) {
        gboolean enabled = c == 0 ? i > 0 : c == 1 ? i + 1 < p->routine_count :
            !s->session.data_load_failed && (c == 2 || (r != NULL && (c != 3 || due)));
        draw_button_sized(s, cr, layout, app_routine_control(&s->presentation.layout, c),
            labels[c], FALSE, c == 3 && due, c == 6, enabled, 11);
    }
    Rect top = app_routine_control(&s->presentation.layout, 0);
    double width = s->presentation.layout.status_rail.width;
    char *heading = g_strdup_printf("ROUTINES  %u / %u  ·  %s", r ? i + 1 : 0,
        p->routine_count, r == NULL ? "CREATE A SCHEDULE" : r->overdue ? "OVERDUE · CONFIRM" : due ? "DUE" : !r->enabled ? "DISABLED" : "WAITING");
    draw_text(s, cr, layout, FONT_MONO, heading, 11, PANGO_WEIGHT_BOLD,
        top.x, top.y + 49, width, TEXT_LEFT, .5, 1, .65, 1);
    g_free(heading);
    draw_text(s, cr, layout, FONT_SANS, r ? r->title : "Stretch · Drink water · Take a break", 18,
        PANGO_WEIGHT_BOLD, top.x, top.y + 77, width, TEXT_LEFT, .5, 1, .65, 1);
    char *detail = NULL;
    if (r != NULL) {
        GDateTime *date = g_date_time_new_from_unix_local(
            r->scheduled && r->deadline != 0 ? r->deadline : r->due);
        char *when = date ? g_date_time_format(date, "%d %b %Y %H:%M") : g_strdup("unknown");
        if (r->scheduled)
            detail = g_strdup_printf("%s: %s",
                r->deadline != 0 ? "Deadline" : "Next", when);
        else
            detail = r->hours ? g_strdup_printf("Every %u h · Next: %s", r->hours, when) :
                g_strdup_printf("Daily · Next: %s", when);
        g_free(when);
        if (date) g_date_time_unref(date);
    }
    draw_text(s, cr, layout, FONT_MONO, detail ? detail : "Missed repeats collapse into one reminder.", 11,
        PANGO_WEIGHT_NORMAL, top.x, top.y + 103, width, TEXT_LEFT, .4, .8, .55, 1);
    g_free(detail);
    if (r != NULL && r->scheduled) {
        char *schedule = g_strdup_printf("Daily %02u:%02u local · %u min window",
            r->clock_minute / 60, r->clock_minute % 60, r->window_minutes);
        draw_text(s, cr, layout, FONT_MONO, schedule, 11, PANGO_WEIGHT_NORMAL,
            top.x, top.y + 125, width, TEXT_LEFT, .4, .8, .55, 1);
        g_free(schedule);
    }
}
