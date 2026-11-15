#pragma once

#include <glib.h>
#include <json-glib/json-glib.h>
#include "app_types.h"

#define CHRONO_MAX_ROUTINES 32
typedef struct {
    char title[257];
    gboolean enabled;
    guint hours; /* zero: next local calendar day; otherwise interval */
    gint64 due; /* Unix seconds, persistent across sleep/restart */
} ChronoRoutine;

gint64 chrono_routine_next(guint hours, gint64 now);
gboolean chrono_routine_due(const ChronoRoutine *routine, gint64 now);

G_BEGIN_DECLS

typedef struct {
    int selected_tab;
    int presentation_mode;
    ShaderSettings settings;
    guint routine_count;
    ChronoRoutine routines[CHRONO_MAX_ROUTINES];
    gboolean notifications_enabled;
    gboolean sound_enabled;
    gboolean phosphor_mask_enabled;
    gboolean curvature_enabled;
    gboolean cursor_shader_effects_enabled;
} ChronoPreferences;

void chrono_preferences_init_defaults(ChronoPreferences *preferences);
void chrono_preferences_copy(ChronoPreferences *destination,
                             const ChronoPreferences *source);

G_END_DECLS

void chrono_preferences_write_local(JsonBuilder *builder, const ChronoPreferences *p);
gboolean chrono_preferences_read_local(JsonObject *root, ChronoPreferences *p);
