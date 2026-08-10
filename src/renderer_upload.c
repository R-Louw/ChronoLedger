#include "renderer_internal.h"

#include <string.h>

gboolean
renderer_upload_scene_texture(RendererFrameContext *context)
{
    Renderer *renderer = context->renderer;
    cairo_surface_flush(context->frame->scene->scene_surface);

    unsigned char *data = cairo_image_surface_get_data(context->frame->scene->scene_surface);
    int stride = cairo_image_surface_get_stride(context->frame->scene->scene_surface);
    if (data == NULL || stride <= 0 || stride % 4 != 0 ||
        context->frame->scene->surface_width_px <= 0 || context->frame->scene->surface_height_px <= 0 ||
        (gsize)context->frame->scene->surface_height_px > G_MAXSIZE / (gsize)stride)
        return FALSE;

    gsize byte_count = (gsize)stride * (gsize)context->frame->scene->surface_height_px;
    GLsizeiptr gl_byte_count = 0;
    if (!renderer_internal_size_to_gl_sizeiptr(byte_count, &gl_byte_count))
        return FALSE;
    const void *upload_pointer = data;
    gboolean used_pbo = FALSE;
    renderer_internal_clear_gl_errors();

    if (renderer->pbo_upload_enabled) {
        renderer->upload_pbo_index %= CHRONO_UPLOAD_PBO_COUNT;
        GLuint pbo = renderer->upload_pbos[renderer->upload_pbo_index];
        renderer->upload_pbo_index =
            (renderer->upload_pbo_index + 1u) % CHRONO_UPLOAD_PBO_COUNT;

        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
        glBufferData(GL_PIXEL_UNPACK_BUFFER,
                     gl_byte_count,
                     NULL,
                     GL_STREAM_DRAW);
        void *mapped = glMapBufferRange(GL_PIXEL_UNPACK_BUFFER,
                                        0,
                                        gl_byte_count,
                                        GL_MAP_WRITE_BIT |
                                        GL_MAP_INVALIDATE_BUFFER_BIT);
        if (mapped != NULL) {
            memcpy(mapped, data, byte_count);
            if (glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER) == GL_TRUE) {
                upload_pointer = (const void *)0;
                used_pbo = TRUE;
            }
        }

        if (!used_pbo) {
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            renderer_internal_clear_gl_errors();
            renderer->pbo_upload_enabled = FALSE;
            g_warning("PBO mapping failed; falling back to direct scene uploads");
        }
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer->scene_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / 4);

    gboolean size_changed =
        renderer->texture_width != context->frame->scene->surface_width_px ||
        renderer->texture_height != context->frame->scene->surface_height_px;
    if (size_changed) {
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGBA8,
                     context->frame->scene->surface_width_px,
                     context->frame->scene->surface_height_px,
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     upload_pointer);
    } else {
        glTexSubImage2D(GL_TEXTURE_2D,
                        0,
                        0,
                        0,
                        context->frame->scene->surface_width_px,
                        context->frame->scene->surface_height_px,
                        GL_RGBA,
                        GL_UNSIGNED_BYTE,
                        upload_pointer);
    }

    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (used_pbo)
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    if (glGetError() != GL_NO_ERROR)
        return FALSE;
    if (size_changed) {
        renderer->texture_width = context->frame->scene->surface_width_px;
        renderer->texture_height = context->frame->scene->surface_height_px;
    }
    return TRUE;
}

gboolean
renderer_upload_text_mask_texture(RendererFrameContext *context)
{
    Renderer *renderer = context->renderer;
    cairo_surface_flush(context->frame->scene->text_mask_surface);

    unsigned char *data = cairo_image_surface_get_data(context->frame->scene->text_mask_surface);
    int stride = cairo_image_surface_get_stride(context->frame->scene->text_mask_surface);
    if (data == NULL || stride <= 0 || context->frame->scene->surface_width_px <= 0 ||
        context->frame->scene->surface_height_px <= 0 ||
        (gsize)context->frame->scene->surface_height_px > G_MAXSIZE / (gsize)stride)
        return FALSE;

    gsize byte_count = (gsize)stride * (gsize)context->frame->scene->surface_height_px;
    GLsizeiptr gl_byte_count = 0;
    if (!renderer_internal_size_to_gl_sizeiptr(byte_count, &gl_byte_count))
        return FALSE;
    const void *upload_pointer = data;
    gboolean used_pbo = FALSE;
    renderer_internal_clear_gl_errors();

    if (renderer->text_mask_pbo_upload_enabled) {
        renderer->text_mask_pbo_index %= CHRONO_UPLOAD_PBO_COUNT;
        GLuint pbo = renderer->text_mask_pbos[renderer->text_mask_pbo_index];
        renderer->text_mask_pbo_index =
            (renderer->text_mask_pbo_index + 1u) % CHRONO_UPLOAD_PBO_COUNT;

        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
        glBufferData(GL_PIXEL_UNPACK_BUFFER,
                     gl_byte_count,
                     NULL,
                     GL_STREAM_DRAW);
        void *mapped = glMapBufferRange(GL_PIXEL_UNPACK_BUFFER,
                                        0,
                                        gl_byte_count,
                                        GL_MAP_WRITE_BIT |
                                        GL_MAP_INVALIDATE_BUFFER_BIT);
        if (mapped != NULL) {
            memcpy(mapped, data, byte_count);
            if (glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER) == GL_TRUE) {
                upload_pointer = (const void *)0;
                used_pbo = TRUE;
            }
        }

        if (!used_pbo) {
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            renderer_internal_clear_gl_errors();
            renderer->text_mask_pbo_upload_enabled = FALSE;
            g_warning("Text-mask PBO mapping failed; falling back to direct R8 uploads");
        }
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer->text_mask_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, stride);

    gboolean size_changed =
        renderer->text_mask_width != context->frame->scene->surface_width_px ||
        renderer->text_mask_height != context->frame->scene->surface_height_px;
    if (size_changed) {
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_R8,
                     context->frame->scene->surface_width_px,
                     context->frame->scene->surface_height_px,
                     0,
                     GL_RED,
                     GL_UNSIGNED_BYTE,
                     upload_pointer);
    } else {
        glTexSubImage2D(GL_TEXTURE_2D,
                        0,
                        0,
                        0,
                        context->frame->scene->surface_width_px,
                        context->frame->scene->surface_height_px,
                        GL_RED,
                        GL_UNSIGNED_BYTE,
                        upload_pointer);
    }

    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (used_pbo)
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    if (glGetError() != GL_NO_ERROR)
        return FALSE;
    if (size_changed) {
        renderer->text_mask_width = context->frame->scene->surface_width_px;
        renderer->text_mask_height = context->frame->scene->surface_height_px;
    }
    return TRUE;
}
