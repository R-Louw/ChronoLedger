#include "renderer_internal.h"
#include "ui_palette.h"
#include "render_policy.h"

#include <math.h>

static gboolean allocate_aa_target_pair(Renderer *renderer,
                                          int width,
                                          int height,
                                          GLint internal_format,
                                          GLenum data_type)
{
    const GLuint textures[2] = {
        renderer->aa_texture,
        renderer->aa_refine_texture,
    };
    const GLuint framebuffers[2] = {
        renderer->aa_fbo,
        renderer->aa_refine_fbo,
    };

    for (int i = 0; i < 2; ++i) {
        glBindTexture(GL_TEXTURE_2D, textures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0,
                     GL_RGBA, data_type, NULL);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, textures[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
                GL_FRAMEBUFFER_COMPLETE ||
            glGetError() != GL_NO_ERROR)
            return FALSE;
    }
    return TRUE;
}

gboolean renderer_ensure_aa_targets(Renderer *renderer, int width, int height)
{
    if (width <= 0 || height <= 0 ||
        width > renderer->max_texture_size ||
        height > renderer->max_texture_size)
        return FALSE;
    if (renderer->aa_width == width && renderer->aa_height == height &&
        renderer->text_mask_width == width &&
        renderer->text_mask_height == height)
        return TRUE;

    GLint saved_texture = 0;
    GLint saved_fbo = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &saved_texture);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &saved_fbo);

    gboolean complete = FALSE;
    gboolean high_precision = FALSE;
    if (renderer->nvidia_gpu) {
        renderer_internal_clear_gl_errors();
        complete = allocate_aa_target_pair(renderer, width, height,
                                           GL_RGBA16F, GL_HALF_FLOAT);
        high_precision = complete;
    }

    if (!complete) {
        renderer_internal_clear_gl_errors();
        complete = allocate_aa_target_pair(renderer, width, height,
                                           GL_RGBA8, GL_UNSIGNED_BYTE);
        high_precision = FALSE;
    }

    if (complete) {
        renderer_internal_clear_gl_errors();
        glBindTexture(GL_TEXTURE_2D, renderer->text_mask_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0,
                     GL_RED, GL_UNSIGNED_BYTE, NULL);
        complete = glGetError() == GL_NO_ERROR;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)saved_fbo);
    glBindTexture(GL_TEXTURE_2D, (GLuint)saved_texture);
    if (!complete)
        return FALSE;

    gboolean first_allocation = renderer->aa_width == 0 ||
                                renderer->aa_height == 0;
    renderer->aa_width = width;
    renderer->aa_height = height;
    renderer->text_mask_width = width;
    renderer->text_mask_height = height;
    renderer->filtered_scene_valid = FALSE;
    renderer->final_scene_valid = FALSE;
    renderer->history_reset_requested = TRUE;
    renderer->history_valid = FALSE;
    renderer->scene_history_deadline_us = 0;
    if (first_allocation) {
        g_print("Anti-aliasing targets: two full-resolution %s textures "
                "plus one R8 text mask "
                "(selective HQ plus Ultra CMAA topology ping-pong, %s path)\n",
                high_precision ? "RGBA16F" : "RGBA8",
                renderer->nvidia_gpu ? "NVIDIA" : "portable");
    }
    return TRUE;
}

static void draw_aa_resolve(Renderer *renderer,
                            GLuint target_fbo,
                            GLuint source_texture,
                            GLuint original_texture,
                            int width,
                            int height,
                            int quality,
                            float strength,
                            gboolean selective,
                            gboolean refinement_pass)
{
    glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
    glViewport(0, 0, width, height);
    glUseProgram(renderer->aa_program);
    glUniform1i(renderer->a_source, 0);
    glUniform1i(renderer->a_original, 1);
    glUniform1i(renderer->a_text_mask, 2);
    glUniform2f(renderer->a_inverse_resolution,
                1.0f / MAX((float)width, 1.0f),
                1.0f / MAX((float)height, 1.0f));
    glUniform1i(renderer->a_quality, quality);
    glUniform1f(renderer->a_strength, strength);
    glUniform1i(renderer->a_selective, selective ? 1 : 0);
    glUniform1i(renderer->a_refinement_pass, refinement_pass ? 1 : 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source_texture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, original_texture);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, renderer->text_mask_texture);
    glBindVertexArray(renderer->vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

gboolean
renderer_render_anti_aliasing_pass(RendererFrameContext *context,
                                          GLuint *scene_texture_out)
{
    if (scene_texture_out == NULL)
        return FALSE;
    Renderer *renderer = context->renderer;
    GLuint result_texture = renderer->composed_texture;
    AntiAliasingMode mode = context->frame->presentation->settings.aa_mode;
    if (mode < AA_OFF || mode >= CHRONO_AA_MODE_COUNT)
        mode = AA_OFF;
    if (mode == AA_OFF) {
        *scene_texture_out = result_texture;
        return TRUE;
    }

    if (!renderer_ensure_aa_targets(renderer,
                           context->frame->scene->surface_width_px,
                           context->frame->scene->surface_height_px))
        return FALSE;

    GLint saved_fbo = 0;
    GLint saved_viewport[4] = {0};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &saved_fbo);
    glGetIntegerv(GL_VIEWPORT, saved_viewport);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    gboolean selective = mode >= AA_FXAA_HIGH;

    if (mode == AA_FXAA_ULTRA) {
        /* Ultra uses a conservative morphological topology resolve instead of
         * applying FXAA twice.  The first pass classifies only one-pixel
         * staircase corners; the second pass blends those corner pixels by a
         * bounded amount.  Straight edges and text are left untouched. */
        draw_aa_resolve(renderer,
                        renderer->aa_fbo,
                        renderer->composed_texture,
                        renderer->composed_texture,
                        context->frame->scene->surface_width_px,
                        context->frame->scene->surface_height_px,
                        4,
                        1.0f,
                        TRUE,
                        FALSE);
        draw_aa_resolve(renderer,
                        renderer->aa_refine_fbo,
                        renderer->aa_texture,
                        renderer->composed_texture,
                        context->frame->scene->surface_width_px,
                        context->frame->scene->surface_height_px,
                        4,
                        0.92f,
                        TRUE,
                        TRUE);
        result_texture = renderer->aa_refine_texture;
    } else {
        int quality = mode == AA_FXAA ? 1 : 2;
        float strength = mode == AA_FXAA ? 0.62f : 0.94f;
        draw_aa_resolve(renderer,
                        renderer->aa_fbo,
                        renderer->composed_texture,
                        renderer->composed_texture,
                        context->frame->scene->surface_width_px,
                        context->frame->scene->surface_height_px,
                        quality,
                        strength,
                        selective,
                        FALSE);
        result_texture = renderer->aa_texture;
    }

    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)saved_fbo);
    glViewport(saved_viewport[0], saved_viewport[1],
               saved_viewport[2], saved_viewport[3]);

    if (glGetError() != GL_NO_ERROR)
        return FALSE;
    *scene_texture_out = result_texture;
    return TRUE;
}

void
renderer_reset_frame_timing(Renderer *renderer)
{
    g_return_if_fail(renderer != NULL);
    renderer->last_crt_update_us = 0;
    renderer->crt_frame_delta = 0.0;
}

void
renderer_request_history_reset(Renderer *renderer)
{
    g_return_if_fail(renderer != NULL);
    renderer->history_reset_requested = TRUE;
    renderer->history_valid = FALSE;
    renderer->scene_history_deadline_us = 0;
    renderer_invalidate_final(renderer);
}

void
renderer_advance_scene_history(Renderer *renderer, gint64 now_us)
{
    g_return_if_fail(renderer != NULL);
    switch (chrono_scene_history_step(&renderer->scene_history_deadline_us,
                                      renderer->last_crt_update_us, now_us)) {
    case CHRONO_SCENE_HISTORY_DECAY:
        renderer_invalidate_final(renderer);
        break;
    case CHRONO_SCENE_HISTORY_CLEANUP:
        renderer_request_history_reset(renderer);
        break;
    case CHRONO_SCENE_HISTORY_IDLE:
        break;
    }
}

void
renderer_invalidate_filtered(Renderer *renderer)
{
    g_return_if_fail(renderer != NULL);
    renderer->filtered_scene_valid = FALSE;
    renderer->final_scene_valid = FALSE;
}

void
renderer_invalidate_final(Renderer *renderer)
{
    g_return_if_fail(renderer != NULL);
    renderer->final_scene_valid = FALSE;
}

gboolean
renderer_filtered_scene_is_valid(const Renderer *renderer)
{
    return renderer != NULL && renderer->filtered_scene_valid;
}

gboolean
renderer_output_is_valid(const Renderer *renderer)
{
    return renderer != NULL && renderer->final_scene_valid;
}

gboolean
renderer_frame_cache_is_valid(const Renderer *renderer)
{
    return renderer_filtered_scene_is_valid(renderer) &&
           renderer_output_is_valid(renderer);
}

gboolean
renderer_needs_render(const Renderer *renderer)
{
    return !renderer_frame_cache_is_valid(renderer);
}

static gboolean ensure_history_targets(Renderer *renderer, int width, int height)
{
    if (width <= 0 || height <= 0 ||
        width > renderer->max_texture_size ||
        height > renderer->max_texture_size)
        return FALSE;

    gboolean size_changed = renderer->history_width != width ||
                            renderer->history_height != height;
    if (!size_changed && !renderer->history_reset_requested)
        return TRUE;

    GLint saved_texture = 0;
    GLint saved_fbo = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &saved_texture);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &saved_fbo);
    renderer_internal_clear_gl_errors();

    for (int i = 0; i < 2; ++i) {
        glBindTexture(GL_TEXTURE_2D, renderer->history_textures[i]);
        if (size_changed) {
            glTexImage2D(GL_TEXTURE_2D,
                         0,
                         GL_RGBA8,
                         width,
                         height,
                         0,
                         GL_RGBA,
                         GL_UNSIGNED_BYTE,
                         NULL);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, renderer->history_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,
                               GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D,
                               renderer->history_textures[i],
                               0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)saved_fbo);
            glBindTexture(GL_TEXTURE_2D, (GLuint)saved_texture);
            return FALSE;
        }

        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)saved_fbo);
    glBindTexture(GL_TEXTURE_2D, (GLuint)saved_texture);

    if (glGetError() != GL_NO_ERROR)
        return FALSE;
    renderer->history_width = width;
    renderer->history_height = height;
    renderer->history_read_index = 0;
    renderer->history_valid = FALSE;
    renderer->history_reset_requested = FALSE;
    return TRUE;
}

gboolean
renderer_render_persistence_pass(RendererFrameContext *context,
                                        GLuint current_scene_texture,
                                        GLuint *history_texture_out,
                                        gboolean *history_enabled_out)
{
    Renderer *renderer = context->renderer;
    float amount = renderer_internal_unit_float_or_zero(context->frame->presentation->settings.persistence);
    *history_texture_out = 0;
    *history_enabled_out = FALSE;

    if (amount <= 0.0001f) {
        renderer->history_valid = FALSE;
        renderer->history_reset_requested = TRUE;
        renderer->scene_history_deadline_us = 0;
        return TRUE;
    }

    renderer_internal_clear_gl_errors();

    /* Persistence is intentionally stored at half resolution. Trails are soft
     * by nature, while keeping the current scene at full resolution preserves
     * text sharpness and cuts persistence fill-rate and memory by roughly 75%. */
    int history_width = MAX(context->frame->scene->surface_width_px / 2 +
                            context->frame->scene->surface_width_px % 2, 1);
    int history_height = MAX(context->frame->scene->surface_height_px / 2 +
                             context->frame->scene->surface_height_px % 2, 1);
    if (!ensure_history_targets(renderer, history_width, history_height))
        return FALSE;

    GLint saved_fbo = 0;
    GLint saved_viewport[4] = {0};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &saved_fbo);
    glGetIntegerv(GL_VIEWPORT, saved_viewport);

    int write_index = 1 - renderer->history_read_index;
    glBindFramebuffer(GL_FRAMEBUFFER, renderer->history_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,
                           GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D,
                           renderer->history_textures[write_index],
                           0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)saved_fbo);
        glViewport(saved_viewport[0], saved_viewport[1],
                   saved_viewport[2], saved_viewport[3]);
        return FALSE;
    }

    glViewport(0, 0, history_width, history_height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    float decay = chrono_scene_persistence_decay(amount, renderer->crt_frame_delta);

    glUseProgram(renderer->persistence_program);
    glUniform1i(renderer->p_current, 0);
    glUniform1i(renderer->p_history, 1);
    glUniform1f(renderer->p_decay, decay);
    glUniform1f(renderer->p_history_valid,
                renderer->history_valid ? 1.0f : 0.0f);
    /* Supply reciprocal dimensions once per draw instead of computing a
     * textureSize() reciprocal independently in every history fragment. */
    glUniform2f(renderer->p_inverse_resolution,
                1.0f / (float)history_width,
                1.0f / (float)history_height);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, current_scene_texture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D,
                  renderer->history_textures[renderer->history_read_index]);
    glBindVertexArray(renderer->vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)saved_fbo);
    glViewport(saved_viewport[0], saved_viewport[1],
               saved_viewport[2], saved_viewport[3]);

    if (glGetError() != GL_NO_ERROR)
        return FALSE;
    renderer->history_read_index = write_index;
    renderer->history_valid = TRUE;
    *history_texture_out = renderer->history_textures[write_index];
    *history_enabled_out = TRUE;
    return TRUE;
}


static gboolean
draw_final_crt_pass(RendererFrameContext *context,
                    const AppViewSnapshot *view,
                    GLuint filtered_scene_texture,
                    GLuint history_texture,
                    gboolean history_enabled)
{
    glViewport(0, 0, context->frame->scene->surface_width_px, context->frame->scene->surface_height_px);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    Renderer *renderer = context->renderer;
    const ShaderSettings *settings = &context->frame->presentation->settings;
    const PaletteParameters *palette = palette_parameters(settings->palette);

    glUseProgram(renderer->program);
    glUniform1i(renderer->u_scene, 0);
    glUniform1i(renderer->u_history, 1);
    glUniform2f(renderer->u_inverse_resolution,
                1.0f / MAX((float)context->frame->scene->surface_width_px, 1.0f),
                1.0f / MAX((float)context->frame->scene->surface_height_px, 1.0f));
    glUniform1f(renderer->u_curvature,
                view->preferences.curvature_enabled
                    ? settings->curvature_strength * CHRONO_CURVATURE_MAX : 0.0f);
    glUniform1f(renderer->u_glow, settings->glow);
    glUniform1f(renderer->u_vignette, settings->vignette);
    glUniform1f(renderer->u_mask_strength,
                view->preferences.phosphor_mask_enabled
                    ? settings->mask_strength : 0.0f);
    glUniform3fv(renderer->u_palette_low, 1, palette->low);
    glUniform3fv(renderer->u_palette_mid, 1, palette->mid);
    glUniform3fv(renderer->u_palette_high, 1, palette->high);
    glUniform1f(renderer->u_palette_midpoint, palette->midpoint);
    glUniform1f(renderer->u_palette_mix, palette->source_mix);
    glUniform1i(renderer->u_palette_mode, palette->mode);
    glUniform1f(renderer->u_palette_spatial_mix, palette->spatial_mix);
    glUniform1f(renderer->u_history_enabled, history_enabled ? 1.0f : 0.0f);
    glUniform1f(renderer->u_history_strength,
                history_enabled ? 0.12f + settings->persistence * 0.42f : 0.0f);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, filtered_scene_texture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D,
                  history_enabled ? history_texture : filtered_scene_texture);
    glBindVertexArray(renderer->vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);

    return glGetError() == GL_NO_ERROR;
}

static gboolean
ensure_final_target(Renderer *renderer, int width, int height)
{
    if (width <= 0 || height <= 0 ||
        width > renderer->max_texture_size ||
        height > renderer->max_texture_size)
        return FALSE;
    if (renderer->final_width == width && renderer->final_height == height)
        return TRUE;

    GLint saved_texture = 0;
    GLint saved_fbo = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &saved_texture);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &saved_fbo);

    renderer_internal_clear_gl_errors();
    glBindTexture(GL_TEXTURE_2D, renderer->final_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindFramebuffer(GL_FRAMEBUFFER, renderer->final_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, renderer->final_texture, 0);
    gboolean complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
                        GL_FRAMEBUFFER_COMPLETE;

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)saved_fbo);
    glBindTexture(GL_TEXTURE_2D, (GLuint)saved_texture);
    if (!complete || glGetError() != GL_NO_ERROR)
        return FALSE;

    renderer->final_width = width;
    renderer->final_height = height;
    renderer->final_scene_valid = FALSE;
    return TRUE;
}

gboolean
renderer_render_final_crt_cache(RendererFrameContext *context,
                       const AppViewSnapshot *view,
                       GLuint filtered_scene_texture,
                       GLuint history_texture,
                       gboolean history_enabled)
{
    Renderer *renderer = context->renderer;
    if (!ensure_final_target(renderer,
                             context->frame->scene->surface_width_px,
                             context->frame->scene->surface_height_px))
        return FALSE;

    GLint saved_fbo = 0;
    GLint saved_viewport[4] = {0};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &saved_fbo);
    glGetIntegerv(GL_VIEWPORT, saved_viewport);
    glBindFramebuffer(GL_FRAMEBUFFER, renderer->final_fbo);
    gboolean ok = draw_final_crt_pass(context, view,
                                      filtered_scene_texture,
                                      history_texture,
                                      history_enabled);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)saved_fbo);
    glViewport(saved_viewport[0], saved_viewport[1],
               saved_viewport[2], saved_viewport[3]);
    if (ok)
        renderer->final_scene_valid = TRUE;
    return ok;
}

gboolean
renderer_present_final_cache(RendererFrameContext *context)
{
    Renderer *renderer = context->renderer;
    if (!renderer->final_scene_valid)
        return FALSE;

    GLint destination_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &destination_fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, renderer->final_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)destination_fbo);
    glBlitFramebuffer(0, 0, renderer->final_width, renderer->final_height,
                      0, 0, context->frame->scene->surface_width_px, context->frame->scene->surface_height_px,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)destination_fbo);
    glViewport(0, 0, context->frame->scene->surface_width_px, context->frame->scene->surface_height_px);
    return glGetError() == GL_NO_ERROR;
}
