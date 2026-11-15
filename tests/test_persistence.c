#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "chrono_persistence.h"
#include "chrono_json.h"

void register_persistence_tests(void);

static void
assert_rejected(const char *json, gsize length)
{
    GError *error = NULL;
    char *directory = g_dir_make_tmp("chronoledger-invalid-XXXXXX", &error);
    g_assert_no_error(error);
    char *path = g_build_filename(directory, "ledger.json", NULL);
    g_assert_true(g_file_set_contents(path, json, (gssize)length, &error));
    g_assert_no_error(error);
    g_assert_null(chrono_persistence_load(path, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    GFile *file = g_file_new_for_path(path);
    g_assert_null(chrono_persistence_import_file(file, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    g_object_unref(file);
    char *unchanged = NULL;
    gsize unchanged_length;
    g_assert_true(g_file_get_contents(path, &unchanged, &unchanged_length, &error));
    g_assert_no_error(error);
    g_assert_cmpmem(json, length, unchanged, unchanged_length);
    g_free(unchanged);
    g_assert_cmpint(g_remove(path), ==, 0);
    g_assert_cmpint(g_rmdir(directory), ==, 0);
    g_free(path);
    g_free(directory);
}

static void
test_invalid_documents(void)
{
    const char *invalid[] = {
        "{}",
        "{\"schema_version\":6,\"product\":\"ChronoLedger\"}",
        "{\"tasks\":null}",
        "{\"tasks\":[{\"id\":\"lost\"}],\"tasks\":[]}",
        "{\"tasks\":[],\"\\u0074asks\":[{\"id\":\"lost\"}]}",
        "{\"tasks\":[],\"x\":{\"key\":1,\"key\":2}}",
        "{\"tasks\":[],\"x\":9223372036854775808}",
        "{\"tasks\":[],\"x\":-9223372036854775809}",
        "{\"tasks\":[],\"x\":18446744073709551615}",
        "{\"tasks\":[],\"x\":\"A\\u0000lost\"}",
        "{\"tasks\":[],\"\\u0000\":true}",
        "{\"tasks\":[],\"x\":\"\\ud800\"}",
        "{\"tasks\":[],\"x\":\"\\udc00\"}",
        "{\"tasks\":[],\"x\":01}",
        "{\"tasks\":[],\"x\":- 1}",
        "{\"tasks\":[],\"x\":1e999}",
        "{\"tasks\":[],}",
        "{\"tasks\":[,]}",
        "{\"tasks\":[]} trailing",
    };
    for (guint i = 0; i < G_N_ELEMENTS(invalid); ++i) {
        g_test_message("Rejecting fixture %u", i);
        assert_rejected(invalid[i], strlen(invalid[i]));
    }
    const char nul[] = "{\"tasks\":[]}\0ignored";
    assert_rejected(nul, sizeof(nul) - 1);
}

static void
test_json_boundaries(void)
{
    const char *valid[] = {
        "{\"tasks\":[],\"min\":-9223372036854775808,\"max\":9223372036854775807}",
        "{\"x\":{\"key\":1},\"y\":{\"key\":2}}",
        "{\"escaped\":\"\\\\u0000\\\"\\b\\f\\n\\r\\t\\/\",\"unicode\":\"\\ud83d\\ude00\"}",
        "[null,true,false,-0,0.25,-1.5e+2,\"é\"]",
    };
    for (guint i = 0; i < G_N_ELEMENTS(valid); ++i) {
        GError *error = NULL;
        g_assert_true(chrono_json_validate(valid[i], strlen(valid[i]), &error));
        g_assert_no_error(error);
    }
    GString *nested = g_string_new(NULL);
    for (guint i = 0; i < 128; ++i)
        g_string_append_c(nested, '[');
    g_string_append_c(nested, '0');
    for (guint i = 0; i < 128; ++i)
        g_string_append_c(nested, ']');
    GError *error = NULL;
    g_assert_true(chrono_json_validate(nested->str, nested->len, &error));
    g_assert_no_error(error);
    g_string_prepend_c(nested, '[');
    g_string_append_c(nested, ']');
    g_assert_false(chrono_json_validate(nested->str, nested->len, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    g_string_free(nested, TRUE);
}

static void
test_legacy_migration(void)
{
    for (guint schema = 1; schema <= 6; ++schema) {
        GError *error = NULL;
        char *directory = g_dir_make_tmp("chronoledger-legacy-XXXXXX", &error);
        g_assert_no_error(error);
        char *path = g_build_filename(directory, "ledger.json", NULL);
        char *json = g_strdup_printf(
            "{\"schema_version\":%u,\"tasks\":[{\"id\":\"legacy\","
            "\"title\":\"Legacy\",\"estimate_seconds\":60,\"allocated_seconds\":60,"
            "\"elapsed_seconds\":15,\"remaining_seconds\":45,\"status\":\"active\"}]}",
            schema);
        g_assert_true(g_file_set_contents(path, json, -1, &error));
        ChronoDocument *document = chrono_persistence_load(path, &error);
        g_assert_no_error(error);
        g_assert_nonnull(document);
        const ChronoTask *task = chrono_store_get_task_at_const(
            chrono_document_get_store_const(document), 0);
        g_assert_cmpint(chrono_task_get_status(task), ==, CHRONO_TASK_PAUSED);
        g_assert_cmpint(chrono_task_get_elapsed_seconds(task), ==, 15);
        g_assert_true(chrono_persistence_save(document, path, G_USEC_PER_SEC, &error));
        g_assert_no_error(error);
        chrono_document_free(document);
        document = chrono_persistence_load(path, &error);
        g_assert_no_error(error);
        g_assert_nonnull(document);
        chrono_document_free(document);
        g_assert_cmpint(g_remove(path), ==, 0);
        g_assert_cmpint(g_rmdir(directory), ==, 0);
        g_free(json);
        g_free(path);
        g_free(directory);
    }
}

void
register_persistence_tests(void)
{
    g_test_add_func("/persistence/invalid-documents-preserve-file", test_invalid_documents);
    g_test_add_func("/persistence/json-boundaries", test_json_boundaries);
    g_test_add_func("/persistence/legacy-migration", test_legacy_migration);
}
