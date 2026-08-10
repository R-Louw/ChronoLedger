#include "chrono_document.h"
#include "chrono_model_private.h"

struct _ChronoDocument {
    ChronoStore *store;
    ChronoPreferences preferences;
};

ChronoDocument *
chrono_document_new(void)
{
    ChronoStore *store = chrono_store_new();
    if (store == NULL)
        return NULL;

    ChronoPreferences preferences;
    chrono_preferences_init_defaults(&preferences);
    ChronoDocument *document = chrono_document_new_take(store, &preferences);
    if (document == NULL)
        chrono_store_free(store);
    return document;
}

ChronoDocument *
chrono_document_new_take(ChronoStore *store,
                         const ChronoPreferences *preferences)
{
    if (store == NULL || preferences == NULL ||
        !chrono_store_is_consistent(store))
        return NULL;

    ChronoDocument *document = g_new0(ChronoDocument, 1);
    document->store = store;
    chrono_preferences_copy(&document->preferences, preferences);
    return document;
}

void
chrono_document_free(ChronoDocument *document)
{
    if (document == NULL)
        return;
    chrono_store_free(document->store);
    g_free(document);
}

ChronoStore *
chrono_document_get_store(ChronoDocument *document)
{
    return document != NULL ? document->store : NULL;
}

const ChronoStore *
chrono_document_get_store_const(const ChronoDocument *document)
{
    return document != NULL ? document->store : NULL;
}

ChronoPreferences *
chrono_document_get_preferences(ChronoDocument *document)
{
    return document != NULL ? &document->preferences : NULL;
}

const ChronoPreferences *
chrono_document_get_preferences_const(const ChronoDocument *document)
{
    return document != NULL ? &document->preferences : NULL;
}

ChronoStore *
chrono_document_exchange_store(ChronoDocument *document, ChronoStore *store)
{
    if (document == NULL || document->store == NULL || store == NULL ||
        !chrono_store_is_consistent(store))
        return NULL;
    ChronoStore *previous = document->store;
    document->store = store;
    return previous;
}

ChronoStore *
chrono_document_take_store(ChronoDocument *document)
{
    if (document == NULL || document->store == NULL)
        return NULL;
    ChronoStore *store = document->store;
    document->store = NULL;
    return store;
}
