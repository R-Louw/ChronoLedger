#pragma once

#include <glib.h>

G_BEGIN_DECLS

gboolean chrono_parse_duration_seconds(const char *text, gint64 *out_seconds);
char *chrono_format_duration(gint64 seconds);
char *chrono_format_compact_duration(gint64 seconds);

G_END_DECLS
