#pragma once

#include "app_state.h"

G_BEGIN_DECLS

typedef struct _AppViewSnapshot AppViewSnapshot;

typedef struct {
    const AppPresentationState *presentation;
    const AppSceneState *scene;
    const AppInputState *input;
    const AppViewSnapshot *view;
    double frame_delta;
    double render_interval;
    double frame_clock_fps;
    double monitor_refresh_hz;
    gboolean scene_needs_upload;
    gboolean hover_visible;
    Rect hover_rect;
    gboolean hover_dangerous;
} RendererFrameInput;

typedef struct {
    gboolean scene_uploaded;
} RendererFrameResult;

typedef enum {
    RENDERER_DRAW_OK,
    RENDERER_DRAW_TARGET_ALLOCATION_FAILED,
    RENDERER_DRAW_SCENE_UPLOAD_FAILED,
    RENDERER_DRAW_TEXT_MASK_UPLOAD_FAILED,
    RENDERER_DRAW_COMPOSITION_FAILED,
    RENDERER_DRAW_ANTI_ALIASING_FAILED,
    RENDERER_DRAW_PERSISTENCE_FAILED,
    RENDERER_DRAW_FINAL_CACHE_FAILED,
    RENDERER_DRAW_PRESENT_FAILED,
    RENDERER_DRAW_CURSOR_FAILED,
    RENDERER_DRAW_GL_ERROR,
} RendererDrawStatus;

Renderer *renderer_create(void);
void renderer_destroy(Renderer *renderer);

gboolean renderer_is_ready(const Renderer *renderer);
gint renderer_get_max_texture_size(const Renderer *renderer);
gboolean renderer_cursor_history_is_valid(const Renderer *renderer);
void renderer_invalidate_cursor_history(Renderer *renderer);
void renderer_clear_cursor_history(Renderer *renderer);

void renderer_realize(AppState *state);
void renderer_unrealize(AppState *state);
void renderer_discard_resources(Renderer *renderer);
void renderer_reset_frame_timing(Renderer *renderer);
void renderer_request_history_reset(Renderer *renderer);
void renderer_advance_scene_history(Renderer *renderer, gint64 now_us);
void renderer_invalidate_filtered(Renderer *renderer);
void renderer_invalidate_final(Renderer *renderer);
gboolean renderer_filtered_scene_is_valid(const Renderer *renderer);
gboolean renderer_output_is_valid(const Renderer *renderer);
gboolean renderer_frame_cache_is_valid(const Renderer *renderer);
gboolean renderer_needs_render(const Renderer *renderer);
RendererDrawStatus renderer_draw(Renderer *renderer,
                                 const RendererFrameInput *frame,
                                 RendererFrameResult *result,
                                 unsigned int *gl_error_out);
const char *renderer_draw_status_message(RendererDrawStatus status);

G_END_DECLS
