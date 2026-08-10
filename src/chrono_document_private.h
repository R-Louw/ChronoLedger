#pragma once

#include "chrono_document.h"

G_BEGIN_DECLS

/* Persistence-only transfer used to expose imported ledger data without
 * exposing imported preferences to the application controller. */
ChronoStore *chrono_document_take_store(ChronoDocument *document);

G_END_DECLS
