#pragma once

#include "renderer.h"
#include "renderer_state.h"
#include "app_view.h"

typedef struct {
    Renderer *renderer;
    const RendererFrameInput *frame;
} RendererFrameContext;

void renderer_internal_clear_gl_errors(void);
gboolean renderer_internal_size_to_gl_sizeiptr(gsize size, GLsizeiptr *out);
gboolean renderer_internal_geometry_upload_arguments(const GArray *vertices,
                                                       GLsizeiptr *bytes_out,
                                                       GLsizei *count_out);
float renderer_internal_unit_float_or_zero(float value);
gint64 renderer_internal_nonnegative_elapsed_us(gint64 end_us, gint64 start_us);
void renderer_internal_append_geometry_line(RendererFrameContext *context,
                                            double x0,
                                            double y0,
                                            double x1,
                                            double y1,
                                            double width,
                                            float r,
                                            float g,
                                            float b,
                                            float a);

gboolean renderer_ensure_aa_targets(Renderer *renderer, int width, int height);
gboolean renderer_upload_scene_texture(RendererFrameContext *context);
gboolean renderer_upload_text_mask_texture(RendererFrameContext *context);
gboolean renderer_render_composed_scene(RendererFrameContext *context,
                                         const AppViewSnapshot *view);
gboolean renderer_render_cursor_overlay(RendererFrameContext *context,
                                         const AppViewSnapshot *view);
gboolean renderer_render_anti_aliasing_pass(RendererFrameContext *context,
                                             GLuint *output_texture);
gboolean renderer_render_persistence_pass(RendererFrameContext *context,
                                           GLuint current_scene_texture,
                                           GLuint *history_texture_out,
                                           gboolean *history_enabled_out);
gboolean renderer_render_final_crt_cache(RendererFrameContext *context,
                                          const AppViewSnapshot *view,
                                          GLuint filtered_scene_texture,
                                          GLuint history_texture,
                                          gboolean history_enabled);
gboolean renderer_present_final_cache(RendererFrameContext *context);
