#include "app_routines.h"
#include "app.h"
#include "app_modal.h"
#include "ui.h"
#include "ui_painter.h"
#include <string.h>

typedef struct {
    GtkEntry *title;
    GtkSpinButton *hours;
    GtkCheckButton *daily;
    GtkLabel *error;
    guint index;
} RoutineEditor;

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
            guint hours = gtk_check_button_get_active(e->daily) ? 0 :
                (guint)gtk_spin_button_get_value_as_int(e->hours);
            if (fresh || r->hours != hours) {
                r->due = chrono_routine_next(hours, g_get_real_time() / G_USEC_PER_SEC);
            }
            g_strlcpy(r->title, title, sizeof(r->title));
            r->hours = hours;
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

static void daily_changed(GtkCheckButton *daily, gpointer data)
{
    gtk_widget_set_sensitive(GTK_WIDGET(data), !gtk_check_button_get_active(daily));
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
    e->daily = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Daily · next local midnight"));
    gtk_box_append(box, GTK_WIDGET(e->daily));
    gtk_box_append(box, editor_label("Otherwise repeat after this many hours (1–8760)"));
    e->hours = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(1, 8760, 1));
    gtk_box_append(box, GTK_WIDGET(e->hours));
    gtk_spin_button_set_value(e->hours, 1);
    g_signal_connect(e->daily, "toggled", G_CALLBACK(daily_changed), e->hours);
    gtk_box_append(box, editor_label("One reminder at a time. Acknowledge to schedule the next.\nChanging the interval starts a new schedule from now."));
    if (index < p->routine_count) {
        ChronoRoutine *r = &p->routines[index];
        gtk_editable_set_text(GTK_EDITABLE(e->title), r->title);
        gtk_check_button_set_active(e->daily, r->hours == 0);
        gtk_spin_button_set_value(e->hours, MAX(r->hours, 1));
    }
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
            if (!chrono_routine_due(r, g_get_real_time() / G_USEC_PER_SEC)) return;
            r->due = chrono_routine_next(r->hours, g_get_real_time() / G_USEC_PER_SEC);
        } else if (c == 4) { edit_routine(s, *i); return; }
        else if (c == 5) r->enabled = !r->enabled;
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
        app_save_state(s);
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
    const char *labels[] = {"Previous", "Next", "+ New", "Acknowledge", "Edit", r != NULL && r->enabled ? "Disable" : "Enable", "Remove"};
    for (guint c = 0; c < 7; c++) {
        gboolean enabled = c == 0 ? i > 0 : c == 1 ? i + 1 < p->routine_count :
            !s->session.data_load_failed && (c == 2 || (r != NULL && (c != 3 || due)));
        draw_button_sized(s, cr, layout, app_routine_control(&s->presentation.layout, c),
            labels[c], FALSE, c == 3 && due, c == 6, enabled, 11);
    }
    Rect top = app_routine_control(&s->presentation.layout, 0);
    double width = s->presentation.layout.status_rail.width;
    char *heading = g_strdup_printf("ROUTINES  %u / %u  ·  %s", r ? i + 1 : 0,
        p->routine_count, r == NULL ? "CREATE A SCHEDULE" : !r->enabled ? "DISABLED" : due ? "DUE" : "WAITING");
    draw_text(s, cr, layout, FONT_MONO, heading, 11, PANGO_WEIGHT_BOLD,
        top.x, top.y + 49, width, TEXT_LEFT, .5, 1, .65, 1);
    g_free(heading);
    draw_text(s, cr, layout, FONT_SANS, r ? r->title : "Stretch · Drink water · Take a break", 18,
        PANGO_WEIGHT_BOLD, top.x, top.y + 77, width, TEXT_LEFT, .5, 1, .65, 1);
    char *detail = NULL;
    if (r != NULL) {
        GDateTime *date = g_date_time_new_from_unix_local(r->due);
        char *when = date ? g_date_time_format(date, "%d %b %Y %H:%M") : g_strdup("unknown");
        detail = r->hours ? g_strdup_printf("Every %u h · Next: %s", r->hours, when) :
            g_strdup_printf("Daily · Next: %s", when);
        g_free(when);
        if (date) g_date_time_unref(date);
    }
    draw_text(s, cr, layout, FONT_MONO, detail ? detail : "Missed repeats collapse into one reminder.", 11,
        PANGO_WEIGHT_NORMAL, top.x, top.y + 103, width, TEXT_LEFT, .4, .8, .55, 1);
    g_free(detail);
}
