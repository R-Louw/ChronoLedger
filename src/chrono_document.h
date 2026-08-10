#pragma once

#include "chrono_model.h"
#include "chrono_preferences.h"

G_BEGIN_DECLS

typedef struct _ChronoDocument ChronoDocument;

ChronoDocument *chrono_document_new(void);
/* On success, ownership of store transfers to the document. On failure,
 * ownership remains with the caller. */
ChronoDocument *chrono_document_new_take(ChronoStore *store,
                                         const ChronoPreferences *preferences);
void chrono_document_free(ChronoDocument *document);

ChronoStore *chrono_document_get_store(ChronoDocument *document);
const ChronoStore *chrono_document_get_store_const(
    const ChronoDocument *document);
ChronoPreferences *chrono_document_get_preferences(ChronoDocument *document);
const ChronoPreferences *chrono_document_get_preferences_const(
    const ChronoDocument *document);

/* Installs a consistent replacement ledger while preserving preferences.
 * On success, ownership of store transfers and the previous owned ledger is
 * returned. On failure, ownership remains with the caller and NULL is
 * returned. */
ChronoStore *chrono_document_exchange_store(ChronoDocument *document,
                                            ChronoStore *store);
G_END_DECLS
