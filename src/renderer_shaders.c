#include "renderer_shaders.h"

#include <string.h>
static const char *
shader_resource_path(RendererShaderId shader)
{
    static const char *const paths[] = {
        [RENDERER_SHADER_FULLSCREEN_VERTEX] = "/io/github/chronoledger/shaders/fullscreen.vert",
        [RENDERER_SHADER_COMPOSITE_FRAGMENT] = "/io/github/chronoledger/shaders/composite.frag",
        [RENDERER_SHADER_ANTI_ALIAS_FRAGMENT] = "/io/github/chronoledger/shaders/anti_alias.frag",
        [RENDERER_SHADER_GEOMETRY_VERTEX] = "/io/github/chronoledger/shaders/geometry.vert",
        [RENDERER_SHADER_GEOMETRY_FRAGMENT] = "/io/github/chronoledger/shaders/geometry.frag",
        [RENDERER_SHADER_CURSOR_AA_FRAGMENT] = "/io/github/chronoledger/shaders/cursor_aa.frag",
        [RENDERER_SHADER_CURSOR_PERSISTENCE_FRAGMENT] = "/io/github/chronoledger/shaders/cursor_persistence.frag",
        [RENDERER_SHADER_CURSOR_PHOSPHOR_FRAGMENT] = "/io/github/chronoledger/shaders/cursor_phosphor.frag",
        [RENDERER_SHADER_CRT_FRAGMENT] = "/io/github/chronoledger/shaders/crt.frag",
        [RENDERER_SHADER_PERSISTENCE_FRAGMENT] = "/io/github/chronoledger/shaders/persistence.frag",
    };

    if ((guint)shader >= G_N_ELEMENTS(paths))
        return NULL;
    return paths[shader];
}

char *
renderer_shader_load_source(RendererShaderId shader,
                            gboolean is_gles,
                            GError **error)
{
    const char *path = shader_resource_path(shader);
    if (path == NULL) {
        g_set_error(error,
                    g_quark_from_static_string("chronoledger-renderer-shader"),
                    1,
                    "Unknown renderer shader identifier %u",
                    (guint)shader);
        return NULL;
    }

    GBytes *bytes = g_resources_lookup_data(path,
                                            0,
                                            error);
    if (bytes == NULL)
        return NULL;

    gsize length = 0;
    const char *body = g_bytes_get_data(bytes, &length);
    const char *header = is_gles
        ? "#version 300 es\nprecision highp float;\nprecision highp int;\n"
        : "#version 130\n";
    const gsize header_length = strlen(header);

    if (G_UNLIKELY(body == NULL && length != 0)) {
        g_set_error_literal(error,
                            g_quark_from_static_string("chronoledger-renderer-shader"),
                            2,
                            "Shader resource has no data");
        g_bytes_unref(bytes);
        return NULL;
    }

    if (G_UNLIKELY(length > G_MAXSIZE - header_length - 1)) {
        g_set_error_literal(error,
                            g_quark_from_static_string("chronoledger-renderer-shader"),
                            3,
                            "Shader resource is too large");
        g_bytes_unref(bytes);
        return NULL;
    }

    char *source = g_malloc(header_length + length + 1);
    memcpy(source, header, header_length);
    if (length != 0)
        memcpy(source + header_length, body, length);
    source[header_length + length] = '\0';
    g_bytes_unref(bytes);
    return source;
}
