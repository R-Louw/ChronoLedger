#include "chrono_preferences.h"

void
chrono_preferences_init_defaults(ChronoPreferences *preferences)
{
    g_return_if_fail(preferences != NULL);

    *preferences = (ChronoPreferences){
        .settings = { .glow = .70f, .persistence = .24f,
            .cursor_persistence = .27f, .vignette = .44f,
            .mask_strength = .36f, .curvature_strength = CHRONO_CURVATURE_DEFAULT, .aa_mode = AA_FXAA_HIGH },
        .notifications_enabled = TRUE,
        .sound_enabled = FALSE,
        .phosphor_mask_enabled = TRUE,
        .curvature_enabled = FALSE,
        .cursor_shader_effects_enabled = TRUE,
    };
}

void
chrono_preferences_copy(ChronoPreferences *destination,
                         const ChronoPreferences *source)
{
    g_return_if_fail(destination != NULL);
    g_return_if_fail(source != NULL);

    *destination = *source;
}

gint64 chrono_routine_next(guint hours, gint64 now)
{
    if (hours > 0)
        return now + (gint64)hours * 3600;
    GDateTime *date = g_date_time_new_from_unix_local(now);
    if (date == NULL) return now + 86400;
    GDateTime *tomorrow = g_date_time_add_days(date, 1);
    GDateTime *midnight = g_date_time_new_local(g_date_time_get_year(tomorrow),
        g_date_time_get_month(tomorrow), g_date_time_get_day_of_month(tomorrow), 0, 0, 0);
    gint64 result = midnight != NULL ? g_date_time_to_unix(midnight) : now + 86400;
    if (midnight != NULL) g_date_time_unref(midnight);
    g_date_time_unref(tomorrow);
    g_date_time_unref(date);
    return result;
}

gboolean chrono_routine_due(const ChronoRoutine *r, gint64 now)
{
    return r->enabled && now >= r->due;
}

static void integer(JsonBuilder *b, const char *key, gint64 value)
{
    json_builder_set_member_name(b, key);
    json_builder_add_int_value(b, value);
}

void chrono_preferences_write_local(JsonBuilder *b, const ChronoPreferences *p)
{
    json_builder_set_member_name(b, "local_state");
    json_builder_begin_object(b);
    integer(b, "tab", p->selected_tab);
    integer(b, "profile", p->presentation_mode);
    integer(b, "aa", p->settings.aa_mode);
    integer(b, "palette", p->settings.palette);
    const float values[] = {p->settings.glow, p->settings.persistence,
        p->settings.cursor_persistence, p->settings.vignette, p->settings.mask_strength, p->settings.curvature_strength};
    json_builder_set_member_name(b, "sliders");
    json_builder_begin_array(b);
    for (guint i = 0; i < G_N_ELEMENTS(values); i++) json_builder_add_double_value(b, values[i]);
    json_builder_end_array(b);
    json_builder_set_member_name(b, "routines");
    json_builder_begin_array(b);
    for (guint i = 0; i < p->routine_count; i++) {
        const ChronoRoutine *r = &p->routines[i];
        json_builder_begin_object(b);
        json_builder_set_member_name(b, "title"); json_builder_add_string_value(b, r->title);
        integer(b, "hours", r->hours); integer(b, "due", r->due);
        json_builder_set_member_name(b, "enabled"); json_builder_add_boolean_value(b, r->enabled);
        json_builder_end_object(b);
    }
    json_builder_end_array(b);
    json_builder_end_object(b);
}

static gboolean read_int(JsonObject *o, const char *key, gint64 max, gint64 *out)
{
    JsonNode *n = json_object_get_member(o, key);
    if (n == NULL || json_node_get_value_type(n) != G_TYPE_INT64) return FALSE;
    *out = json_node_get_int(n);
    return *out >= 0 && *out <= max;
}

gboolean chrono_preferences_read_local(JsonObject *root, ChronoPreferences *p)
{
    if (!json_object_has_member(root, "local_state")) return TRUE;
    JsonNode *n = json_object_get_member(root, "local_state");
    if (!JSON_NODE_HOLDS_OBJECT(n)) return FALSE;
    JsonObject *o = json_node_get_object(n);
    gint64 v;
    if (!read_int(o, "tab", CHRONO_MODE_COUNT - 1, &v)) return FALSE;
    p->selected_tab = (int)v;
    if (!read_int(o, "profile", CHRONO_PRESENTATION_MODE_COUNT - 1, &v)) return FALSE;
    p->presentation_mode = (int)v;
    if (!read_int(o, "aa", CHRONO_AA_MODE_COUNT - 1, &v)) return FALSE;
    p->settings.aa_mode = (AntiAliasingMode)v;
    if (!read_int(o, "palette", CHRONO_PALETTE_COUNT - 1, &v)) return FALSE;
    p->settings.palette = (Palette)v;
    n = json_object_get_member(o, "sliders");
    if (n == NULL || !JSON_NODE_HOLDS_ARRAY(n)) return FALSE;
    JsonArray *a = json_node_get_array(n);
    guint length = json_array_get_length(a);
    if (length != 5 && length != CHRONO_SLIDER_COUNT) return FALSE;
    float *values[] = {&p->settings.glow, &p->settings.persistence,
        &p->settings.cursor_persistence, &p->settings.vignette, &p->settings.mask_strength, &p->settings.curvature_strength};
    for (guint i = 0; i < length; i++) {
        n = json_array_get_element(a, i);
        GType t = json_node_get_value_type(n);
        if (t != G_TYPE_DOUBLE && t != G_TYPE_INT64) return FALSE;
        double d = json_node_get_double(n);
        if (!(d >= 0 && d <= 1)) return FALSE;
        *values[i] = (float)d;
    }
    n = json_object_get_member(o, "routines");
    if (n == NULL || !JSON_NODE_HOLDS_ARRAY(n)) return FALSE;
    a = json_node_get_array(n);
    p->routine_count = json_array_get_length(a);
    if (p->routine_count > CHRONO_MAX_ROUTINES) return FALSE;
    for (guint i = 0; i < p->routine_count; i++) {
        n = json_array_get_element(a, i);
        if (!JSON_NODE_HOLDS_OBJECT(n)) return FALSE;
        JsonObject *r = json_node_get_object(n);
        n = json_object_get_member(r, "title");
        if (n == NULL || json_node_get_value_type(n) != G_TYPE_STRING) return FALSE;
        const char *title = json_node_get_string(n);
        if (*title == 0 || strlen(title) > 256) return FALSE;
        g_strlcpy(p->routines[i].title, title, sizeof(p->routines[i].title));
        if (!read_int(r, "hours", 8760, &v)) return FALSE;
        p->routines[i].hours = (guint)v;
        if (!read_int(r, "due", 253402214400LL, &v) || v == 0) return FALSE;
        p->routines[i].due = v;
        n = json_object_get_member(r, "enabled");
        if (n == NULL || json_node_get_value_type(n) != G_TYPE_BOOLEAN) return FALSE;
        p->routines[i].enabled = json_node_get_boolean(n);
    }
    return TRUE;
}
