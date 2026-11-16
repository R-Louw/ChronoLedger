#pragma once

#include <glib.h>
#include <json-glib/json-glib.h>
#include "app_types.h"

#define CHRONO_MAX_ROUTINES 32
typedef struct {
    char title[257];
    gboolean enabled;
    guint hours; /* zero: next local calendar day; otherwise interval */
    gint64 due; /* Original trigger in Unix seconds, persistent across sleep/restart */
    gboolean scheduled; /* fixed daily local clock confirmation; legacy types stay unchanged */
    guint clock_minute; /* 0..1439 */
    guint window_minutes; /* 1..120 */
    gint64 deadline; /* zero while waiting; original due + window while outstanding */
    gboolean overdue; /* latched until confirmation, including after clock rollback */
} ChronoRoutine;

gint64 chrono_routine_next(guint hours, gint64 now);
gboolean chrono_routine_due(const ChronoRoutine *routine, gint64 now);
gint64 chrono_routine_next_clock(guint minute, gint64 now);
gboolean chrono_routine_maintain(ChronoRoutine *routine, gint64 now);
gboolean chrono_routine_confirm(ChronoRoutine *routine, gint64 now);

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
