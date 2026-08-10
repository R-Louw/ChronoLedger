#include "time_parse.h"

#include <errno.h>
#include <stdlib.h>

static const char *
skip_spaces(const char *p)
{
  while (*p != '\0' && g_ascii_isspace(*p)) {
    p++;
  }
  return p;
}

static gboolean
add_duration(gint64 *total, gint64 value, gint64 multiplier)
{
  if (value <= 0 || multiplier <= 0) {
    return FALSE;
  }

  if (value > G_MAXINT64 / multiplier) {
    return FALSE;
  }

  gint64 addend = value * multiplier;
  if (*total > G_MAXINT64 - addend) {
    return FALSE;
  }

  *total += addend;
  return TRUE;
}

gboolean
chrono_parse_duration_seconds(const char *text, gint64 *out_seconds)
{
  g_return_val_if_fail(out_seconds != NULL, FALSE);

  if (text == NULL) {
    return FALSE;
  }

  const char *p = skip_spaces(text);
  if (*p == '\0' || *p == '+' || *p == '-') {
    return FALSE;
  }

  gint64 total = 0;
  gboolean consumed_any = FALSE;
  gboolean saw_unit = FALSE;

  while (*p != '\0') {
    p = skip_spaces(p);
    if (*p == '\0') {
      break;
    }

    errno = 0;
    char *end = NULL;
    gint64 value = g_ascii_strtoll(p, &end, 10);
    if (end == p || errno != 0 || value <= 0) {
      return FALSE;
    }

    p = skip_spaces(end);

    if (g_ascii_strncasecmp(p, "hours", 5) == 0) {
      if (!add_duration(&total, value, 3600)) {
        return FALSE;
      }
      p += 5;
      saw_unit = TRUE;
    } else if (g_ascii_strncasecmp(p, "hour", 4) == 0) {
      if (!add_duration(&total, value, 3600)) {
        return FALSE;
      }
      p += 4;
      saw_unit = TRUE;
    } else if (g_ascii_strncasecmp(p, "hrs", 3) == 0) {
      if (!add_duration(&total, value, 3600)) {
        return FALSE;
      }
      p += 3;
      saw_unit = TRUE;
    } else if (g_ascii_strncasecmp(p, "hr", 2) == 0) {
      if (!add_duration(&total, value, 3600)) {
        return FALSE;
      }
      p += 2;
      saw_unit = TRUE;
    } else if (g_ascii_strncasecmp(p, "h", 1) == 0) {
      if (!add_duration(&total, value, 3600)) {
        return FALSE;
      }
      p += 1;
      saw_unit = TRUE;
    } else if (g_ascii_strncasecmp(p, "minutes", 7) == 0) {
      if (!add_duration(&total, value, 60)) {
        return FALSE;
      }
      p += 7;
      saw_unit = TRUE;
    } else if (g_ascii_strncasecmp(p, "minute", 6) == 0) {
      if (!add_duration(&total, value, 60)) {
        return FALSE;
      }
      p += 6;
      saw_unit = TRUE;
    } else if (g_ascii_strncasecmp(p, "mins", 4) == 0) {
      if (!add_duration(&total, value, 60)) {
        return FALSE;
      }
      p += 4;
      saw_unit = TRUE;
    } else if (g_ascii_strncasecmp(p, "min", 3) == 0) {
      if (!add_duration(&total, value, 60)) {
        return FALSE;
      }
      p += 3;
      saw_unit = TRUE;
    } else if (g_ascii_strncasecmp(p, "m", 1) == 0) {
      if (!add_duration(&total, value, 60)) {
        return FALSE;
      }
      p += 1;
      saw_unit = TRUE;
    } else if (!saw_unit && *skip_spaces(p) == '\0') {
      if (!add_duration(&total, value, 60)) {
        return FALSE;
      }
    } else {
      return FALSE;
    }

    consumed_any = TRUE;
  }

  if (!consumed_any || total <= 0) {
    return FALSE;
  }

  *out_seconds = total;
  return TRUE;
}

char *
chrono_format_duration(gint64 seconds)
{
  if (seconds < 0) {
    seconds = 0;
  }

  gint64 hours = seconds / 3600;
  gint64 minutes = (seconds % 3600) / 60;
  gint64 secs = seconds % 60;

  if (hours > 0) {
    return g_strdup_printf("%" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT, hours, minutes, secs);
  }

  return g_strdup_printf("%02" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT, minutes, secs);
}

char *
chrono_format_compact_duration(gint64 seconds)
{
  if (seconds < 0)
    seconds = 0;

  gint64 hours = seconds / 3600;
  gint64 minutes = (seconds % 3600) / 60;
  gint64 secs = seconds % 60;
  if (hours > 0)
    return g_strdup_printf("%" G_GINT64_FORMAT "h %02" G_GINT64_FORMAT "m", hours, minutes);
  if (minutes > 0)
    return g_strdup_printf("%" G_GINT64_FORMAT "m %02" G_GINT64_FORMAT "s", minutes, secs);
  return g_strdup_printf("%" G_GINT64_FORMAT "s", secs);
}
