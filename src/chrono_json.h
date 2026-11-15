#pragma once

#include <glib.h>

/* Validate bounded JSON before JSON-GLib can truncate strings, saturate
 * integers, or replace duplicate object members. */
gboolean chrono_json_validate(const char *data, gsize length, GError **error);
