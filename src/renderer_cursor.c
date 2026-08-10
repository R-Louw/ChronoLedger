#include "renderer_internal.h"

#include <float.h>
#include <math.h>

typedef struct {
    int x0;
    int y0;
    int x1;
    int y1;
} CursorDamage;

static double geometry_cursor_duration(float amount)
{
    if (!isfinite(amount) || amount <= 0.001f)
        return 0.0;
    double normalized = CLAMP((double)amount, 0.0, 1.0);
    return 0.15 + pow(normalized, 4.0) * 29.85;
}
static void
append_cursor_geometry(RendererFrameContext *context, gint64 now_us)
{
    /* This geometry, color, fade, and line hierarchy are copied from the
     * calibrated cursor phosphor geometry. The only adapter
     * is coordinate selection: raw window coordinates keep the cursor on its
     * independent post-CRT native-rate plane when curvature is enabled. */
    double duration = geometry_cursor_duration(context->frame->presentation->settings.cursor_persistence);
    unsigned int trail_count = MIN(context->frame->input->cursor_trail_count,
                                   CHRONO_CURSOR_TRAIL_SAMPLES);
    if (duration > 0.0 && trail_count > 1) {
        unsigned int oldest =
            (context->frame->input->cursor_trail_head + CHRONO_CURSOR_TRAIL_SAMPLES -
             trail_count) % CHRONO_CURSOR_TRAIL_SAMPLES;
        for (unsigned int i = 0; i + 1 < trail_count; ++i) {
            const CursorTrailPoint *a =
                &context->frame->input->cursor_trail[(oldest + i) % CHRONO_CURSOR_TRAIL_SAMPLES];
            const CursorTrailPoint *b =
                &context->frame->input->cursor_trail[(oldest + i + 1u) % CHRONO_CURSOR_TRAIL_SAMPLES];
            if (!isfinite(a->x) || !isfinite(a->y) ||
                !isfinite(b->x) || !isfinite(b->y) ||
                a->timestamp_us <= 0 || b->timestamp_us <= 0)
                continue;
            gint64 midpoint_us = a->timestamp_us / 2 + b->timestamp_us / 2 +
                (a->timestamp_us % 2 + b->timestamp_us % 2) / 2;
            if (midpoint_us > now_us)
                continue;
            double age = (double)(now_us - midpoint_us) / G_USEC_PER_SEC;
            if (age < 0.020 || age > duration)
                continue;
            double position = CLAMP(age / MAX(duration, 0.001), 0.0, 1.0);
            float alpha = (float)(pow(1.0 - position,
                                      CHRONO_REFERENCE_CURSOR_FADE_EXPONENT) *
                                  CHRONO_REFERENCE_CURSOR_TRAIL_ALPHA);
            /* Draw the phosphor trace with a dark separator
             * beneath a narrow charged core. Normal alpha blending below
             * makes the dark under-stroke effective. */
            renderer_internal_append_geometry_line(context, a->x, a->y, b->x, b->y,
                                 CHRONO_REFERENCE_CURSOR_TRAIL_SHADOW_WIDTH,
                                 0.0f, 0.0f, 0.0f, alpha * 0.28f);
            renderer_internal_append_geometry_line(context, a->x, a->y, b->x, b->y,
                                 CHRONO_REFERENCE_CURSOR_TRAIL_CORE_WIDTH,
                                 CHRONO_REFERENCE_CURSOR_TRAIL_R,
                                 CHRONO_REFERENCE_CURSOR_TRAIL_G,
                                 CHRONO_REFERENCE_CURSOR_TRAIL_B,
                                 alpha);
        }
    }

    if (!context->frame->input->pointer_inside)
        return;

    const double inner = CHRONO_REFERENCE_CURSOR_CROSS_INNER;
    const double outer = CHRONO_REFERENCE_CURSOR_CROSS_OUTER;
    const double x = context->frame->input->raw_pointer_x;
    const double y = context->frame->input->raw_pointer_y;
    const double segments[4][4] = {
        {x - outer, y, x - inner, y},
        {x + inner, y, x + outer, y},
        {x, y - outer, x, y - inner},
        {x, y + inner, x, y + outer},
    };
    for (int i = 0; i < 4; ++i) {
        renderer_internal_append_geometry_line(context,
                             segments[i][0], segments[i][1],
                             segments[i][2], segments[i][3],
                             CHRONO_REFERENCE_CURSOR_CROSS_SHADOW_WIDTH,
                             0.0f, 0.0f, 0.0f, 0.90f);
        renderer_internal_append_geometry_line(context,
                             segments[i][0], segments[i][1],
                             segments[i][2], segments[i][3],
                             CHRONO_REFERENCE_CURSOR_CROSS_CORE_WIDTH,
                             CHRONO_REFERENCE_CURSOR_CROSS_R,
                             CHRONO_REFERENCE_CURSOR_CROSS_G,
                             CHRONO_REFERENCE_CURSOR_CROSS_B,
                             0.99f);
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
    double duration = geometry_cursor_duration(context->frame->presentation->settings.cursor_persistence);

    unsigned int trail_count = MIN(context->frame->input->cursor_trail_count,
                                   CHRONO_CURSOR_TRAIL_SAMPLES);
    if (duration > 0.0 && trail_count > 1) {
        unsigned int oldest =
            (context->frame->input->cursor_trail_head + CHRONO_CURSOR_TRAIL_SAMPLES -
             trail_count) % CHRONO_CURSOR_TRAIL_SAMPLES;
        for (unsigned int i = 0; i < trail_count; ++i) {
            const CursorTrailPoint *point =
                &context->frame->input->cursor_trail[(oldest + i) % CHRONO_CURSOR_TRAIL_SAMPLES];
            if (!isfinite(point->x) || !isfinite(point->y) ||
                point->timestamp_us <= 0 || point->timestamp_us > now_us)
                continue;
            double age = (double)(now_us - point->timestamp_us) /
                         G_USEC_PER_SEC;
            if (age < 0.0 || age > duration + 0.05)
                continue;
            min_x = MIN(min_x, point->x);
            min_y = MIN(min_y, point->y);
            max_x = MAX(max_x, point->x);
            max_y = MAX(max_y, point->y);
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
    if (!size_changed && !renderer->cursor_history_reset_requested)
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

    /* Keep cursor persistence at full output resolution. The reference
     * cursor's 3 px core and four separated cross arms collapse toward a
     * rounded dot in a half-resolution history buffer. Full-resolution
     * feedback preserves the complete cross silhouette while the scissor
     * rectangle keeps actual fill-rate local to the pointer. */
    int history_width = width;
    int history_height = height;
    for (int i = 0; i < 2; ++i) {
        glBindTexture(GL_TEXTURE_2D, renderer->cursor_history_textures[i]);
        if (size_changed)
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                         history_width, history_height, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glBindFramebuffer(GL_FRAMEBUFFER, renderer->cursor_history_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D,
                               renderer->cursor_history_textures[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            goto failure;
        glViewport(0, 0, history_width, history_height);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)saved_fbo);
    glBindTexture(GL_TEXTURE_2D, (GLuint)saved_texture);
    glViewport(saved_viewport[0], saved_viewport[1],
               saved_viewport[2], saved_viewport[3]);
    if (glGetError() != GL_NO_ERROR)
        return FALSE;
    renderer->cursor_width = width;
    renderer->cursor_height = height;
    renderer->cursor_history_read_index = 0;
    renderer->cursor_history_valid = FALSE;
    renderer->cursor_history_reset_requested = FALSE;
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

    g_array_set_size(renderer->geometry_vertices, 0);
    append_cursor_geometry(context, now_us);
    if (renderer->geometry_vertices->len > 0) {
        GLsizeiptr geometry_bytes = 0;
        GLsizei geometry_count = 0;
        if (!renderer_internal_geometry_upload_arguments(renderer->geometry_vertices,
                                       &geometry_bytes, &geometry_count)) {
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindVertexArray(0);
            glUseProgram(0);
            glDisable(GL_BLEND);
            glDisable(GL_SCISSOR_TEST);
            return FALSE;
        }
        glUseProgram(renderer->geometry_program);
        glBindVertexArray(renderer->geometry_vao);
        glBindBuffer(GL_ARRAY_BUFFER, renderer->geometry_vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     geometry_bytes,
                     renderer->geometry_vertices->data,
                     GL_STREAM_DRAW);
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                            GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glDrawArrays(GL_TRIANGLES, 0, geometry_count);
        glDisable(GL_BLEND);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_SCISSOR_TEST);
    return glGetError() == GL_NO_ERROR;
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
render_cursor_persistence(RendererFrameContext *context,
                          CursorDamage damage,
                          GLuint *history_out,
                          gboolean *history_enabled_out)
{
    Renderer *renderer = context->renderer;
    *history_out = renderer->cursor_aa_texture;
    *history_enabled_out = FALSE;
    float amount = renderer_internal_unit_float_or_zero(context->frame->presentation->settings.persistence);
    if (amount <= 0.0001f) {
        if (renderer->cursor_history_valid)
            renderer->cursor_history_reset_requested = TRUE;
        renderer->cursor_history_valid = FALSE;
        return TRUE;
    }

    renderer_internal_clear_gl_errors();

    int history_width = context->frame->scene->surface_width_px;
    int history_height = context->frame->scene->surface_height_px;
    CursorDamage history_damage = damage;
    int write_index = 1 - renderer->cursor_history_read_index;
    glBindFramebuffer(GL_FRAMEBUFFER, renderer->cursor_history_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D,
                           renderer->cursor_history_textures[write_index], 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return FALSE;
    glViewport(0, 0, history_width, history_height);
    cursor_apply_scissor(history_damage, history_height);
    glDisable(GL_BLEND);

    double delta = CLAMP(context->frame->frame_delta > 0.0 ? context->frame->frame_delta
                                                   : context->frame->render_interval,
                         1.0 / 240.0, 0.1);
    double half_life = 0.025 + (double)amount * (double)amount * 0.62;
    float decay = (float)exp(-0.6931471805599453 * delta / half_life);
    glUseProgram(renderer->cursor_persistence_program);
    glUniform1i(glGetUniformLocation(renderer->cursor_persistence_program,
                                    "u_current"), 0);
    glUniform1i(glGetUniformLocation(renderer->cursor_persistence_program,
                                    "u_history"), 1);
    glUniform1f(glGetUniformLocation(renderer->cursor_persistence_program,
                                    "u_decay"), decay);
    glUniform1f(glGetUniformLocation(renderer->cursor_persistence_program,
                                    "u_history_valid"),
                renderer->cursor_history_valid ? 1.0f : 0.0f);
    glUniform2f(glGetUniformLocation(renderer->cursor_persistence_program,
                                    "u_inverse_resolution"),
                1.0f / (float)history_width,
                1.0f / (float)history_height);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer->cursor_aa_texture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D,
                  renderer->cursor_history_textures[renderer->cursor_history_read_index]);
    glBindVertexArray(renderer->vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    glDisable(GL_SCISSOR_TEST);

    if (glGetError() != GL_NO_ERROR)
        return FALSE;
    renderer->cursor_history_read_index = write_index;
    renderer->cursor_history_valid = TRUE;
    *history_out = renderer->cursor_history_textures[write_index];
    *history_enabled_out = TRUE;
    return TRUE;
}

static gboolean
composite_cursor_phosphor(RendererFrameContext *context,
                          const AppViewSnapshot *view,
                          CursorDamage damage,
                          GLuint current_texture,
                          GLuint history_texture,
                          gboolean history_enabled,
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
    glUniform1i(glGetUniformLocation(renderer->cursor_phosphor_program,
                                    "u_history"), 1);
    glUniform2f(glGetUniformLocation(renderer->cursor_phosphor_program,
                                    "u_inverse_resolution"),
                1.0f / MAX((float)context->frame->scene->surface_width_px, 1.0f),
                1.0f / MAX((float)context->frame->scene->surface_height_px, 1.0f));
    glUniform1f(glGetUniformLocation(renderer->cursor_phosphor_program, "u_glow"),
                effects_enabled ? context->frame->presentation->settings.glow : 0.0f);
    glUniform1f(glGetUniformLocation(renderer->cursor_phosphor_program,
                                    "u_history_strength"),
                effects_enabled && history_enabled
                    ? 0.12f + context->frame->presentation->settings.persistence * 0.42f
                    : 0.0f);
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
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D,
                  history_enabled ? history_texture : current_texture);
    glBindVertexArray(renderer->vao);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    glDisable(GL_SCISSOR_TEST);
    return glGetError() == GL_NO_ERROR;
}

gboolean
renderer_render_cursor_overlay(RendererFrameContext *context,
                      const AppViewSnapshot *view)
{
    Renderer *renderer = context->renderer;
    if (context->frame->presentation->layout.density == UI_DENSITY_TOO_SMALL)
        return TRUE;
    if (!ensure_cursor_targets(renderer,
                               context->frame->scene->surface_width_px,
                               context->frame->scene->surface_height_px))
        return FALSE;

    CursorDamage current = {0};
    gboolean has_current = compute_cursor_damage(
        context, view->monotonic_us, &current);
    gint64 now_us = view->monotonic_us;
    gboolean effects_enabled =
        view->preferences.cursor_shader_effects_enabled;
    if (!effects_enabled) {
        if (renderer->cursor_history_valid)
            renderer->cursor_history_reset_requested = TRUE;
        renderer->cursor_history_valid = FALSE;
    }
    gboolean phosphor_active = effects_enabled &&
        renderer->cursor_history_valid &&
        now_us < context->frame->input->cursor_phosphor_active_until_us;
    CursorDamage accumulated = {
        renderer->cursor_damage_x0,
        renderer->cursor_damage_y0,
        renderer->cursor_damage_x1,
        renderer->cursor_damage_y1,
    };
    if (has_current)
        accumulated = cursor_damage_union(accumulated, current);
    if (!has_current && !phosphor_active) {
        renderer->cursor_damage_x0 = renderer->cursor_damage_y0 = 0;
        renderer->cursor_damage_x1 = renderer->cursor_damage_y1 = 0;
        return TRUE;
    }
    if (!cursor_damage_valid(accumulated))
        return TRUE;

    GLint saved_fbo = 0;
    GLint saved_viewport[4] = {0};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &saved_fbo);
    glGetIntegerv(GL_VIEWPORT, saved_viewport);
    gboolean ok = render_cursor_scene(context, accumulated, now_us);
    GLuint current_texture = renderer->cursor_scene_texture;
    GLuint history = renderer->cursor_scene_texture;
    gboolean history_enabled = FALSE;
    if (ok && effects_enabled) {
        ok = render_cursor_aa(context, accumulated);
        current_texture = renderer->cursor_aa_texture;
        history = renderer->cursor_aa_texture;
    }
    if (ok && effects_enabled)
        ok = render_cursor_persistence(context, accumulated,
                                       &history, &history_enabled);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)saved_fbo);
    glViewport(saved_viewport[0], saved_viewport[1],
               saved_viewport[2], saved_viewport[3]);
    if (ok)
        ok = composite_cursor_phosphor(context, view, accumulated, current_texture,
                                       history, history_enabled,
                                       effects_enabled);
    if (ok) {
        CursorDamage retained_damage = effects_enabled ? accumulated : current;
        renderer->cursor_damage_x0 = retained_damage.x0;
        renderer->cursor_damage_y0 = retained_damage.y0;
        renderer->cursor_damage_x1 = retained_damage.x1;
        renderer->cursor_damage_y1 = retained_damage.y1;
    }
    return ok;
}
