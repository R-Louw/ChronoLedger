#include "renderer.h"
#include "renderer_state.h"
#include "renderer_shaders.h"
#include "app.h"

#include <stddef.h>
#include <string.h>

#define CHRONO_GL_LOG_MAX_BYTES (1024 * 1024)

static void
clear_gl_errors(void)
{
    for (int i = 0; i < 32 && glGetError() != GL_NO_ERROR; ++i) {
    }
}

static char *
allocate_gl_log_buffer(GLint reported_length, GLsizei *length_out)
{
    GLint bounded_length = CLAMP(reported_length, 1,
                                 CHRONO_GL_LOG_MAX_BYTES);
    *length_out = (GLsizei)bounded_length;
    return g_malloc0((gsize)bounded_length);
}

static void release_gl_resources(Renderer *renderer)
{
    if (renderer->composed_fbo != 0)
        glDeleteFramebuffers(1, &renderer->composed_fbo);
    if (renderer->aa_fbo != 0)
        glDeleteFramebuffers(1, &renderer->aa_fbo);
    if (renderer->aa_refine_fbo != 0)
        glDeleteFramebuffers(1, &renderer->aa_refine_fbo);
    if (renderer->history_fbo != 0)
        glDeleteFramebuffers(1, &renderer->history_fbo);
    if (renderer->final_fbo != 0)
        glDeleteFramebuffers(1, &renderer->final_fbo);
    if (renderer->cursor_scene_fbo != 0)
        glDeleteFramebuffers(1, &renderer->cursor_scene_fbo);
    if (renderer->cursor_aa_fbo != 0)
        glDeleteFramebuffers(1, &renderer->cursor_aa_fbo);
    if (renderer->cursor_history_fbo != 0)
        glDeleteFramebuffers(1, &renderer->cursor_history_fbo);
    if (renderer->history_textures[0] != 0 || renderer->history_textures[1] != 0)
        glDeleteTextures(2, renderer->history_textures);
    if (renderer->cursor_history_textures[0] != 0 ||
        renderer->cursor_history_textures[1] != 0)
        glDeleteTextures(2, renderer->cursor_history_textures);
    if (renderer->aa_texture != 0)
        glDeleteTextures(1, &renderer->aa_texture);
    if (renderer->aa_refine_texture != 0)
        glDeleteTextures(1, &renderer->aa_refine_texture);
    if (renderer->text_mask_texture != 0)
        glDeleteTextures(1, &renderer->text_mask_texture);
    if (renderer->final_texture != 0)
        glDeleteTextures(1, &renderer->final_texture);
    if (renderer->cursor_scene_texture != 0)
        glDeleteTextures(1, &renderer->cursor_scene_texture);
    if (renderer->cursor_aa_texture != 0)
        glDeleteTextures(1, &renderer->cursor_aa_texture);
    if (renderer->composed_texture != 0)
        glDeleteTextures(1, &renderer->composed_texture);
    if (renderer->scene_texture != 0)
        glDeleteTextures(1, &renderer->scene_texture);
    if (renderer->upload_pbos[0] != 0 ||
        renderer->upload_pbos[1] != 0 ||
        renderer->upload_pbos[2] != 0)
        glDeleteBuffers(CHRONO_UPLOAD_PBO_COUNT, renderer->upload_pbos);
    if (renderer->text_mask_pbos[0] != 0 ||
        renderer->text_mask_pbos[1] != 0 ||
        renderer->text_mask_pbos[2] != 0)
        glDeleteBuffers(CHRONO_UPLOAD_PBO_COUNT, renderer->text_mask_pbos);
    if (renderer->geometry_vbo != 0)
        glDeleteBuffers(1, &renderer->geometry_vbo);
    if (renderer->geometry_vao != 0)
        glDeleteVertexArrays(1, &renderer->geometry_vao);
    if (renderer->vao != 0)
        glDeleteVertexArrays(1, &renderer->vao);
    if (renderer->geometry_program != 0)
        glDeleteProgram(renderer->geometry_program);
    if (renderer->cursor_aa_program != 0)
        glDeleteProgram(renderer->cursor_aa_program);
    if (renderer->cursor_persistence_program != 0)
        glDeleteProgram(renderer->cursor_persistence_program);
    if (renderer->cursor_phosphor_program != 0)
        glDeleteProgram(renderer->cursor_phosphor_program);
    if (renderer->aa_program != 0)
        glDeleteProgram(renderer->aa_program);
    if (renderer->composite_program != 0)
        glDeleteProgram(renderer->composite_program);
    if (renderer->persistence_program != 0)
        glDeleteProgram(renderer->persistence_program);
    if (renderer->program != 0)
        glDeleteProgram(renderer->program);
    g_clear_pointer(&renderer->geometry_vertices, g_array_unref);

    gboolean is_gles = renderer->is_gles;
    g_clear_pointer(&renderer->vendor_string, g_free);
    g_clear_pointer(&renderer->renderer_string, g_free);
    *renderer = (Renderer){
        .is_gles = is_gles,
    };
}

static GLuint compile_shader(GLenum type,
                             const char *source,
                             char **error_out)
{
    GLuint shader = glCreateShader(type);
    if (shader == 0) {
        const char *stage = type == GL_VERTEX_SHADER ? "vertex" : "fragment";
        if (error_out != NULL)
            *error_out = g_strdup_printf("Unable to create the %s shader object", stage);
        return 0;
    }

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE)
        return shader;

    GLint log_length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
    GLsizei safe_log_length = 0;
    char *log = allocate_gl_log_buffer(log_length, &safe_log_length);
    glGetShaderInfoLog(shader, safe_log_length, NULL, log);

    const char *stage = type == GL_VERTEX_SHADER ? "vertex" : "fragment";
    g_warning("%s shader compilation failed: %s", stage, log);
    if (error_out != NULL)
        *error_out = g_strdup_printf("%s shader compilation failed:\n%s", stage, log);

    g_free(log);
    glDeleteShader(shader);
    return 0;
}

static char *
load_shader_source(RendererShaderId shader, gboolean is_gles, char **error_out)
{
    GError *error = NULL;
    char *source = renderer_shader_load_source(shader, is_gles, &error);
    if (source == NULL && error_out != NULL)
        *error_out = g_strdup(error != NULL ? error->message
                                           : "Shader resource could not be loaded");
    g_clear_error(&error);
    return source;
}

static GLuint create_program(gboolean is_gles,
                             RendererShaderId fragment_shader_id,
                             char **error_out)
{
    char *vertex_source = load_shader_source(RENDERER_SHADER_FULLSCREEN_VERTEX,
                                             is_gles, error_out);
    if (vertex_source == NULL)
        return 0;
    char *fragment_source = load_shader_source(fragment_shader_id,
                                               is_gles, error_out);
    if (fragment_source == NULL) {
        g_free(vertex_source);
        return 0;
    }

    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER,
                                          vertex_source,
                                          error_out);
    g_free(vertex_source);
    if (vertex_shader == 0) {
        g_free(fragment_source);
        return 0;
    }

    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER,
                                            fragment_source,
                                            error_out);
    g_free(fragment_source);
    if (fragment_shader == 0) {
        glDeleteShader(vertex_shader);
        return 0;
    }

    GLuint program = glCreateProgram();
    if (program == 0) {
        if (error_out != NULL)
            *error_out = g_strdup("Unable to create the shader program object");
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        return 0;
    }

    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_TRUE)
        return program;

    GLint log_length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
    GLsizei safe_log_length = 0;
    char *log = allocate_gl_log_buffer(log_length, &safe_log_length);
    glGetProgramInfoLog(program, safe_log_length, NULL, log);
    g_warning("Shader link failed: %s", log);
    if (error_out != NULL)
        *error_out = g_strdup_printf("Shader program link failed:\n%s", log);

    g_free(log);
    glDeleteProgram(program);
    return 0;
}

static GLuint create_geometry_program(gboolean is_gles, char **error_out)
{
    char *vertex_source = load_shader_source(RENDERER_SHADER_GEOMETRY_VERTEX,
                                             is_gles, error_out);
    if (vertex_source == NULL)
        return 0;
    char *fragment_source = load_shader_source(RENDERER_SHADER_GEOMETRY_FRAGMENT,
                                               is_gles, error_out);
    if (fragment_source == NULL) {
        g_free(vertex_source);
        return 0;
    }

    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER,
                                          vertex_source,
                                          error_out);
    g_free(vertex_source);
    if (vertex_shader == 0) {
        g_free(fragment_source);
        return 0;
    }

    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER,
                                            fragment_source,
                                            error_out);
    g_free(fragment_source);
    if (fragment_shader == 0) {
        glDeleteShader(vertex_shader);
        return 0;
    }

    GLuint program = glCreateProgram();
    if (program == 0) {
        if (error_out != NULL)
            *error_out = g_strdup("Unable to create the geometry shader program object");
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        return 0;
    }

    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glBindAttribLocation(program, 0, "a_position");
    glBindAttribLocation(program, 1, "a_color");
    glLinkProgram(program);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_TRUE)
        return program;

    GLint log_length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
    GLsizei safe_log_length = 0;
    char *log = allocate_gl_log_buffer(log_length, &safe_log_length);
    glGetProgramInfoLog(program, safe_log_length, NULL, log);
    if (error_out != NULL)
        *error_out = g_strdup_printf("Geometry shader program link failed:\n%s", log);
    g_warning("Geometry shader link failed: %s", log);
    g_free(log);
    glDeleteProgram(program);
    return 0;
}

static GLuint create_crt_program(gboolean is_gles, char **error_out)
{
    return create_program(is_gles, RENDERER_SHADER_CRT_FRAGMENT, error_out);
}

static gboolean cache_uniform_locations(Renderer *renderer, char **error_out)
{
    renderer->u_scene = glGetUniformLocation(renderer->program, "u_scene");
    renderer->u_history = glGetUniformLocation(renderer->program, "u_history");
    renderer->u_inverse_resolution = glGetUniformLocation(renderer->program,
                                                         "u_inverse_resolution");
    renderer->u_curvature = glGetUniformLocation(renderer->program, "u_curvature");
    renderer->u_glow = glGetUniformLocation(renderer->program, "u_glow");
    renderer->u_vignette = glGetUniformLocation(renderer->program, "u_vignette");
    renderer->u_mask_strength = glGetUniformLocation(renderer->program, "u_mask_strength");
    renderer->u_palette_low = glGetUniformLocation(renderer->program, "u_palette_low");
    renderer->u_palette_mid = glGetUniformLocation(renderer->program, "u_palette_mid");
    renderer->u_palette_high = glGetUniformLocation(renderer->program, "u_palette_high");
    renderer->u_palette_midpoint = glGetUniformLocation(renderer->program,
                                                         "u_palette_midpoint");
    renderer->u_palette_mix = glGetUniformLocation(renderer->program, "u_palette_mix");
    renderer->u_palette_mode = glGetUniformLocation(renderer->program, "u_palette_mode");
    renderer->u_palette_spatial_mix = glGetUniformLocation(renderer->program,
                                                            "u_palette_spatial_mix");
    renderer->u_history_enabled = glGetUniformLocation(renderer->program,
                                                        "u_history_enabled");
    renderer->u_history_strength = glGetUniformLocation(renderer->program,
                                                         "u_history_strength");

    renderer->c_source = glGetUniformLocation(renderer->composite_program,
                                               "u_source");

    renderer->a_source = glGetUniformLocation(renderer->aa_program, "u_source");
    renderer->a_original = glGetUniformLocation(renderer->aa_program, "u_original");
    renderer->a_text_mask = glGetUniformLocation(renderer->aa_program, "u_text_mask");
    renderer->a_inverse_resolution = glGetUniformLocation(renderer->aa_program,
                                                           "u_inverse_resolution");
    renderer->a_quality = glGetUniformLocation(renderer->aa_program, "u_quality");
    renderer->a_strength = glGetUniformLocation(renderer->aa_program, "u_strength");
    renderer->a_selective = glGetUniformLocation(renderer->aa_program, "u_selective");
    renderer->a_refinement_pass = glGetUniformLocation(renderer->aa_program,
                                                        "u_refinement_pass");

    renderer->p_current = glGetUniformLocation(renderer->persistence_program,
                                                "u_current");
    renderer->p_history = glGetUniformLocation(renderer->persistence_program,
                                                "u_history");
    renderer->p_decay = glGetUniformLocation(renderer->persistence_program,
                                              "u_decay");
    renderer->p_history_valid = glGetUniformLocation(renderer->persistence_program,
                                                      "u_history_valid");
    renderer->p_inverse_resolution = glGetUniformLocation(renderer->persistence_program,
                                                          "u_inverse_resolution");

    const struct {
        const char *name;
        GLint location;
    } required[] = {
        {"u_scene", renderer->u_scene},
        {"u_history", renderer->u_history},
        {"u_inverse_resolution", renderer->u_inverse_resolution},
        {"u_curvature", renderer->u_curvature},
        {"u_glow", renderer->u_glow},
        {"u_vignette", renderer->u_vignette},
        {"u_mask_strength", renderer->u_mask_strength},
        {"u_palette_low", renderer->u_palette_low},
        {"u_palette_mid", renderer->u_palette_mid},
        {"u_palette_high", renderer->u_palette_high},
        {"u_palette_midpoint", renderer->u_palette_midpoint},
        {"u_palette_mix", renderer->u_palette_mix},
        {"u_palette_mode", renderer->u_palette_mode},
        {"u_palette_spatial_mix", renderer->u_palette_spatial_mix},
        {"u_history_enabled", renderer->u_history_enabled},
        {"u_history_strength", renderer->u_history_strength},
        {"composite.u_source", renderer->c_source},
        {"aa.u_source", renderer->a_source},
        {"aa.u_original", renderer->a_original},
        {"aa.u_text_mask", renderer->a_text_mask},
        {"aa.u_inverse_resolution", renderer->a_inverse_resolution},
        {"aa.u_quality", renderer->a_quality},
        {"aa.u_strength", renderer->a_strength},
        {"aa.u_selective", renderer->a_selective},
        {"aa.u_refinement_pass", renderer->a_refinement_pass},
        {"persistence.u_current", renderer->p_current},
        {"persistence.u_history", renderer->p_history},
        {"persistence.u_decay", renderer->p_decay},
        {"persistence.u_history_valid", renderer->p_history_valid},
        {"persistence.u_inverse_resolution", renderer->p_inverse_resolution},
    };

    for (gsize i = 0; i < G_N_ELEMENTS(required); ++i) {
        if (required[i].location >= 0)
            continue;

        if (error_out != NULL)
            *error_out = g_strdup_printf("Required shader uniform '%s' was optimized out or not linked",
                                         required[i].name);
        return FALSE;
    }

    return TRUE;
}

static const char *gl_string(GLenum name)
{
    const GLubyte *value = glGetString(name);
    return value != NULL ? (const char *)value : "unavailable";
}

Renderer *
renderer_create(void)
{
    return g_new0(Renderer, 1);
}

void
renderer_destroy(Renderer *renderer)
{
    if (renderer == NULL)
        return;

    /* GL objects are context-owned and must have been released by
     * renderer_unrealize(). Heap-only members are still safe to release here
     * if realization never completed or teardown was interrupted. */
    g_clear_pointer(&renderer->geometry_vertices, g_array_unref);
    g_clear_pointer(&renderer->vendor_string, g_free);
    g_clear_pointer(&renderer->renderer_string, g_free);
    g_free(renderer);
}

void
renderer_discard_resources(Renderer *renderer)
{
    if (renderer != NULL)
        release_gl_resources(renderer);
}

gboolean
renderer_is_ready(const Renderer *renderer)
{
    return renderer != NULL && renderer->ready;
}

gint
renderer_get_max_texture_size(const Renderer *renderer)
{
    return renderer != NULL ? renderer->max_texture_size : 0;
}

gboolean
renderer_cursor_history_is_valid(const Renderer *renderer)
{
    return renderer != NULL && renderer->cursor_history_valid;
}

void
renderer_invalidate_cursor_history(Renderer *renderer)
{
    if (renderer == NULL)
        return;
    if (renderer->cursor_history_valid)
        renderer->cursor_history_reset_requested = TRUE;
    renderer->cursor_history_valid = FALSE;
}

void
renderer_clear_cursor_history(Renderer *renderer)
{
    if (renderer == NULL)
        return;
    renderer->cursor_history_valid = FALSE;
    renderer->cursor_history_reset_requested = TRUE;
    renderer->cursor_damage_x0 = 0;
    renderer->cursor_damage_y0 = 0;
    renderer->cursor_damage_x1 = 0;
    renderer->cursor_damage_y1 = 0;
}

void renderer_realize(AppState *state)
{
    g_return_if_fail(state != NULL);
    g_return_if_fail(state->renderer != NULL);
    g_return_if_fail(state->shell.gl_area != NULL);
    gtk_gl_area_make_current(state->shell.gl_area);

    const GError *context_error = gtk_gl_area_get_error(state->shell.gl_area);
    if (context_error != NULL) {
        char *message = g_strdup_printf(
            "Unable to create the GL context:\n%s\n\n"
            "Run the application from a terminal and retain this output.",
            context_error->message);
        g_warning("Unable to initialize GtkGLArea: %s", context_error->message);
        app_show_diagnostic(state, message, TRUE);
        g_free(message);
        return;
    }

    Renderer *renderer = state->renderer;
    if (renderer->program != 0 || renderer->vao != 0 ||
        renderer->scene_texture != 0 || renderer->geometry_vertices != NULL) {
        g_warning("Releasing stale GL resources left from an earlier context");
        release_gl_resources(renderer);
    }
    *renderer = (Renderer){0};

#if GTK_CHECK_VERSION(4, 12, 0)
    GdkGLAPI api = gtk_gl_area_get_api(state->shell.gl_area);
    renderer->is_gles = api == GDK_GL_API_GLES;
#else
    renderer->is_gles = gtk_gl_area_get_use_es(state->shell.gl_area);
#endif

    g_print("Graphics API: %s\n",
            renderer->is_gles ? "OpenGL ES" : "desktop OpenGL");
    renderer->vendor_string = g_strdup(gl_string(GL_VENDOR));
    renderer->renderer_string = g_strdup(gl_string(GL_RENDERER));
    g_print("GL vendor: %s\n", renderer->vendor_string);
    g_print("GL renderer: %s\n", renderer->renderer_string);
    g_print("GL version: %s\n", gl_string(GL_VERSION));
    g_print("GLSL version: %s\n", gl_string(GL_SHADING_LANGUAGE_VERSION));
    renderer->nvidia_gpu = g_strrstr(gl_string(GL_VENDOR), "NVIDIA") != NULL ||
                           g_strrstr(gl_string(GL_RENDERER), "NVIDIA") != NULL;
    g_print("GPU tuning profile: %s\n",
            renderer->nvidia_gpu
                ? "NVIDIA RTX-class (selective/Ultra AA and dual half-float targets enabled when supported)"
                : "portable fallback");

    clear_gl_errors();

    char *shader_error = NULL;
    renderer->program = create_crt_program(renderer->is_gles,
                                           &shader_error);
    if (renderer->program == 0) {
        char *message = g_strdup_printf(
            "The GL context was created, but the CRT shader failed.\n\n%s",
            shader_error != NULL ? shader_error : "No shader compiler log was returned.");
        app_show_diagnostic(state, message, TRUE);
        g_free(message);
        g_free(shader_error);
        release_gl_resources(renderer);
        return;
    }
    g_clear_pointer(&shader_error, g_free);

    renderer->persistence_program = create_program(renderer->is_gles,
                                                   RENDERER_SHADER_PERSISTENCE_FRAGMENT,
                                                   &shader_error);
    if (renderer->persistence_program == 0) {
        char *message = g_strdup_printf(
            "The GL context was created, but the persistence shader failed.\n\n%s",
            shader_error != NULL ? shader_error : "No shader compiler log was returned.");
        app_show_diagnostic(state, message, TRUE);
        g_free(message);
        g_free(shader_error);
        release_gl_resources(renderer);
        return;
    }
    g_free(shader_error);
    shader_error = NULL;

    renderer->composite_program = create_program(renderer->is_gles,
                                                 RENDERER_SHADER_COMPOSITE_FRAGMENT,
                                                 &shader_error);
    if (renderer->composite_program == 0) {
        char *message = g_strdup_printf(
            "The GL context was created, but the scene-composite shader failed.\n\n%s",
            shader_error != NULL ? shader_error : "No shader compiler log was returned.");
        app_show_diagnostic(state, message, TRUE);
        g_free(message);
        g_free(shader_error);
        release_gl_resources(renderer);
        return;
    }
    g_free(shader_error);
    shader_error = NULL;

    renderer->aa_program = create_program(renderer->is_gles,
                                          RENDERER_SHADER_ANTI_ALIAS_FRAGMENT,
                                          &shader_error);
    if (renderer->aa_program == 0) {
        char *message = g_strdup_printf(
            "The GL context was created, but the anti-aliasing shader failed.\n\n%s",
            shader_error != NULL ? shader_error : "No shader compiler log was returned.");
        app_show_diagnostic(state, message, TRUE);
        g_free(message);
        g_free(shader_error);
        release_gl_resources(renderer);
        return;
    }
    g_free(shader_error);
    shader_error = NULL;

    renderer->geometry_program = create_geometry_program(renderer->is_gles,
                                                         &shader_error);
    if (renderer->geometry_program == 0) {
        char *message = g_strdup_printf(
            "The GL context was created, but the geometry/cursor shader failed.\n\n%s",
            shader_error != NULL ? shader_error : "No shader compiler log was returned.");
        app_show_diagnostic(state, message, TRUE);
        g_free(message);
        g_free(shader_error);
        release_gl_resources(renderer);
        return;
    }
    g_free(shader_error);

    shader_error = NULL;
    renderer->cursor_aa_program = create_program(renderer->is_gles,
                                                 RENDERER_SHADER_CURSOR_AA_FRAGMENT,
                                                 &shader_error);
    if (renderer->cursor_aa_program == 0) {
        char *message = g_strdup_printf(
            "The GL context was created, but the cursor AA shader failed.\n\n%s",
            shader_error != NULL ? shader_error : "No shader compiler log was returned.");
        app_show_diagnostic(state, message, TRUE);
        g_free(message);
        g_free(shader_error);
        release_gl_resources(renderer);
        return;
    }
    g_free(shader_error);

    shader_error = NULL;
    renderer->cursor_persistence_program = create_program(
        renderer->is_gles, RENDERER_SHADER_CURSOR_PERSISTENCE_FRAGMENT, &shader_error);
    if (renderer->cursor_persistence_program == 0) {
        char *message = g_strdup_printf(
            "The GL context was created, but the cursor persistence shader failed.\n\n%s",
            shader_error != NULL ? shader_error : "No shader compiler log was returned.");
        app_show_diagnostic(state, message, TRUE);
        g_free(message);
        g_free(shader_error);
        release_gl_resources(renderer);
        return;
    }
    g_free(shader_error);

    shader_error = NULL;
    renderer->cursor_phosphor_program = create_program(
        renderer->is_gles, RENDERER_SHADER_CURSOR_PHOSPHOR_FRAGMENT, &shader_error);
    if (renderer->cursor_phosphor_program == 0) {
        char *message = g_strdup_printf(
            "The GL context was created, but the cursor phosphor shader failed.\n\n%s",
            shader_error != NULL ? shader_error : "No shader compiler log was returned.");
        app_show_diagnostic(state, message, TRUE);
        g_free(message);
        g_free(shader_error);
        release_gl_resources(renderer);
        return;
    }
    g_free(shader_error);

    glGenVertexArrays(1, &renderer->vao);
    glGenVertexArrays(1, &renderer->geometry_vao);
    glGenBuffers(1, &renderer->geometry_vbo);
    glGenTextures(1, &renderer->scene_texture);
    glGenTextures(1, &renderer->composed_texture);
    glGenTextures(1, &renderer->aa_texture);
    glGenTextures(1, &renderer->aa_refine_texture);
    glGenTextures(1, &renderer->text_mask_texture);
    glGenTextures(1, &renderer->final_texture);
    glGenTextures(2, renderer->history_textures);
    glGenTextures(1, &renderer->cursor_scene_texture);
    glGenTextures(1, &renderer->cursor_aa_texture);
    glGenTextures(2, renderer->cursor_history_textures);
    glGenFramebuffers(1, &renderer->composed_fbo);
    glGenFramebuffers(1, &renderer->aa_fbo);
    glGenFramebuffers(1, &renderer->aa_refine_fbo);
    glGenFramebuffers(1, &renderer->history_fbo);
    glGenFramebuffers(1, &renderer->final_fbo);
    glGenFramebuffers(1, &renderer->cursor_scene_fbo);
    glGenFramebuffers(1, &renderer->cursor_aa_fbo);
    glGenFramebuffers(1, &renderer->cursor_history_fbo);
    glGenBuffers(CHRONO_UPLOAD_PBO_COUNT, renderer->upload_pbos);
    glGenBuffers(CHRONO_UPLOAD_PBO_COUNT, renderer->text_mask_pbos);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &renderer->max_texture_size);

    if (renderer->vao == 0 || renderer->geometry_vao == 0 ||
        renderer->geometry_vbo == 0 || renderer->scene_texture == 0 ||
        renderer->composed_texture == 0 || renderer->composed_fbo == 0 ||
        renderer->aa_texture == 0 || renderer->aa_refine_texture == 0 ||
        renderer->text_mask_texture == 0 || renderer->final_texture == 0 ||
        renderer->aa_fbo == 0 || renderer->aa_refine_fbo == 0 ||
        renderer->history_textures[0] == 0 || renderer->history_textures[1] == 0 ||
        renderer->history_fbo == 0 || renderer->final_fbo == 0 ||
        renderer->cursor_scene_texture == 0 || renderer->cursor_aa_texture == 0 ||
        renderer->cursor_history_textures[0] == 0 ||
        renderer->cursor_history_textures[1] == 0 ||
        renderer->cursor_scene_fbo == 0 || renderer->cursor_aa_fbo == 0 ||
        renderer->cursor_history_fbo == 0 || renderer->max_texture_size <= 0) {
        app_show_diagnostic(state,
                            "OpenGL did not provide the required shader, framebuffer, or texture resources.",
                            TRUE);
        release_gl_resources(renderer);
        return;
    }

    const GLuint textures[] = {
        renderer->scene_texture,
        renderer->composed_texture,
        renderer->aa_texture,
        renderer->aa_refine_texture,
        renderer->text_mask_texture,
        renderer->final_texture,
        renderer->history_textures[0],
        renderer->history_textures[1],
        renderer->cursor_scene_texture,
        renderer->cursor_aa_texture,
        renderer->cursor_history_textures[0],
        renderer->cursor_history_textures[1],
    };
    /* Shader sampling helpers rely on clamp-to-edge here and intentionally
     * avoid repeating clamp() around every normalized texture lookup.
     * texelFetch() paths still clamp integer coordinates explicitly. */
    for (gsize i = 0; i < G_N_ELEMENTS(textures); ++i) {
        glBindTexture(GL_TEXTURE_2D, textures[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    renderer->cursor_history_reset_requested = TRUE;

    glBindVertexArray(renderer->geometry_vao);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->geometry_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                          sizeof(GeometryVertex),
                          (const void *)offsetof(GeometryVertex, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE,
                          sizeof(GeometryVertex),
                          (const void *)offsetof(GeometryVertex, r));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    renderer->geometry_vertices = g_array_sized_new(FALSE,
                                                     FALSE,
                                                     sizeof(GeometryVertex),
                                                     16384);

    renderer->pbo_upload_enabled = TRUE;
    renderer->text_mask_pbo_upload_enabled = TRUE;
    for (int i = 0; i < CHRONO_UPLOAD_PBO_COUNT; ++i) {
        if (renderer->upload_pbos[i] == 0)
            renderer->pbo_upload_enabled = FALSE;
        if (renderer->text_mask_pbos[i] == 0)
            renderer->text_mask_pbo_upload_enabled = FALSE;
    }
    g_print("Scene upload path: %s\n",
            renderer->pbo_upload_enabled
                ? "triple-buffered pixel unpack buffers"
                : "direct texture upload fallback");
    g_print("Text-mask upload path: %s\n",
            renderer->text_mask_pbo_upload_enabled
                ? "independent triple-buffered R8 pixel unpack buffers"
                : "direct R8 texture upload fallback");

    GLenum gl_error = glGetError();
    if (gl_error != GL_NO_ERROR) {
        char *message = g_strdup_printf(
            "OpenGL resource initialization failed with error 0x%04x.",
            (unsigned int)gl_error);
        g_warning("%s", message);
        app_show_diagnostic(state, message, TRUE);
        g_free(message);
        release_gl_resources(renderer);
        return;
    }

    char *uniform_error = NULL;
    if (!cache_uniform_locations(renderer, &uniform_error)) {
        char *message = g_strdup_printf("The CRT shader linked incompletely.\n\n%s",
                                        uniform_error != NULL ? uniform_error : "Unknown uniform error");
        app_show_diagnostic(state, message, TRUE);
        g_free(message);
        g_free(uniform_error);
        release_gl_resources(renderer);
        return;
    }
    g_free(uniform_error);

    renderer->history_reset_requested = TRUE;
    renderer->ready = TRUE;
    app_show_diagnostic(state, "Graphics initialized. Rendering the first frame…", FALSE);
}

void renderer_unrealize(AppState *state)
{
    g_return_if_fail(state != NULL);
    g_return_if_fail(state->renderer != NULL);
    g_return_if_fail(state->shell.gl_area != NULL);
    gtk_gl_area_make_current(state->shell.gl_area);

    Renderer *renderer = state->renderer;
    if (gtk_gl_area_get_error(state->shell.gl_area) != NULL) {
        g_clear_pointer(&renderer->geometry_vertices, g_array_unref);
        g_clear_pointer(&renderer->vendor_string, g_free);
        g_clear_pointer(&renderer->renderer_string, g_free);
        *renderer = (Renderer){0};
        return;
    }

    release_gl_resources(renderer);
    *renderer = (Renderer){0};
}
