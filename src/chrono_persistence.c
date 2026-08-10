#include "chrono_persistence.h"
#include "chrono_document_private.h"
#include "chrono_model_private.h"

#include <errno.h>
#include <string.h>
#include <json-glib/json-glib.h>
#include <glib/gstdio.h>

char *
chrono_default_data_path(void)
{
    const char *override = g_getenv("CHRONOLEDGER_FILE");
    if (override != NULL && *override != '\0')
        return g_strdup(override);
    return g_build_filename(g_get_user_data_dir(), "chronoledger", "ledger.json", NULL);
}

static gboolean
ensure_parent_dir(const char *path, GError **error)
{
    char *parent = g_path_get_dirname(path);
    if (g_mkdir_with_parents(parent, 0700) != 0) {
        int saved_errno = errno;
        g_set_error(error,
                    G_FILE_ERROR,
                    g_file_error_from_errno(saved_errno),
                    "Could not create %s: %s",
                    parent,
                    g_strerror(saved_errno));
        g_free(parent);
        return FALSE;
    }
    g_free(parent);
    return TRUE;
}

static gboolean
json_member_is(JsonObject *object, const char *name, GType type)
{
    if (!json_object_has_member(object, name))
        return FALSE;
    JsonNode *node = json_object_get_member(object, name);
    return JSON_NODE_HOLDS_VALUE(node) &&
           (GType)json_node_get_value_type(node) == type;
}

static gboolean
get_required_string(JsonObject *object, const char *name, const char **out)
{
    if (!json_member_is(object, name, G_TYPE_STRING))
        return FALSE;
    const char *value = json_object_get_string_member(object, name);
    if (value == NULL || !g_utf8_validate(value, -1, NULL))
        return FALSE;
    *out = value;
    return TRUE;
}

static gboolean
get_required_int(JsonObject *object, const char *name, gint64 *out)
{
    if (!json_object_has_member(object, name))
        return FALSE;
    JsonNode *node = json_object_get_member(object, name);
    if (!JSON_NODE_HOLDS_VALUE(node))
        return FALSE;
    GType type = json_node_get_value_type(node);
    if (type != G_TYPE_INT64 && type != G_TYPE_INT && type != G_TYPE_LONG)
        return FALSE;
    *out = json_object_get_int_member(object, name);
    return TRUE;
}

static gboolean
get_optional_int(JsonObject *object, const char *name, gint64 *out)
{
    if (!json_object_has_member(object, name)) {
        *out = 0;
        return TRUE;
    }
    return get_required_int(object, name, out);
}

static gboolean
get_optional_bool(JsonObject *object,
                  const char *name,
                  gboolean fallback,
                  gboolean *out)
{
    if (!json_object_has_member(object, name)) {
        *out = fallback;
        return TRUE;
    }
    JsonNode *node = json_object_get_member(object, name);
    if (!JSON_NODE_HOLDS_VALUE(node) || json_node_get_value_type(node) != G_TYPE_BOOLEAN)
        return FALSE;
    *out = json_object_get_boolean_member(object, name);
    return TRUE;
}

static gboolean
validate_document_for_serialization(const ChronoDocument *document,
                                    gint64 now_monotonic_us,
                                    GError **error)
{
    const ChronoStore *store = chrono_document_get_store_const(document);
    if (store == NULL || !chrono_store_is_consistent(store)) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                    "ChronoLedger store is not initialized or is inconsistent");
        return FALSE;
    }
    guint task_count = chrono_store_get_task_count(store);
    if (task_count > CHRONO_IMPORT_MAX_TASKS) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                    "ChronoLedger data exceeds the %u-task safety limit",
                    CHRONO_IMPORT_MAX_TASKS);
        return FALSE;
    }
    if (now_monotonic_us <= 0) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                    "A positive monotonic timestamp is required to save data");
        return FALSE;
    }

    for (guint i = 0; i < task_count; ++i) {
        const ChronoTask *task = chrono_store_get_task_at_const(store, i);
        if (chrono_task_get_status(task) == CHRONO_TASK_ACTIVE &&
            now_monotonic_us < chrono_task_get_active_started_monotonic_us(task)) {
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                        "The save timestamp predates task %u's active interval",
                        i + 1);
            return FALSE;
        }
    }
    return TRUE;
}

static JsonNode *
serialize_document(const ChronoDocument *document, gint64 now_monotonic_us)
{
    const ChronoStore *store = chrono_document_get_store_const(document);
    const ChronoPreferences *preferences =
        chrono_document_get_preferences_const(document);
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "schema_version");
    json_builder_add_int_value(builder, CHRONO_SCHEMA_VERSION);
    json_builder_set_member_name(builder, "product");
    json_builder_add_string_value(builder, "ChronoLedger");
    json_builder_set_member_name(builder, "notifications_enabled");
    json_builder_add_boolean_value(builder, preferences->notifications_enabled);
    json_builder_set_member_name(builder, "sound_enabled");
    json_builder_add_boolean_value(builder, preferences->sound_enabled);
    json_builder_set_member_name(builder, "phosphor_mask_enabled");
    json_builder_add_boolean_value(builder, preferences->phosphor_mask_enabled);
    json_builder_set_member_name(builder, "curvature_enabled");
    json_builder_add_boolean_value(builder, preferences->curvature_enabled);
    json_builder_set_member_name(builder, "cursor_shader_effects_enabled");
    json_builder_add_boolean_value(
        builder, preferences->cursor_shader_effects_enabled);
    json_builder_set_member_name(builder, "tasks");
    json_builder_begin_array(builder);

    guint task_count = chrono_store_get_task_count(store);
    for (guint i = 0; i < task_count; ++i) {
        const ChronoTask *task = chrono_store_get_task_at_const(store, i);
        gint64 remaining = chrono_task_effective_remaining(task, now_monotonic_us);
        gint64 elapsed = chrono_task_effective_elapsed(task, now_monotonic_us);
        ChronoTaskStatus status = chrono_task_get_status(task);
        if (status == CHRONO_TASK_ACTIVE)
            status = remaining == 0 ? CHRONO_TASK_EXPIRED : CHRONO_TASK_PAUSED;

        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "id");
        json_builder_add_string_value(builder, chrono_task_get_id(task));
        json_builder_set_member_name(builder, "title");
        json_builder_add_string_value(builder, chrono_task_get_title(task));
        json_builder_set_member_name(builder, "estimate_seconds");
        json_builder_add_int_value(builder, chrono_task_get_estimate_seconds(task));
        json_builder_set_member_name(builder, "allocated_seconds");
        json_builder_add_int_value(builder, chrono_task_get_allocated_seconds(task));
        json_builder_set_member_name(builder, "elapsed_seconds");
        json_builder_add_int_value(builder, elapsed);
        json_builder_set_member_name(builder, "remaining_seconds");
        json_builder_add_int_value(builder, remaining);
        json_builder_set_member_name(builder, "status");
        json_builder_add_string_value(builder, chrono_task_status_to_string(status));
        json_builder_set_member_name(builder, "created_at_unix");
        json_builder_add_int_value(builder, chrono_task_get_created_at_unix(task));
        json_builder_set_member_name(builder, "completed_at_unix");
        json_builder_add_int_value(builder, chrono_task_get_completed_at_unix(task));
        json_builder_end_object(builder);
    }

    json_builder_end_array(builder);
    json_builder_end_object(builder);
    JsonNode *root = json_builder_get_root(builder);
    g_object_unref(builder);
    return root;
}

static gboolean
document_to_bytes(const ChronoDocument *document,
                  gint64 now_monotonic_us,
                  char **out_data,
                  gsize *out_length,
                  GError **error)
{
    g_return_val_if_fail(out_data != NULL, FALSE);
    g_return_val_if_fail(out_length != NULL, FALSE);
    *out_data = NULL;
    *out_length = 0;
    if (!validate_document_for_serialization(document, now_monotonic_us, error))
        return FALSE;

    JsonNode *root = serialize_document(document, now_monotonic_us);
    JsonGenerator *generator = json_generator_new();
    json_generator_set_root(generator, root);
    json_generator_set_pretty(generator, TRUE);
    *out_data = json_generator_to_data(generator, out_length);
    json_node_free(root);
    g_object_unref(generator);
    if (*out_data == NULL) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                    "Could not serialize ChronoLedger data");
        return FALSE;
    }
    if (*out_length > CHRONO_IMPORT_MAX_BYTES) {
        g_clear_pointer(out_data, g_free);
        *out_length = 0;
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_NOSPC,
                    "Serialized data exceeds the 16 MiB safety limit");
        return FALSE;
    }
    return TRUE;
}

static gboolean
replace_file_contents(GFile *file,
                      const char *data,
                      gsize length,
                      GError **error)
{
    return g_file_replace_contents(file,
                                   data,
                                   length,
                                   NULL,
                                   FALSE,
                                   G_FILE_CREATE_PRIVATE,
                                   NULL,
                                   NULL,
                                   error);
}

gboolean
chrono_persistence_save(const ChronoDocument *document,
                        const char *path,
                        gint64 now_monotonic_us,
                        GError **error)
{
    g_return_val_if_fail(document != NULL, FALSE);
    g_return_val_if_fail(path != NULL, FALSE);

    char *data = NULL;
    gsize length = 0;
    if (!document_to_bytes(document, now_monotonic_us, &data, &length, error))
        return FALSE;
    if (!ensure_parent_dir(path, error)) {
        g_free(data);
        return FALSE;
    }
    GFile *file = g_file_new_for_path(path);
    gboolean ok = replace_file_contents(file, data, length, error);
    g_object_unref(file);
    g_free(data);
    return ok;
}

gboolean
chrono_persistence_export_file(const ChronoDocument *document,
                               GFile *file,
                               gint64 now_monotonic_us,
                               GError **error)
{
    g_return_val_if_fail(document != NULL, FALSE);
    g_return_val_if_fail(G_IS_FILE(file), FALSE);
    char *data = NULL;
    gsize length = 0;
    if (!document_to_bytes(document, now_monotonic_us, &data, &length, error))
        return FALSE;
    gboolean ok = replace_file_contents(file, data, length, error);
    g_free(data);
    return ok;
}

static ChronoTask *
load_task(JsonObject *object,
          gint64 schema_version,
          gboolean *was_active_out,
          gboolean *discard_legacy_out)
{
    if (was_active_out != NULL)
        *was_active_out = FALSE;
    if (discard_legacy_out != NULL)
        *discard_legacy_out = FALSE;
    const char *id = NULL;
    const char *title = NULL;
    const char *status_text = NULL;
    gint64 estimate = 0;
    gint64 allocated = 0;
    gint64 elapsed = 0;
    gint64 remaining = 0;
    gint64 created = 0;
    gint64 completed = 0;
    ChronoTaskStatus status;
    gboolean discard_legacy = FALSE;

    /* Schema 3 introduced bundled sample records. They are not production
     * ledger entries and are discarded during migration. */
    if (schema_version >= 3 &&
        !get_optional_bool(object, "is_demo", FALSE, &discard_legacy))
        return NULL;
    if (discard_legacy_out != NULL)
        *discard_legacy_out = discard_legacy;

    if (!get_required_string(object, "id", &id) || *id == '\0' ||
        strlen(id) > CHRONO_TASK_ID_MAX_BYTES ||
        !get_required_string(object, "title", &title) ||
        !get_required_int(object, "estimate_seconds", &estimate) ||
        !get_required_int(object, "remaining_seconds", &remaining) ||
        !get_required_string(object, "status", &status_text) ||
        !chrono_task_status_from_string(status_text, &status) ||
        !get_optional_int(object, "created_at_unix", &created) ||
        !get_optional_int(object, "completed_at_unix", &completed))
        return NULL;

    if (schema_version >= 2) {
        if (!get_required_int(object, "allocated_seconds", &allocated) ||
            !get_required_int(object, "elapsed_seconds", &elapsed))
            return NULL;
    } else {
        if (estimate <= 0 || remaining < 0 || remaining > estimate)
            return NULL;
        allocated = estimate;
        elapsed = estimate - remaining;
    }

    if (estimate <= 0 || allocated <= 0 || allocated < estimate ||
        elapsed < 0 || remaining < 0 ||
        elapsed > allocated || remaining > allocated ||
        allocated - elapsed != remaining || created < 0 || completed < 0 ||
        (completed > 0 && completed < created))
        return NULL;
    if (status == CHRONO_TASK_DONE && completed <= 0)
        return NULL;
    if (status != CHRONO_TASK_DONE && completed != 0)
        return NULL;
    if (remaining == 0 && status != CHRONO_TASK_DONE)
        status = CHRONO_TASK_EXPIRED;
    else if (status == CHRONO_TASK_EXPIRED)
        return NULL;
    if (status == CHRONO_TASK_ACTIVE) {
        if (was_active_out != NULL)
            *was_active_out = TRUE;
        status = CHRONO_TASK_PAUSED;
    }

    return chrono_task_new_loaded(id, title, estimate, allocated, elapsed,
                                  remaining, status, created, completed);
}

static ChronoDocument *
create_loaded_document(ChronoStore *store,
                       const ChronoPreferences *preferences,
                       GError **error)
{
    ChronoDocument *document = chrono_document_new_take(store, preferences);
    if (document != NULL)
        return document;

    chrono_store_free(store);
    g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                "Could not construct the loaded ChronoLedger document");
    return NULL;
}

static ChronoDocument *
parse_document(const char *data, gssize length, GError **error)
{
    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_data(parser, data, length, error)) {
        g_object_unref(parser);
        return NULL;
    }

    JsonNode *root_node = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_OBJECT(root_node)) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                    "Root JSON value must be an object");
        g_object_unref(parser);
        return NULL;
    }

    JsonObject *root = json_node_get_object(root_node);
    gint64 schema_version = 1;
    if (json_object_has_member(root, "schema_version") &&
        !get_required_int(root, "schema_version", &schema_version)) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                    "schema_version must be an integer");
        g_object_unref(parser);
        return NULL;
    }
    if (schema_version < 1 || schema_version > CHRONO_SCHEMA_VERSION) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                    "Unsupported schema version %" G_GINT64_FORMAT,
                    schema_version);
        g_object_unref(parser);
        return NULL;
    }

    ChronoStore *store = chrono_store_new();
    ChronoPreferences preferences;
    chrono_preferences_init_defaults(&preferences);
    gboolean curvature_valid = json_object_has_member(root, "curvature_enabled")
        ? get_optional_bool(root, "curvature_enabled", FALSE,
                            &preferences.curvature_enabled)
        : get_optional_bool(root, "curvature_test_enabled", FALSE,
                            &preferences.curvature_enabled);
    if (!curvature_valid ||
        !get_optional_bool(root, "notifications_enabled", TRUE,
                           &preferences.notifications_enabled) ||
        !get_optional_bool(root, "sound_enabled", FALSE,
                           &preferences.sound_enabled) ||
        !get_optional_bool(root, "phosphor_mask_enabled", TRUE,
                           &preferences.phosphor_mask_enabled) ||
        !get_optional_bool(root, "cursor_shader_effects_enabled", TRUE,
                           &preferences.cursor_shader_effects_enabled)) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                    "Boolean settings must contain true or false values");
        chrono_store_free(store);
        g_object_unref(parser);
        return NULL;
    }

    if (!json_object_has_member(root, "tasks")) {
        g_object_unref(parser);
        return create_loaded_document(store, &preferences, error);
    }
    JsonNode *tasks_node = json_object_get_member(root, "tasks");
    if (!JSON_NODE_HOLDS_ARRAY(tasks_node)) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                    "tasks must be an array");
        chrono_store_free(store);
        g_object_unref(parser);
        return NULL;
    }

    GHashTable *ids = g_hash_table_new(g_str_hash, g_str_equal);
    JsonArray *tasks = json_node_get_array(tasks_node);
    guint count = json_array_get_length(tasks);
    if (count > CHRONO_IMPORT_MAX_TASKS) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                    "JSON data exceeds the %u-task safety limit",
                    CHRONO_IMPORT_MAX_TASKS);
        g_hash_table_unref(ids);
        chrono_store_free(store);
        g_object_unref(parser);
        return NULL;
    }
    guint imported_active_count = 0;
    for (guint i = 0; i < count; ++i) {
        JsonNode *node = json_array_get_element(tasks, i);
        gboolean was_active = FALSE;
        gboolean discard_legacy = FALSE;
        ChronoTask *task = JSON_NODE_HOLDS_OBJECT(node)
            ? load_task(json_node_get_object(node), schema_version,
                        &was_active, &discard_legacy)
            : NULL;
        if (task != NULL && discard_legacy) {
            chrono_task_free(task);
            continue;
        }
        gboolean duplicate = task != NULL && g_hash_table_contains(ids, chrono_task_get_id(task));
        if (task == NULL || duplicate) {
            chrono_task_free(task);
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                        duplicate
                            ? "Task %u duplicates an earlier task ID"
                            : "Task %u contains invalid or inconsistent data",
                        i + 1);
            g_hash_table_unref(ids);
            chrono_store_free(store);
            g_object_unref(parser);
            return NULL;
        }
        if (was_active && ++imported_active_count > 1) {
            chrono_task_free(task);
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                        "Task %u creates multiple active allocations", i + 1);
            g_hash_table_unref(ids);
            chrono_store_free(store);
            g_object_unref(parser);
            return NULL;
        }
        /* load_task and the ID set establish every store invariant here.
         * Append directly so a maximum-size import remains linear-time. */
        if (!chrono_store_adopt_loaded_task(store, task)) {
            chrono_task_free(task);
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                        "Task %u could not be added to the ledger", i + 1);
            g_hash_table_unref(ids);
            chrono_store_free(store);
            g_object_unref(parser);
            return NULL;
        }
        g_hash_table_add(ids, (gpointer)chrono_task_get_id(task));
    }

    g_hash_table_unref(ids);
    g_object_unref(parser);
    return create_loaded_document(store, &preferences, error);
}

static gboolean
load_file_contents_bounded(GFile *file,
                           char **data_out,
                           gsize *length_out,
                           GError **error)
{
    g_return_val_if_fail(G_IS_FILE(file), FALSE);
    g_return_val_if_fail(data_out != NULL, FALSE);
    g_return_val_if_fail(length_out != NULL, FALSE);

    *data_out = NULL;
    *length_out = 0;
    GFileInputStream *stream = g_file_read(file, NULL, error);
    if (stream == NULL)
        return FALSE;

    const gsize capacity = CHRONO_IMPORT_MAX_BYTES + 1u;
    char *data = g_malloc(capacity);
    gsize length = 0;
    gboolean ok = TRUE;
    while (length < capacity) {
        gssize count = g_input_stream_read(G_INPUT_STREAM(stream),
                                           data + length,
                                           capacity - length,
                                           NULL,
                                           error);
        if (count < 0) {
            ok = FALSE;
            break;
        }
        if (count == 0)
            break;
        length += (gsize)count;
    }

    if (ok && length == capacity) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                    "JSON file exceeds the 16 MiB safety limit");
        ok = FALSE;
    }
    g_input_stream_close(G_INPUT_STREAM(stream), NULL, NULL);
    g_object_unref(stream);

    if (!ok) {
        g_free(data);
        return FALSE;
    }
    *data_out = data;
    *length_out = length;
    return TRUE;
}

static ChronoDocument *
load_file(GFile *file, GError **error)
{
    char *data = NULL;
    gsize length = 0;
    if (!load_file_contents_bounded(file, &data, &length, error))
        return NULL;

    ChronoDocument *document = parse_document(data, (gssize)length, error);
    g_free(data);
    return document;
}

ChronoDocument *
chrono_persistence_load(const char *path, GError **error)
{
    g_return_val_if_fail(path != NULL, NULL);

    GFile *file = g_file_new_for_path(path);
    GError *load_error = NULL;
    ChronoDocument *document = load_file(file, &load_error);
    g_object_unref(file);

    if (document != NULL) {
        g_clear_error(&load_error);
        return document;
    }
    if (g_error_matches(load_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
        g_clear_error(&load_error);
        return chrono_document_new();
    }
    if (load_error == NULL) {
        g_set_error(&load_error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                    "Could not load ChronoLedger data");
    }
    if (error != NULL)
        g_propagate_error(error, load_error);
    else
        g_clear_error(&load_error);
    return NULL;
}

ChronoStore *
chrono_persistence_import_file(GFile *file, GError **error)
{
    g_return_val_if_fail(G_IS_FILE(file), NULL);

    ChronoDocument *document = load_file(file, error);
    if (document == NULL)
        return NULL;

    ChronoStore *store = chrono_document_take_store(document);
    chrono_document_free(document);
    if (store == NULL) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                    "Imported document did not contain a ledger");
        return NULL;
    }
    return store;
}
