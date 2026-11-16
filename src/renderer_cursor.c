#include "renderer_internal.h"
#include "cursor_state.h"

#include <float.h>
#include <math.h>

typedef struct { int x0, y0, x1, y1; } CursorDamage;

/* Afterimages contain only the cross's emissive arms. No path stroke or
 * dark separator is deposited in afterglow. GL_MAX resolves overlap without
 * input-rate-dependent accumulation. The live cross retains its separator. */
static void
append_cross(RendererFrameContext *context, double x, double y,
             float emission, gboolean live)
{
    const double inner = CHRONO_REFERENCE_CURSOR_CROSS_INNER;
    const double outer = CHRONO_REFERENCE_CURSOR_CROSS_OUTER;
    const double segments[4][4] = {
        {x - outer, y, x - inner, y},
        {x + inner, y, x + outer, y},
        {x, y - outer, x, y - inner},
        {x, y + inner, x, y + outer},
    };
    for (int i = 0; i < 4; ++i) {
        if (live)
            renderer_internal_append_geometry_line(context,
                segments[i][0], segments[i][1], segments[i][2], segments[i][3],
                CHRONO_REFERENCE_CURSOR_CROSS_SHADOW_WIDTH, 0, 0, 0, 0.90f);
        renderer_internal_append_geometry_line(context,
                             segments[i][0], segments[i][1],
                             segments[i][2], segments[i][3],
                             CHRONO_REFERENCE_CURSOR_CROSS_CORE_WIDTH,
                             CHRONO_REFERENCE_CURSOR_CROSS_R * (live ? 1 : emission),
                             CHRONO_REFERENCE_CURSOR_CROSS_G * (live ? 1 : emission),
                             CHRONO_REFERENCE_CURSOR_CROSS_B * (live ? 1 : emission),
                             live ? 0.99f : emission);
    }
}
static gboolean
cursor_damage_valid(CursorDamage damage)
{
    return damage.x1 > damage.x0 && damage.y1 > damage.y0;
}

static CursorDamage
cursor_damage_union(CursorDamage a, CursorDamage b)
{
    if (!cursor_damage_valid(a))
        return b;
    if (!cursor_damage_valid(b))
        return a;
    return (CursorDamage){
        .x0 = MIN(a.x0, b.x0),
        .y0 = MIN(a.y0, b.y0),
        .x1 = MAX(a.x1, b.x1),
        .y1 = MAX(a.y1, b.y1),
    };
}

static gboolean
compute_cursor_damage(RendererFrameContext *context,
                      gint64 now_us,
                      CursorDamage *damage_out)
{
    double min_x = DBL_MAX;
    double min_y = DBL_MAX;
    double max_x = -DBL_MAX;
    double max_y = -DBL_MAX;
    gboolean any = FALSE;
    double duration = cursor_state_half_life(context->frame->presentation->settings.persistence) * 10;

    unsigned int sample_count = MIN(context->frame->input->cursor_sample_count,
                                   CHRONO_CURSOR_SAMPLES);
    if (duration > 0.0 && context->frame->view->preferences.cursor_shader_effects_enabled && sample_count > 0) {
        unsigned int oldest =
            (context->frame->input->cursor_sample_head + CHRONO_CURSOR_SAMPLES -
             sample_count) % CHRONO_CURSOR_SAMPLES;
        for (unsigned int i = 0; i < sample_count; ++i) {
            const CursorPhosphorPoint *point =
                &context->frame->input->cursor_samples[(oldest + i) % CHRONO_CURSOR_SAMPLES];
            if (!isfinite(point->x) || !isfinite(point->y) ||
                point->timestamp_us <= 0 || point->timestamp_us > now_us)
                continue;
            double age = (double)(now_us - point->timestamp_us) /
                         G_USEC_PER_SEC;
            if (age < 0.0 || age > duration + 0.05)
                continue;
            min_x = MIN(min_x, point->x - CHRONO_REFERENCE_CURSOR_CROSS_OUTER);
            min_y = MIN(min_y, point->y - CHRONO_REFERENCE_CURSOR_CROSS_OUTER);
            max_x = MAX(max_x, point->x + CHRONO_REFERENCE_CURSOR_CROSS_OUTER);
            max_y = MAX(max_y, point->y + CHRONO_REFERENCE_CURSOR_CROSS_OUTER);
            any = TRUE;
        }
    }
    if (context->frame->input->pointer_inside && isfinite(context->frame->input->raw_pointer_x) &&
        isfinite(context->frame->input->raw_pointer_y)) {
        min_x = MIN(min_x, context->frame->input->raw_pointer_x - CHRONO_REFERENCE_CURSOR_CROSS_OUTER);
        min_y = MIN(min_y, context->frame->input->raw_pointer_y - CHRONO_REFERENCE_CURSOR_CROSS_OUTER);
        max_x = MAX(max_x, context->frame->input->raw_pointer_x + CHRONO_REFERENCE_CURSOR_CROSS_OUTER);
        max_y = MAX(max_y, context->frame->input->raw_pointer_y + CHRONO_REFERENCE_CURSOR_CROSS_OUTER);
        any = TRUE;
    }
    if (!any)
        return FALSE;

    if (context->frame->scene->surface_width_px <= 0 || context->frame->scene->surface_height_px <= 0)
        return FALSE;
    int scale = MAX(context->frame->scene->surface_scale, 1);
    int padding = CHRONO_CURSOR_GLOW_PADDING_PX;
    double x_min_bound = -(double)padding / scale;
    double y_min_bound = -(double)padding / scale;
    double x_max_bound = ((double)context->frame->scene->surface_width_px + padding) / scale;
    double y_max_bound = ((double)context->frame->scene->surface_height_px + padding) / scale;
    min_x = CLAMP(min_x, x_min_bound, x_max_bound);
    min_y = CLAMP(min_y, y_min_bound, y_max_bound);
    max_x = CLAMP(max_x, x_min_bound, x_max_bound);
    max_y = CLAMP(max_y, y_min_bound, y_max_bound);
    double x0 = CLAMP(floor(min_x * scale) - padding,
                      0.0, (double)context->frame->scene->surface_width_px);
    double y0 = CLAMP(floor(min_y * scale) - padding,
                      0.0, (double)context->frame->scene->surface_height_px);
    double x1 = CLAMP(ceil(max_x * scale) + padding,
                      0.0, (double)context->frame->scene->surface_width_px);
    double y1 = CLAMP(ceil(max_y * scale) + padding,
                      0.0, (double)context->frame->scene->surface_height_px);
    CursorDamage damage = {
        .x0 = (int)x0,
        .y0 = (int)y0,
        .x1 = (int)x1,
        .y1 = (int)y1,
    };
    if (!cursor_damage_valid(damage))
        return FALSE;
    *damage_out = damage;
    return TRUE;
}

static void
cursor_apply_scissor(CursorDamage damage, int target_height)
{
    glEnable(GL_SCISSOR_TEST);
    glScissor(damage.x0,
              target_height - damage.y1,
              damage.x1 - damage.x0,
              damage.y1 - damage.y0);
}

static gboolean
ensure_cursor_targets(Renderer *renderer, int width, int height)
{
    if (width <= 0 || height <= 0)
        return FALSE;
    if (width > renderer->max_texture_size ||
        height > renderer->max_texture_size)
        return FALSE;
    gboolean size_changed = renderer->cursor_width != width ||
                            renderer->cursor_height != height;
    if (!size_changed)
        return TRUE;

    GLint saved_texture = 0;
    GLint saved_fbo = 0;
    GLint saved_viewport[4] = {0};
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &saved_texture);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &saved_fbo);
    glGetIntegerv(GL_VIEWPORT, saved_viewport);
    renderer_internal_clear_gl_errors();

    const GLuint full_textures[] = {
        renderer->cursor_scene_texture,
        renderer->cursor_aa_texture,
    };
    const GLuint full_fbos[] = {
        renderer->cursor_scene_fbo,
        renderer->cursor_aa_fbo,
    };
    for (guint i = 0; i < G_N_ELEMENTS(full_textures); ++i) {
        glBindTexture(GL_TEXTURE_2D, full_textures[i]);
        if (size_changed)
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glBindFramebuffer(GL_FRAMEBUFFER, full_fbos[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, full_textures[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            goto failure;
        glViewport(0, 0, width, height);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)saved_fbo);
    glBindTexture(GL_TEXTURE_2D, (GLuint)saved_texture);
    glViewport(saved_viewport[0], saved_viewport[1], saved_viewport[2], saved_viewport[3]);
    renderer->cursor_width = width;
    renderer->cursor_height = height;
    renderer->cursor_damage_x0 = renderer->cursor_damage_y0 = 0;
    renderer->cursor_damage_x1 = renderer->cursor_damage_y1 = 0;
    return TRUE;

failure:
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)saved_fbo);
    glBindTexture(GL_TEXTURE_2D, (GLuint)saved_texture);
    glViewport(saved_viewport[0], saved_viewport[1],
               saved_viewport[2], saved_viewport[3]);
    return FALSE;
}

static gboolean
draw_cursor_geometry(RendererFrameContext *context, gboolean emission)
{
    Renderer *r = context->renderer;
    GLsizeiptr bytes;
    GLsizei count;
    if (!renderer_internal_geometry_upload_arguments(r->geometry_vertices, &bytes, &count)) return FALSE;
    glUseProgram(r->geometry_program);
    glBindVertexArray(r->geometry_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->geometry_vbo);
    glBufferData(GL_ARRAY_BUFFER, bytes, r->geometry_vertices->data, GL_STREAM_DRAW);
    glEnable(GL_BLEND);
    glBlendEquation(emission ? GL_MAX : GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, count);
    glBlendEquation(GL_FUNC_ADD);
    glDisable(GL_BLEND);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glUseProgram(0);
    return glGetError() == GL_NO_ERROR;
}

static gboolean
render_cursor_scene(RendererFrameContext *context,
                    CursorDamage damage,
                    gint64 now_us)
{
    Renderer *renderer = context->renderer;
    glBindFramebuffer(GL_FRAMEBUFFER, renderer->cursor_scene_fbo);
    glViewport(0, 0, context->frame->scene->surface_width_px, context->frame->scene->surface_height_px);
    cursor_apply_scissor(damage, context->frame->scene->surface_height_px);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* Rebuild finite emission directly from timestamps: no recursive texture,
     * diffusion, animation-step clamp or repeated history quantization. */
    g_array_set_size(renderer->geometry_vertices, 0);
    const AppInputState *input = context->frame->input;
    if (context->frame->view->preferences.cursor_shader_effects_enabled) {
        guint count = MIN(input->cursor_sample_count, CHRONO_CURSOR_SAMPLES);
        guint oldest = (input->cursor_sample_head + CHRONO_CURSOR_SAMPLES - count) % CHRONO_CURSOR_SAMPLES;
        for (guint i = 0; i < count; ++i) {
            const CursorPhosphorPoint *point = &input->cursor_samples[(oldest + i) % CHRONO_CURSOR_SAMPLES];
            if (point->timestamp_us <= 0 || point->timestamp_us > now_us) continue;
            float emission = cursor_state_emission(context->frame->presentation->settings.persistence,
                                                   now_us - point->timestamp_us);
            if (emission > 0) append_cross(context, point->x, point->y, emission, FALSE);
        }
    }
    gboolean ok = draw_cursor_geometry(context, TRUE);
    g_array_set_size(renderer->geometry_vertices, 0);
    if (input->pointer_inside)
        append_cross(context, input->raw_pointer_x, input->raw_pointer_y, 1, TRUE);
    if (ok) ok = draw_cursor_geometry(context, FALSE);
    glDisable(GL_SCISSOR_TEST);
    return ok;
}

static gboolean
render_cursor_aa(RendererFrameContext *context, CursorDamage damage)
{
    Renderer *renderer = context->renderer;
    glBindFramebuffer(GL_FRAMEBUFFER, renderer->cursor_aa_fbo);
    glViewport(0, 0, context->frame->scene->surface_width_px, context->frame->scene->surface_height_px);
    cursor_apply_scissor(damage, context->frame->scene->surface_height_px);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(renderer->cursor_aa_program);
    glUniform1i(glGetUniformLocation(renderer->cursor_aa_program, "u_source"), 0);
    glUniform2f(glGetUniformLocation(renderer->cursor_aa_program,
                                    "u_inverse_resolution"),
                1.0f / MAX((float)context->frame->scene->surface_width_px, 1.0f),
                1.0f / MAX((float)context->frame->scene->surface_height_px, 1.0f));
    float aa_strength = 0.0f;
    switch (context->frame->presentation->settings.aa_mode) {
    case AA_FXAA:
        aa_strength = 0.48f;
        break;
    case AA_FXAA_HIGH:
        aa_strength = 0.78f;
        break;
    case AA_FXAA_ULTRA:
        aa_strength = 1.0f;
        break;
    case AA_OFF:
    default:
        break;
    }
    glUniform1f(glGetUniformLocation(renderer->cursor_aa_program, "u_strength"),
                aa_strength);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer->cursor_scene_texture);
    glBindVertexArray(renderer->vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    glDisable(GL_SCISSOR_TEST);
    return glGetError() == GL_NO_ERROR;
}

static gboolean
composite_cursor_phosphor(RendererFrameContext *context,
                          const AppViewSnapshot *view,
                          CursorDamage damage,
                          GLuint current_texture,
                          gboolean effects_enabled)
{
    Renderer *renderer = context->renderer;
    GLint destination_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &destination_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)destination_fbo);
    glViewport(0, 0, context->frame->scene->surface_width_px, context->frame->scene->surface_height_px);
    cursor_apply_scissor(damage, context->frame->scene->surface_height_px);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUseProgram(renderer->cursor_phosphor_program);
    glUniform1i(glGetUniformLocation(renderer->cursor_phosphor_program,
                                    "u_current"), 0);
    glUniform2f(glGetUniformLocation(renderer->cursor_phosphor_program,
                                    "u_inverse_resolution"),
                1.0f / MAX((float)context->frame->scene->surface_width_px, 1.0f),
                1.0f / MAX((float)context->frame->scene->surface_height_px, 1.0f));
    glUniform1f(glGetUniformLocation(renderer->cursor_phosphor_program, "u_glow"),
                effects_enabled ? context->frame->presentation->settings.glow : 0.0f);
    glUniform1f(glGetUniformLocation(renderer->cursor_phosphor_program,
                                    "u_mask_strength"),
                effects_enabled && view->preferences.phosphor_mask_enabled
                    ? context->frame->presentation->settings.mask_strength : 0.0f);
    glUniform1f(glGetUniformLocation(renderer->cursor_phosphor_program,
                                    "u_vignette"),
                effects_enabled ? context->frame->presentation->settings.vignette : 0.0f);
    glUniform1f(glGetUniformLocation(renderer->cursor_phosphor_program,
                                    "u_effects_enabled"),
                effects_enabled ? 1.0f : 0.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, current_texture);
    glBindVertexArray(renderer->vao);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    glDisable(GL_SCISSOR_TEST);
    return glGetError() == GL_NO_ERROR;
}

gboolean
renderer_render_cursor_overlay(RendererFrameContext *context, const AppViewSnapshot *view)
{
    Renderer *renderer = context->renderer;
    if (!context->frame->cursor_visible ||
        context->frame->presentation->layout.density == UI_DENSITY_TOO_SMALL)
        return TRUE;
    if (!ensure_cursor_targets(renderer, context->frame->scene->surface_width_px,
                                context->frame->scene->surface_height_px)) return FALSE;
    CursorDamage current = {0};
    gboolean has_current = compute_cursor_damage(context, view->monotonic_us, &current);
    CursorDamage previous = {renderer->cursor_damage_x0, renderer->cursor_damage_y0,
                             renderer->cursor_damage_x1, renderer->cursor_damage_y1};
    CursorDamage damage = cursor_damage_union(previous, current);
    if (!cursor_damage_valid(damage)) return TRUE;
    GLint saved_fbo, viewport[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &saved_fbo);
    glGetIntegerv(GL_VIEWPORT, viewport);
    gboolean ok = render_cursor_scene(context, damage, view->monotonic_us);
    GLuint texture = renderer->cursor_scene_texture;
    gboolean effects = view->preferences.cursor_shader_effects_enabled;
    if (ok && effects) {
        ok = render_cursor_aa(context, damage);
        texture = renderer->cursor_aa_texture;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)saved_fbo);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    if (ok) ok = composite_cursor_phosphor(context, view, damage, texture, effects);
    if (ok) {
        renderer->cursor_damage_x0 = has_current ? current.x0 : 0;
        renderer->cursor_damage_y0 = has_current ? current.y0 : 0;
        renderer->cursor_damage_x1 = has_current ? current.x1 : 0;
        renderer->cursor_damage_y1 = has_current ? current.y1 : 0;
    }
    return ok;
}
