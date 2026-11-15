#include "renderer_internal.h"
#include "render_policy.h"

RendererDrawStatus
renderer_draw(Renderer *renderer,
              const RendererFrameInput *frame,
              RendererFrameResult *result,
              unsigned int *gl_error_out)
{
    if (result != NULL)
        *result = (RendererFrameResult){0};
    if (gl_error_out != NULL)
        *gl_error_out = 0;
    g_return_val_if_fail(renderer != NULL, RENDERER_DRAW_GL_ERROR);
    g_return_val_if_fail(frame != NULL, RENDERER_DRAW_GL_ERROR);
    g_return_val_if_fail(frame->presentation != NULL, RENDERER_DRAW_GL_ERROR);
    g_return_val_if_fail(frame->scene != NULL, RENDERER_DRAW_GL_ERROR);
    g_return_val_if_fail(frame->input != NULL, RENDERER_DRAW_GL_ERROR);
    g_return_val_if_fail(frame->view != NULL, RENDERER_DRAW_GL_ERROR);
    if (!renderer->ready)
        return RENDERER_DRAW_OK;
    if (frame->scene->logical_width <= 0 || frame->scene->logical_height <= 0)
        return RENDERER_DRAW_OK;

    renderer_advance_scene_history(renderer, frame->view->monotonic_us);

    RendererFrameContext context = {
        .renderer = renderer,
        .frame = frame,
    };
    renderer_internal_clear_gl_errors();

    if (!renderer_ensure_aa_targets(renderer,
                           frame->scene->surface_width_px,
                           frame->scene->surface_height_px))
        return RENDERER_DRAW_TARGET_ALLOCATION_FAILED;

    if (frame->scene_needs_upload) {
        if (!renderer_upload_scene_texture(&context))
            return RENDERER_DRAW_SCENE_UPLOAD_FAILED;
        if (frame->presentation->settings.aa_mode >= AA_FXAA_HIGH &&
            !renderer_upload_text_mask_texture(&context))
            return RENDERER_DRAW_TEXT_MASK_UPLOAD_FAILED;
        if (result != NULL)
            result->scene_uploaded = TRUE;
    }

    gboolean rebuild_filtered_scene = !renderer->filtered_scene_valid;
    if (rebuild_filtered_scene) {
        if (!renderer_render_composed_scene(&context, frame->view))
            return RENDERER_DRAW_COMPOSITION_FAILED;

        GLuint filtered_scene_texture = renderer->composed_texture;
        if (!renderer_render_anti_aliasing_pass(&context, &filtered_scene_texture))
            return RENDERER_DRAW_ANTI_ALIASING_FAILED;
        renderer->filtered_scene_texture = filtered_scene_texture;
        renderer->filtered_scene_valid = TRUE;
        renderer->final_scene_valid = FALSE;
        if (renderer->history_valid) {
            renderer->scene_history_deadline_us = chrono_scene_history_deadline(
                frame->presentation->settings.persistence, frame->view->monotonic_us);
        }
    }

    GLuint filtered_scene_texture = renderer->filtered_scene_texture;
    gboolean update_crt_base = !renderer->final_scene_valid;
    if (update_crt_base) {
        gint64 now_us = frame->view->monotonic_us;
        if (renderer->last_crt_update_us > 0) {
            renderer->crt_frame_delta =
                (double)renderer_internal_nonnegative_elapsed_us(
                    now_us, renderer->last_crt_update_us) / G_USEC_PER_SEC;
        } else {
            double native_hz = frame->monitor_refresh_hz > 1.0
                ? frame->monitor_refresh_hz
                : frame->frame_clock_fps > 1.0
                    ? frame->frame_clock_fps
                    : 75.0;
            renderer->crt_frame_delta = frame->frame_delta > 0.0
                ? frame->frame_delta
                : 1.0 / native_hz;
        }
        renderer->last_crt_update_us = now_us;

        GLuint history_texture = 0;
        gboolean history_enabled = FALSE;
        if (!renderer_render_persistence_pass(&context,
                                     filtered_scene_texture,
                                     &history_texture,
                                     &history_enabled))
            return RENDERER_DRAW_PERSISTENCE_FAILED;

        if (!renderer_render_final_crt_cache(&context, frame->view,
                                    filtered_scene_texture,
                                    history_texture,
                                    history_enabled))
            return RENDERER_DRAW_FINAL_CACHE_FAILED;
    }

    if (!renderer_present_final_cache(&context))
        return RENDERER_DRAW_PRESENT_FAILED;
    if (!renderer_render_cursor_overlay(&context, frame->view))
        return RENDERER_DRAW_CURSOR_FAILED;

    GLenum gl_error = glGetError();
    if (gl_error != GL_NO_ERROR) {
        if (gl_error_out != NULL)
            *gl_error_out = (unsigned int)gl_error;
        return RENDERER_DRAW_GL_ERROR;
    }
    return RENDERER_DRAW_OK;
}

const char *
renderer_draw_status_message(RendererDrawStatus status)
{
    switch (status) {
    case RENDERER_DRAW_OK:
        return NULL;
    case RENDERER_DRAW_TARGET_ALLOCATION_FAILED:
        return "The anti-aliasing targets or text mask could not be allocated.";
    case RENDERER_DRAW_SCENE_UPLOAD_FAILED:
        return "The CPU-rendered scene could not be uploaded to OpenGL.";
    case RENDERER_DRAW_TEXT_MASK_UPLOAD_FAILED:
        return "The selective-AA text coverage mask could not be uploaded.";
    case RENDERER_DRAW_COMPOSITION_FAILED:
        return "The instrument surface and progress geometry could not be composed.";
    case RENDERER_DRAW_ANTI_ALIASING_FAILED:
        return "The pre-CRT anti-aliasing framebuffer could not be rendered.";
    case RENDERER_DRAW_PERSISTENCE_FAILED:
        return "The phosphor persistence framebuffer could not be rendered.";
    case RENDERER_DRAW_FINAL_CACHE_FAILED:
        return "The cached CRT presentation surface could not be rendered.";
    case RENDERER_DRAW_PRESENT_FAILED:
        return "The cached CRT presentation surface could not be presented.";
    case RENDERER_DRAW_CURSOR_FAILED:
        return "The independent cursor AA/phosphor pipeline could not be rendered.";
    case RENDERER_DRAW_GL_ERROR:
        return "Rendering failed with an OpenGL error.";
    }
    return "Rendering failed.";
}
