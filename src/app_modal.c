#include "app_input.h"
#include "app_modal.h"
#include "ui_palette.h"
#include "renderer.h"
#include "cursor_state.h"
#include "ui.h"
#include <math.h>

typedef struct {
    GtkWindow *owner; /* bounded by the owner's widget hierarchy */
    GtkWidget *shield;
    GtkWidget *panel;
    GtkWidget *header;
    GtkWidget *scroll;
    GtkWidget *actions;
    GtkWidget *focus;
    GtkCssProvider *provider;
    GdkDisplay *display;
    AppModalResponse response;
    gboolean responded;
    gpointer data;
    GDestroyNotify destroy;
} AppModal;

static void modal_free(gpointer data)
{
    AppModal *m = data;
    gtk_style_context_remove_provider_for_display(m->display, GTK_STYLE_PROVIDER(m->provider));
    g_object_unref(m->provider);
    g_object_unref(m->display);
    if (!m->responded && m->response != NULL) m->response(NULL, FALSE, m->data);
    if (m->destroy != NULL) m->destroy(m->data);
    g_clear_object(&m->focus);
    g_free(m);
}

static void modal_respond(AppModal *m, gboolean accepted)
{
    AppState *s = g_object_get_data(G_OBJECT(m->owner), "chronoledger-state");
    if (s == NULL) return;
    if (m->response != NULL && !m->response(s, accepted, m->data)) return;
    m->responded = TRUE;
    GtkWidget *focus = m->focus != NULL ? g_object_ref(m->focus) : NULL;
    s->shell.modal_overlay = NULL;
    gtk_widget_set_sensitive(GTK_WIDGET(s->shell.gl_area), TRUE);
    gtk_widget_set_can_focus(GTK_WIDGET(s->shell.input_proxy), TRUE);
    gtk_overlay_remove_overlay(GTK_OVERLAY(s->shell.root_overlay), m->shield);
    if (!s->shell.closing) {
        if (focus == NULL || !gtk_widget_grab_focus(focus))
            gtk_widget_grab_focus(GTK_WIDGET(s->shell.gl_area));
        ui_invalidate_dynamic(s);
    }
    g_clear_object(&focus);
}

void app_modal_cancel(AppState *s)
{
    if (s != NULL && s->shell.modal_overlay != NULL)
        modal_respond(g_object_get_data(G_OBJECT(s->shell.modal_overlay), "app-modal"), FALSE);
}

static void clicked(GtkButton *button, gpointer data)
{
    modal_respond(data, GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "accept")));
}

static gboolean modal_key(GtkEventControllerKey *controller, guint key, guint code,
                           GdkModifierType modifiers, gpointer data)
{
    (void)controller; (void)code;
    AppModal *m = data;
    if (key == GDK_KEY_Escape) { modal_respond(m, FALSE); return TRUE; }
    if (key == GDK_KEY_Tab || key == GDK_KEY_ISO_Left_Tab) {
        GtkDirectionType direction = (modifiers & GDK_SHIFT_MASK) || key == GDK_KEY_ISO_Left_Tab
            ? GTK_DIR_TAB_BACKWARD : GTK_DIR_TAB_FORWARD;
        if (!gtk_widget_child_focus(m->panel, direction)) {
            gtk_window_set_focus(m->owner, NULL);
            gtk_widget_child_focus(m->panel, direction);
        }
        return TRUE;
    }
    return FALSE;
}

static GtkWidget *label_new(const char *text, const char *style)
{
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 56);
    if (style != NULL) gtk_widget_add_css_class(label, style);
    return label;
}

static char *rgb(const float *v, double brightness)
{
    return g_strdup_printf("rgb(%d,%d,%d)",
        (int)(255 * CLAMP(pow(v[0], 1.0/2.2) * brightness, 0, 1)),
        (int)(255 * CLAMP(pow(v[1], 1.0/2.2) * brightness, 0, 1)),
        (int)(255 * CLAMP(pow(v[2], 1.0/2.2) * brightness, 0, 1)));
}

static gboolean fit_panel(GtkWidget *widget, GdkFrameClock *clock, gpointer data)
{
    (void)clock; (void)widget;
    AppModal *m = data;
    int width = MAX(240, MIN(460, gtk_widget_get_width(GTK_WIDGET(m->owner)) - 66));
    int requested;
    gtk_widget_get_size_request(m->scroll, &requested, NULL);
    if (requested != width) gtk_widget_set_size_request(m->scroll, width, -1);
    int header, actions;
    gtk_widget_measure(m->header, GTK_ORIENTATION_VERTICAL, width, NULL, &header, NULL, NULL);
    gtk_widget_measure(m->actions, GTK_ORIENTATION_VERTICAL, width, NULL, &actions, NULL, NULL);
    int available = MAX(40, gtk_widget_get_height(GTK_WIDGET(m->owner)) - 66 - 28 - header - actions);
    if (gtk_scrolled_window_get_max_content_height(GTK_SCROLLED_WINDOW(m->scroll)) != available)
        gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(m->scroll), available);
    return G_SOURCE_CONTINUE;
}

void app_modal_present(AppState *s, const char *title, const char *detail,
                       GtkWidget *content, const char *accept_label,
                       gboolean destructive, AppModalResponse response,
                       gpointer data, GDestroyNotify destroy)
{
    if (s->shell.closing) { if (destroy != NULL) destroy(data); return; }
    app_modal_cancel(s);
    AppModal *m = g_new0(AppModal, 1);
    m->owner = s->shell.window;
    GtkWidget *focus = gtk_window_get_focus(m->owner);
    if (focus != NULL) m->focus = g_object_ref(focus);
    m->response = response; m->data = data; m->destroy = destroy;
    m->shield = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(m->shield, TRUE);
    gtk_widget_set_vexpand(m->shield, TRUE);
    gtk_widget_add_css_class(m->shield, "chrono-modal-shield");
    gtk_widget_set_cursor_from_name(m->shield, "default");
    g_object_set_data_full(G_OBJECT(m->shield), "app-modal", m, modal_free);
    m->panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_halign(m->panel, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(m->panel, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(m->panel, TRUE);
    gtk_widget_set_margin_top(m->panel, 12);
    gtk_widget_set_margin_bottom(m->panel, 12);
    gtk_widget_set_margin_start(m->panel, 12);
    gtk_widget_set_margin_end(m->panel, 12);
    gtk_widget_add_css_class(m->panel, "chrono-modal-panel");
    gtk_box_append(GTK_BOX(m->shield), m->panel);
    m->header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_box_append(GTK_BOX(m->header), label_new("CHRONOLEDGER / OPERATOR PANEL", "modal-kicker"));
    gtk_box_append(GTK_BOX(m->header), label_new(title, "modal-title"));
    gtk_box_append(GTK_BOX(m->panel), m->header);
    // Only the body scrolls; Cancel and Save remain visible in short tiles.
    m->scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(m->scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(m->scroll), TRUE);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(m->scroll),
        MAX(40, gtk_widget_get_height(s->shell.root_overlay) - 210));
    gtk_widget_set_size_request(m->scroll,
        MAX(240, MIN(460, gtk_widget_get_width(GTK_WIDGET(m->owner)) - 66)), -1);
    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    if (detail != NULL) gtk_box_append(GTK_BOX(body), label_new(detail, NULL));
    if (content != NULL) gtk_box_append(GTK_BOX(body), content);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(m->scroll), body);
    gtk_box_append(GTK_BOX(m->panel), m->scroll);
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    m->actions = actions;
    gtk_widget_set_halign(actions, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(m->panel), actions);
    gtk_widget_add_tick_callback(m->shield, fit_panel, m, NULL);
    GtkWidget *cancel = gtk_button_new_with_label(accept_label != NULL ? "Cancel" : "Close");
    g_signal_connect(cancel, "clicked", G_CALLBACK(clicked), m);
    gtk_box_append(GTK_BOX(actions), cancel);
    if (accept_label != NULL) {
        GtkWidget *accept = gtk_button_new_with_label(accept_label);
        gtk_widget_add_css_class(accept, destructive ? "modal-danger" : "modal-accept");
        g_object_set_data(G_OBJECT(accept), "accept", GINT_TO_POINTER(TRUE));
        g_signal_connect(accept, "clicked", G_CALLBACK(clicked), m);
        gtk_box_append(GTK_BOX(actions), accept);
    }
    const PaletteParameters *p = palette_parameters(s->presentation.settings.palette);
    char *accent = rgb(p->mid, 1), *text = rgb(p->high, 1);
    char *base = rgb(p->low, .065), *border = rgb(p->mid, .5);
    double glow = s->presentation.settings.glow;
    char *css = g_strdup_printf(
        ".chrono-modal-shield { background: rgba(0,0,0,0.68); }"
        ".chrono-modal-panel { background: %s; color: %s; border: 1px solid %s;"
        " border-radius: 6px; padding: 20px; font-family: 'IBM Plex Sans', sans-serif; font-size: 13px;"
        " box-shadow: 0 0 %.0fpx alpha(%s,0.22); }"
        ".chrono-modal-panel label { color: %s; }"
        ".chrono-modal-panel .modal-kicker { font-family: 'JetBrains Mono', monospace; font-size: 10px; color: %s; }"
        ".chrono-modal-panel .modal-title { font-family: 'JetBrains Mono', monospace; font-size: 18px; font-weight: bold; }"
        ".chrono-modal-panel button, .chrono-modal-panel entry, .chrono-modal-panel spinbutton {"
        " background: %s; color: %s; border: 1px solid %s; border-radius: 3px; min-height: 28px; box-shadow: none; }"
        ".chrono-modal-panel button:hover { background: alpha(%s,0.20); }"
        ".chrono-modal-panel :focus { outline: 2px solid %s; outline-offset: 2px; }"
        ".chrono-modal-panel .modal-danger { color: #ffb5a8; border-color: #c86d60; }"
        ".chrono-modal-panel .modal-accept { border-color: %s; }"
        ".chrono-modal-panel checkbutton check { background: %s; color: %s; border: 1px solid %s; border-radius: 2px; }"
        ".chrono-modal-panel checkbutton check:checked { background: %s; color: %s; border-color: %s; }"
        ".chrono-modal-panel .error { color: #ffb5a8; }",
        base,text,border,glow*20,accent,text,accent,base,text,border,accent,accent,accent,
        base,text,border,accent,base,accent);
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, css);
    m->provider = provider;
    m->display = g_object_ref(gtk_widget_get_display(m->shield));
    gtk_style_context_add_provider_for_display(m->display, GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    g_free(css); g_free(accent); g_free(text); g_free(base); g_free(border);
    GtkEventController *keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(modal_key), m);
    gtk_widget_add_controller(m->shield, keys);
    s->shell.modal_overlay = m->shield;
    s->input.pointer_down = FALSE; s->input.active_slider = SLIDER_NONE;
    s->input.pointer_motion_pending = FALSE; s->input.cursor_motion_count = 0;
    app_input_update_cursor(s, FALSE);
    s->input.pointer_inside = FALSE; s->input.hover_control = ui_control_none();
    cursor_state_clear(&s->input, s->renderer);
    renderer_invalidate_filtered(s->renderer);
    gtk_widget_set_sensitive(GTK_WIDGET(s->shell.gl_area), FALSE);
    gtk_widget_set_can_focus(GTK_WIDGET(s->shell.input_proxy), FALSE);
    gtk_overlay_add_overlay(GTK_OVERLAY(s->shell.root_overlay), m->shield);
    gtk_widget_grab_focus(cancel); // Return initially cancels destructive operations.
    ui_invalidate_dynamic(s);
}

static gboolean choose_response(AppState *s, gboolean accepted, gpointer data)
{
    (void)s;
    g_task_return_int(G_TASK(data), accepted ? 1 : 0);
    return TRUE;
}

void app_modal_choose(AppState *s, GtkAlertDialog *dialog,
                      GAsyncReadyCallback callback, gpointer data)
{
    if (s->shell.root_overlay == NULL) {
        gtk_alert_dialog_choose(dialog, s->shell.window, NULL, callback, data);
        return;
    }
    GTask *task = g_task_new(dialog, NULL, callback, data);
    g_task_set_source_tag(task, G_GNUC_EXTENSION (gpointer)app_modal_choose);
    const char * const *buttons = gtk_alert_dialog_get_buttons(dialog);
    app_modal_present(s, gtk_alert_dialog_get_message(dialog), gtk_alert_dialog_get_detail(dialog),
        NULL, buttons != NULL ? buttons[1] : NULL, buttons != NULL,
        choose_response, task, g_object_unref);
}

int app_modal_choose_finish(GtkAlertDialog *dialog, GAsyncResult *result, GError **error)
{
    if (g_async_result_is_tagged(result, G_GNUC_EXTENSION (gpointer)app_modal_choose)) {
        g_return_val_if_fail(g_task_is_valid(result, dialog), -1);
        return (int)g_task_propagate_int(G_TASK(result), error);
    }
    return gtk_alert_dialog_choose_finish(dialog, result, error);
}
