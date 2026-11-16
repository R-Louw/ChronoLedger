#include <epoxy/egl.h>
#include "renderer_internal.h"
#include "renderer_shaders.h"
#include "cursor_state.h"
#include "shader_profile.h"
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <math.h>
static GLuint
program(RendererShaderId v, RendererShaderId f)
{
    GLuint p = glCreateProgram();
    RendererShaderId ids[] = { v, f };
    for (int i = 0; i < 2; i++) {
        char* s = renderer_shader_load_source(ids[i], FALSE, NULL);
        GLuint sh = glCreateShader(i ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER);
        glShaderSource(sh, 1, (const char**)&s, NULL);
        glCompileShader(sh);
        GLint ok;
        glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[4096];
            glGetShaderInfoLog(sh, 4096, NULL, log);
            fprintf(stderr, "%s", log);
            exit(2);
        }
        glAttachShader(p, sh);
        glDeleteShader(sh);
        g_free(s);
    }
    glBindAttribLocation(p, 0, "a_position");
    glBindAttribLocation(p, 1, "a_color");
    glLinkProgram(p);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    g_assert(ok);
    return p;
}
int
main(int argc, char** argv)
{
    g_assert(argc == 5);
    const char* out = argv[1];
    int hz = atoi(argv[2]);
    float persist = atof(argv[3]);
    int effects = atoi(argv[4]);
    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    g_assert(eglInitialize(d, NULL, NULL));
    g_assert(eglBindAPI(EGL_OPENGL_API));
    EGLConfig cfg;
    EGLint n;
    EGLint attrs[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE };
    g_assert(eglChooseConfig(d, attrs, &cfg, 1, &n) && n);
    EGLint ca[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3, EGL_NONE };
    EGLContext ec = eglCreateContext(d, cfg, EGL_NO_CONTEXT, ca);
    g_assert(ec != EGL_NO_CONTEXT);
    g_assert(eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, ec));
    printf("GL: %s / %s\n", glGetString(GL_VENDOR), glGetString(GL_RENDERER));
    Renderer r = { 0 };
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &r.max_texture_size);
    r.geometry_program
        = program(RENDERER_SHADER_GEOMETRY_VERTEX, RENDERER_SHADER_GEOMETRY_FRAGMENT);
    r.cursor_aa_program
        = program(RENDERER_SHADER_FULLSCREEN_VERTEX, RENDERER_SHADER_CURSOR_AA_FRAGMENT);
    r.cursor_phosphor_program
        = program(RENDERER_SHADER_FULLSCREEN_VERTEX, RENDERER_SHADER_CURSOR_PHOSPHOR_FRAGMENT);
    glGenVertexArrays(1, &r.vao);
    glGenVertexArrays(1, &r.geometry_vao);
    glGenBuffers(1, &r.geometry_vbo);
    glBindVertexArray(r.geometry_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r.geometry_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, 0, sizeof(GeometryVertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1, 4, GL_FLOAT, 0, sizeof(GeometryVertex), (void*)offsetof(GeometryVertex, r));
    r.geometry_vertices = g_array_new(FALSE, FALSE, sizeof(GeometryVertex));
    GLuint tex[2];
    glGenTextures(2, tex);
    r.cursor_scene_texture = tex[0];
    r.cursor_aa_texture = tex[1];
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, tex[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glGenFramebuffers(1, &r.cursor_scene_fbo);
    glGenFramebuffers(1, &r.cursor_aa_fbo);
    const int w = 800, h = 360;
    GLuint dest, dt;
    glGenFramebuffers(1, &dest);
    glGenTextures(1, &dt);
    glBindTexture(GL_TEXTURE_2D, dt);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindFramebuffer(GL_FRAMEBUFFER, dest);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dt, 0);
    g_assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    AppPresentationState ps = { 0 };
    shader_settings_reset(&ps.settings);
    ps.settings.persistence = persist;
    AppSceneState sc = { .logical_width = w,
        .logical_height = h,
        .surface_width_px = w,
        .surface_height_px = h,
        .surface_scale = 1 };
    AppInputState in = { .pointer_inside = TRUE };
    AppViewSnapshot view = { 0 };
    view.preferences.cursor_shader_effects_enabled = effects;
    view.preferences.phosphor_mask_enabled = TRUE;
    RendererFrameInput frame = { .cursor_visible = TRUE,
        .presentation = &ps,
        .scene = &sc,
        .input = &in,
        .view = &view,
        .frame_delta = 1.0 / hz,
        .render_interval = 1.0 / hz };
    RendererFrameContext ctx = { &r, &frame };
    FILE* fp = fopen(out, "wb");
    g_assert(fp);
    unsigned char* pixels = malloc(w * h * 4);
    int event = 0;
    for (int k = 0; k <= hz * 8; k++) {
        double t = (double)k / hz;
        view.monotonic_us = 1000000
            + llround(t * 1000000); // Feed the same 720 Hz input stream at every presentation rate.
        while (event <= 864
            && (gint64)1000000 + llround(event * 1000000.0 / 720) <= view.monotonic_us) {
            double e = (double)event / 720, x, y;
            if (e <= .6) {
                x = 100 + 300 * e;
                y = 150;
            } else if (e <= .9) {
                x = 280 + 900 * (e - .6);
                y = 150;
            } else {
                x = 550 - 900 * (e - .9);
                y = 150 + 200 * (e - .9);
            }
            in.raw_pointer_x = x;
            in.raw_pointer_y = y;
            cursor_state_record_motion(
                &in, &ps.settings, &view.preferences, &r, x, y, 1000000 + llround(e * 1000000));
            ++event;
        }
        if (getenv("CURSOR_STUDY_STALL") && t > 1.2 && t < 2.5) {
            for (int row = h - 1; row >= 0; row--)
                fwrite(pixels + row * w * 4, 1, w * 4, fp);
            continue;
        }
        cursor_state_update(&in, &ps.settings, &view.preferences, &r, view.monotonic_us);
        glBindFramebuffer(GL_FRAMEBUFFER, dest);
        glViewport(0, 0, w, h);
        glClearColor(.025, .045, .035, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        g_assert(renderer_render_cursor_overlay(&ctx, &view));
        glBindFramebuffer(GL_FRAMEBUFFER, dest);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        for (int row = h - 1; row >= 0; row--)
            fwrite(pixels + row * w * 4, 1, w * 4, fp);
    }
    fclose(fp);
    g_assert(glGetError() == GL_NO_ERROR);
    return 0;
}
