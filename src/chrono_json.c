#include "chrono_json.h"

#include <errno.h>
#include <math.h>
#include <string.h>

#define MAX_JSON_DEPTH 128u

typedef struct {
    const char *start;
    const char *cursor;
    const char *end;
    const char *problem;
} JsonInput;

static gboolean
invalid(JsonInput *input, const char *problem)
{
    if (input->problem == NULL)
        input->problem = problem;
    return FALSE;
}

static void
skip_space(JsonInput *input)
{
    while (input->cursor < input->end &&
           strchr(" \t\r\n", *input->cursor) != NULL)
        ++input->cursor;
}

static gboolean
take(JsonInput *input, char expected)
{
    skip_space(input);
    if (input->cursor == input->end || *input->cursor != expected)
        return FALSE;
    ++input->cursor;
    return TRUE;
}

static gboolean
hex_quad(JsonInput *input, gunichar *value)
{
    if (input->end - input->cursor < 4)
        return invalid(input, "Incomplete Unicode escape");
    *value = 0;
    for (guint i = 0; i < 4; ++i) {
        int digit = g_ascii_xdigit_value(*input->cursor++);
        if (digit < 0)
            return invalid(input, "Invalid Unicode escape");
        *value = (*value << 4) | (guint)digit;
    }
    return TRUE;
}

static gboolean
read_string(JsonInput *input, GString *decoded)
{
    if (!take(input, '"'))
        return invalid(input, "Expected a JSON string");
    while (input->cursor < input->end) {
        unsigned char c = (unsigned char)*input->cursor++;
        if (c == '"')
            return TRUE;
        if (c < 0x20)
            return invalid(input, "Unescaped control character");
        if (c != '\\') {
            if (decoded != NULL)
                g_string_append_c(decoded, (char)c);
            continue;
        }
        if (input->cursor == input->end)
            return invalid(input, "Incomplete string escape");
        c = (unsigned char)*input->cursor++;
        gunichar value;
        switch (c) {
        case '"': case '\\': case '/': value = c; break;
        case 'b': value = '\b'; break;
        case 'f': value = '\f'; break;
        case 'n': value = '\n'; break;
        case 'r': value = '\r'; break;
        case 't': value = '\t'; break;
        case 'u':
            if (!hex_quad(input, &value))
                return FALSE;
            if (value >= 0xd800 && value <= 0xdbff) {
                if (input->end - input->cursor < 2 ||
                    input->cursor[0] != '\\' || input->cursor[1] != 'u')
                    return invalid(input, "Unpaired Unicode surrogate");
                input->cursor += 2;
                gunichar low;
                if (!hex_quad(input, &low))
                    return FALSE;
                if (low < 0xdc00 || low > 0xdfff)
                    return invalid(input, "Unpaired Unicode surrogate");
                value = 0x10000 + ((value - 0xd800) << 10) + low - 0xdc00;
            } else if (value >= 0xdc00 && value <= 0xdfff) {
                return invalid(input, "Unpaired Unicode surrogate");
            }
            if (value == 0)
                return invalid(input, "NUL characters are not supported");
            break;
        default:
            return invalid(input, "Invalid string escape");
        }
        if (decoded != NULL)
            g_string_append_unichar(decoded, value);
    }
    return invalid(input, "Unterminated JSON string");
}

static gboolean read_value(JsonInput *input, guint depth);

static gboolean
read_object(JsonInput *input, guint depth)
{
    GHashTable *keys = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    gboolean ok = TRUE;
    if (take(input, '}'))
        goto out;
    do {
        GString *key = g_string_new(NULL);
        ok = read_string(input, key);
        if (ok && g_hash_table_contains(keys, key->str))
            ok = invalid(input, "Duplicate JSON member name");
        if (!ok) {
            g_string_free(key, TRUE);
            break;
        }
        g_hash_table_add(keys, g_string_free(key, FALSE));
        if (!take(input, ':') || !read_value(input, depth)) {
            ok = FALSE;
            break;
        }
        if (take(input, '}'))
            goto out;
    } while (take(input, ','));
    ok = FALSE;
out:
    g_hash_table_unref(keys);
    return ok;
}

static gboolean
read_number(JsonInput *input)
{
    const char *start = input->cursor;
    gboolean negative = take(input, '-');
    /* A sign cannot be separated from its digits by whitespace. */
    if (input->cursor == input->end || !g_ascii_isdigit(*input->cursor))
        return invalid(input, "Expected a number");
    guint64 limit = negative ? (guint64)G_MAXINT64 + 1u : (guint64)G_MAXINT64;
    guint64 magnitude = 0;
    gboolean overflow = FALSE;
    gboolean zero = *input->cursor == '0';
    do {
        guint digit = (guint)(*input->cursor++ - '0');
        if (magnitude > (limit - digit) / 10u)
            overflow = TRUE;
        else if (!overflow)
            magnitude = magnitude * 10u + digit;
        if (zero)
            break;
    } while (input->cursor < input->end && g_ascii_isdigit(*input->cursor));

    gboolean integer = TRUE;
    if (input->cursor < input->end && *input->cursor == '.') {
        integer = FALSE;
        ++input->cursor;
        if (input->cursor == input->end || !g_ascii_isdigit(*input->cursor))
            return invalid(input, "Missing fractional digits");
        while (input->cursor < input->end && g_ascii_isdigit(*input->cursor))
            ++input->cursor;
    }
    if (input->cursor < input->end &&
        (*input->cursor == 'e' || *input->cursor == 'E')) {
        integer = FALSE;
        ++input->cursor;
        if (input->cursor < input->end &&
            (*input->cursor == '+' || *input->cursor == '-'))
            ++input->cursor;
        if (input->cursor == input->end || !g_ascii_isdigit(*input->cursor))
            return invalid(input, "Missing exponent digits");
        while (input->cursor < input->end && g_ascii_isdigit(*input->cursor))
            ++input->cursor;
    }
    if (integer)
        return !overflow || invalid(input, "Integer exceeds signed 64-bit range");
    char *token = g_strndup(start, (gsize)(input->cursor - start));
    errno = 0;
    double value = g_ascii_strtod(token, NULL);
    gboolean finite = isfinite(value) && errno != ERANGE;
    g_free(token);
    return finite || invalid(input, "Floating-point number is out of range");
}

static gboolean
read_value(JsonInput *input, guint depth)
{
    skip_space(input);
    if (input->cursor == input->end)
        return invalid(input, "Missing JSON value");
    char c = *input->cursor;
    if (c == '{' || c == '[') {
        if (depth >= MAX_JSON_DEPTH)
            return invalid(input, "JSON nesting exceeds the safety limit");
        ++input->cursor;
        if (c == '{')
            return read_object(input, depth + 1);
        if (take(input, ']'))
            return TRUE;
        do {
            if (!read_value(input, depth + 1))
                return FALSE;
            if (take(input, ']'))
                return TRUE;
        } while (take(input, ','));
        return FALSE;
    }
    if (c == '"')
        return read_string(input, NULL);
    if (c == '-' || g_ascii_isdigit(c))
        return read_number(input);
    const char *literal = c == 't' ? "true" : c == 'f' ? "false" : "null";
    gsize length = strlen(literal);
    if ((gsize)(input->end - input->cursor) < length ||
        memcmp(input->cursor, literal, length) != 0)
        return invalid(input, "Invalid JSON value");
    input->cursor += length;
    return TRUE;
}

gboolean
chrono_json_validate(const char *data, gsize length, GError **error)
{
    if (data == NULL || length > G_MAXSSIZE ||
        !g_utf8_validate(data, (gssize)length, NULL)) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                            "JSON must be valid UTF-8 without NUL bytes");
        return FALSE;
    }
    JsonInput input = {.start = data, .cursor = data, .end = data + length};
    gboolean ok = read_value(&input, 0);
    skip_space(&input);
    if (ok && input.cursor == input.end)
        return TRUE;
    g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                "%s at byte %" G_GSIZE_FORMAT,
                input.problem != NULL ? input.problem : "Invalid JSON syntax",
                (gsize)(input.cursor - input.start));
    return FALSE;
}
