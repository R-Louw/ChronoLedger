#pragma once

#include <gio/gio.h>

#include "chrono_document.h"

G_BEGIN_DECLS

#define CHRONO_SCHEMA_VERSION 6
#define CHRONO_IMPORT_MAX_BYTES ((gsize)16 * 1024 * 1024)
#define CHRONO_IMPORT_MAX_TASKS CHRONO_STORE_MAX_TASKS

char *chrono_default_data_path(void);
ChronoDocument *chrono_persistence_load(const char *path, GError **error);
gboolean chrono_persistence_save(const ChronoDocument *document,
                                  const char *path,
                                  gint64 now_monotonic_us,
                                  GError **error);
/* Imports ledger records only. Local preferences are intentionally not part
 * of the returned value. */
ChronoStore *chrono_persistence_import_file(GFile *file, GError **error);
gboolean chrono_persistence_export_file(const ChronoDocument *document,
                                         GFile *file,
                                         gint64 now_monotonic_us,
                                         GError **error);

G_END_DECLS
