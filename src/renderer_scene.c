#include "renderer_internal.h"

#include <float.h>
#include <math.h>

#define CHRONO_PI 3.14159265358979323846
#define CHRONO_HALF_PI 1.57079632679489661923

static gboolean
make_geometry_vertex(const RendererFrameContext *context,
                     double x,
                     double y,
                     float r,
                     float g,
                     float b,
                     float a,
                     GeometryVertex *out_vertex)
{
    if (context == NULL || out_vertex == NULL ||
        !isfinite(x) || !isfinite(y) ||
        !isfinite(r) || !isfinite(g) || !isfinite(b) || !isfinite(a))
        return FALSE;

    double width = MAX(context->frame->scene->logical_width, 1);
    double height = MAX(context->frame->scene->logical_height, 1);
    double normalized_x = x / width * 2.0 - 1.0;
    double normalized_y = 1.0 - y / height * 2.0;
    if (!isfinite(normalized_x) || !isfinite(normalized_y) ||
        fabs(normalized_x) > FLT_MAX || fabs(normalized_y) > FLT_MAX)
        return FALSE;

    *out_vertex = (GeometryVertex){
        .x = (float)normalized_x,
        .y = (float)normalized_y,
        .r = r,
        .g = g,
        .b = b,
        .a = a,
    };
    return TRUE;
}

static gboolean
fill_geometry_vertices(const RendererFrameContext *context,
                       const double positions[][2],
                       gsize count,
                       float r,
                       float g,
                       float b,
                       float a,
                       GeometryVertex *vertices)
{
    if (positions == NULL || vertices == NULL || count == 0)
        return FALSE;
    for (gsize i = 0; i < count; ++i) {
        if (!make_geometry_vertex(context,
                                  positions[i][0], positions[i][1],
                                  r, g, b, a, &vertices[i]))
            return FALSE;
    }
    return TRUE;
}

static void append_geometry_quad(RendererFrameContext *context,
                                 double x0,
                                 double y0,
                                 double x1,
                                 double y1,
                                 float r,
                                 float g,
                                 float b,
                                 float a)
{
    const double positions[6][2] = {
        {x0, y0}, {x1, y0}, {x1, y1},
        {x0, y0}, {x1, y1}, {x0, y1},
    };
    GeometryVertex vertices[6];
    if (!fill_geometry_vertices(context, positions, G_N_ELEMENTS(positions),
                                r, g, b, a, vertices))
        return;
    g_array_append_vals(context->renderer->geometry_vertices, vertices,
                        G_N_ELEMENTS(vertices));
}

void
renderer_internal_append_geometry_line(RendererFrameContext *context,
                                 double x0,
                                 double y0,
                                 double x1,
                                 double y1,
                                 double width,
                                 float r,
                                 float g,
                                 float b,
                                 float a)
{
    if (!isfinite(x0) || !isfinite(y0) ||
        !isfinite(x1) || !isfinite(y1) || !isfinite(width) ||
        !isfinite(r) || !isfinite(g) || !isfinite(b) || !isfinite(a) ||
        width <= 0.0 || a <= 0.0f)
        return;

    double dx = x1 - x0;
    double dy = y1 - y0;
    if (!isfinite(dx) || !isfinite(dy))
        return;
    double length = hypot(dx, dy);
    if (!isfinite(length) || length <= 0.0001)
        return;

    double nx = -dy / length * width * 0.5;
    double ny = dx / length * width * 0.5;
    if (!isfinite(nx) || !isfinite(ny))
        return;
    const double positions[6][2] = {
        {x0 + nx, y0 + ny},
        {x1 + nx, y1 + ny},
        {x1 - nx, y1 - ny},
        {x0 + nx, y0 + ny},
        {x1 - nx, y1 - ny},
        {x0 - nx, y0 - ny},
    };
    GeometryVertex vertices[6];
    if (!fill_geometry_vertices(context, positions, G_N_ELEMENTS(positions),
                                r, g, b, a, vertices))
        return;
    g_array_append_vals(context->renderer->geometry_vertices, vertices,
                        G_N_ELEMENTS(vertices));
}

static int
geometry_arc_segments(const RendererFrameContext *context, double radius, double span)
{
    if (context == NULL || !isfinite(radius) || !isfinite(span) ||
        radius <= 0.0 || span <= 0.0)
        return 0;
    double physical_radius = radius * MAX(context->frame->scene->surface_scale, 1);
    double arc_length = fabs(span) * MAX(physical_radius, 1.0);
    if (!isfinite(physical_radius) || !isfinite(arc_length))
        return 0;
    double requested = ceil(arc_length / CHRONO_GAUGE_SEGMENT_LENGTH_PX);
    if (!isfinite(requested))
        return 0;
    requested = CLAMP(requested, 12.0,
                      (double)CHRONO_GAUGE_MAX_SEGMENTS);
    return (int)requested;
}

static void
append_geometry_annular_band(RendererFrameContext *context,
                             double cx,
                             double cy,
                             double inner_radius,
                             double outer_radius,
                             double start_angle,
                             double end_angle,
                             float r,
                             float g,
                             float b,
                             float inner_alpha,
                             float outer_alpha)
{
    if (!isfinite(cx) || !isfinite(cy) ||
        !isfinite(inner_radius) || !isfinite(outer_radius) ||
        !isfinite(start_angle) || !isfinite(end_angle) ||
        !isfinite(r) || !isfinite(g) || !isfinite(b) ||
        !isfinite(inner_alpha) || !isfinite(outer_alpha) ||
        inner_radius < 0.0 || outer_radius <= inner_radius ||
        end_angle <= start_angle ||
        (inner_alpha <= 0.0f && outer_alpha <= 0.0f))
        return;

    int segments = geometry_arc_segments(context, outer_radius,
                                         end_angle - start_angle);
    for (int i = 0; i < segments; ++i) {
        double t0 = (double)i / segments;
        double t1 = (double)(i + 1) / segments;
        double a0 = start_angle + (end_angle - start_angle) * t0;
        double a1 = start_angle + (end_angle - start_angle) * t1;
        double c0 = cos(a0), s0 = sin(a0);
        double c1 = cos(a1), s1 = sin(a1);
        const double positions[6][2] = {
            {cx + c0 * inner_radius, cy + s0 * inner_radius},
            {cx + c0 * outer_radius, cy + s0 * outer_radius},
            {cx + c1 * outer_radius, cy + s1 * outer_radius},
            {cx + c0 * inner_radius, cy + s0 * inner_radius},
            {cx + c1 * outer_radius, cy + s1 * outer_radius},
            {cx + c1 * inner_radius, cy + s1 * inner_radius},
        };
        GeometryVertex vertices[6];
        const float alphas[6] = {
            inner_alpha, outer_alpha, outer_alpha,
            inner_alpha, outer_alpha, inner_alpha,
        };
        for (gsize j = 0; j < G_N_ELEMENTS(vertices); ++j) {
            if (!make_geometry_vertex(context,
                                      positions[j][0], positions[j][1],
                                      r, g, b, alphas[j], &vertices[j]))
                return;
        }
        g_array_append_vals(context->renderer->geometry_vertices, vertices,
                            G_N_ELEMENTS(vertices));
    }
}

static void
append_geometry_antialiased_arc(RendererFrameContext *context,
                                  double cx,
                                  double cy,
                                  double radius,
                                  double start_angle,
                                  double end_angle,
                                  double width,
                                  float r,
                                  float g,
                                  float b,
                                  float alpha)
{
    if (!isfinite(cx) || !isfinite(cy) || !isfinite(radius) ||
        !isfinite(start_angle) || !isfinite(end_angle) || !isfinite(width) ||
        !isfinite(r) || !isfinite(g) || !isfinite(b) || !isfinite(alpha) ||
        radius <= 0.0 || width <= 0.0 || alpha <= 0.0f ||
        end_angle <= start_angle)
        return;

    double pixel = 1.0 / MAX(context->frame->scene->surface_scale, 1);
    double feather = CHRONO_GAUGE_EDGE_FEATHER_PX * pixel;
    double inner = MAX(radius - width * 0.5, feather);
    double outer = radius + width * 0.5;

    append_geometry_annular_band(context, cx, cy,
                                 MAX(inner - feather, 0.0), inner,
                                 start_angle, end_angle, r, g, b,
                                 0.0f, alpha);
    append_geometry_annular_band(context, cx, cy, inner, outer,
                                 start_angle, end_angle, r, g, b,
                                 alpha, alpha);
    append_geometry_annular_band(context, cx, cy, outer, outer + feather,
                                 start_angle, end_angle, r, g, b,
                                 alpha, 0.0f);
}

static void
append_geometry_antialiased_line(RendererFrameContext *context,
                                  double x0,
                                  double y0,
                                  double x1,
                                  double y1,
                                  double width,
                                  float r,
                                  float g,
                                  float b,
                                  float alpha)
{
    double pixel = 1.0 / MAX(context->frame->scene->surface_scale, 1);
    double feather = CHRONO_GAUGE_EDGE_FEATHER_PX * pixel;
    renderer_internal_append_geometry_line(context, x0, y0, x1, y1,
                         width + feather * 2.0, r, g, b, alpha * 0.18f);
    renderer_internal_append_geometry_line(context, x0, y0, x1, y1,
                         width, r, g, b, alpha);
}

static void
append_hover_geometry(RendererFrameContext *context)
{
    if (!context->frame->hover_visible)
        return;
    Rect rect = context->frame->hover_rect;
    gboolean danger = context->frame->hover_dangerous;

    double inset = 2.0;
    double x0 = rect.x - inset;
    double y0 = rect.y - inset;
    double x1 = rect.x + rect.width + inset;
    double y1 = rect.y + rect.height + inset;
    double length = CLAMP(MIN(rect.width, rect.height) * 0.28, 5.0, 12.0);
    float r = danger ? 1.00f : 0.48f;
    float g = danger ? 0.30f : 1.00f;
    float b = danger ? 0.18f : 0.68f;

    const double segments[8][4] = {
        {x0, y0, x0 + length, y0}, {x0, y0, x0, y0 + length},
        {x1, y0, x1 - length, y0}, {x1, y0, x1, y0 + length},
        {x0, y1, x0 + length, y1}, {x0, y1, x0, y1 - length},
        {x1, y1, x1 - length, y1}, {x1, y1, x1, y1 - length},
    };
    for (int i = 0; i < 8; ++i) {
        renderer_internal_append_geometry_line(context,
                             segments[i][0], segments[i][1],
                             segments[i][2], segments[i][3],
                             7.0, r, g, b, 0.08f);
        renderer_internal_append_geometry_line(context,
                             segments[i][0], segments[i][1],
                             segments[i][2], segments[i][3],
                             1.5, r, g, b, 0.86f);
    }
}

static void append_focus_geometry(RendererFrameContext *context,
                                  const AppViewSnapshot *view)
{
    Rect dial = context->frame->presentation->layout.focus_dial;
    if (dial.width <= 1.0 || dial.height <= 1.0)
        return;

    const AppTaskView *task = view->has_active_task
        ? &view->active_task
        : view->has_expired_task ? &view->expired_task : NULL;

    double cx = dial.x + dial.width * 0.5;
    double cy = dial.y + dial.height * 0.5;
    double radius = MIN(dial.width, dial.height) * 0.42;
    double start_angle = -CHRONO_HALF_PI;
    double full_end = start_angle + 2.0 * CHRONO_PI;
    gboolean has_progress_signal = task != NULL;
    double progress = task != NULL ? task->progress_fraction : 0.0;
    progress = CLAMP(progress, 0.0, 1.0);

    /* A continuous annulus avoids the gaps and hard quad joints produced by
     * the former 160 independent line segments. The adaptive tessellation
     * keeps every chord near one physical pixel and the alpha fringe provides
     * true subpixel coverage before the optional post-process AA stage. */
    append_geometry_antialiased_arc(context, cx, cy, radius,
                                    start_angle, full_end,
                                    2.0, 0.18f, 0.52f, 0.36f, 0.26f);

    if (has_progress_signal && progress > 0.0) {
        gboolean expired = task != NULL &&
                           task->status == CHRONO_TASK_EXPIRED;
        float r = expired ? 1.0f : 0.44f;
        float g = expired ? 0.34f : 1.0f;
        float b = expired ? 0.22f : 0.63f;
        double progress_end = start_angle + 2.0 * CHRONO_PI * progress;

        append_geometry_antialiased_arc(context, cx, cy, radius,
                                        start_angle, progress_end,
                                        8.0, r, g, b, 0.055f);
        append_geometry_antialiased_arc(context, cx, cy, radius,
                                        start_angle, progress_end,
                                        2.8, r, g, b, 0.98f);

        double marker_inner = radius - 12.0;
        double marker_outer = radius + 13.0;
        append_geometry_antialiased_line(
            context,
            cx + cos(progress_end) * marker_inner,
            cy + sin(progress_end) * marker_inner,
            cx + cos(progress_end) * marker_outer,
            cy + sin(progress_end) * marker_outer,
            3.2, r, g, b, 0.98f);
    }

    for (int i = 0; i < 12; ++i) {
        double angle = start_angle + 2.0 * CHRONO_PI * i / 12.0;
        double inner = radius + 17.0;
        double outer = radius + (i % 3 == 0 ? 29.0 : 23.0);
        append_geometry_antialiased_line(
            context,
            cx + cos(angle) * inner,
            cy + sin(angle) * inner,
            cx + cos(angle) * outer,
            cy + sin(angle) * outer,
            i % 3 == 0 ? 2.0 : 1.2,
            0.27f, 0.76f, 0.50f,
            i % 3 == 0 ? 0.58f : 0.32f);
    }
}

static void
append_ledger_geometry(RendererFrameContext *context, const AppViewSnapshot *view)
{
    Rect graph = context->frame->presentation->layout.ledger_chart;
    if (graph.width <= 1.0 || graph.height <= 1.0)
        return;

    Rect plot = {
        graph.x + 54.0,
        graph.y + 27.0,
        MAX(graph.width - 68.0, 1.0),
        MAX(graph.height - 39.0, 1.0),
    };
    double centre_y = plot.y + plot.height * 0.5;
    renderer_internal_append_geometry_line(context, plot.x, centre_y,
                         plot.x + plot.width, centre_y,
                         1.2, 0.24f, 0.70f, 0.47f, 0.44f);
    renderer_internal_append_geometry_line(context, plot.x, plot.y + 1.0,
                         plot.x + plot.width, plot.y + 1.0,
                         1.0, 0.20f, 0.52f, 0.37f, 0.13f);
    renderer_internal_append_geometry_line(context, plot.x, plot.y + plot.height - 1.0,
                         plot.x + plot.width, plot.y + plot.height - 1.0,
                         1.0, 0.20f, 0.52f, 0.37f, 0.13f);

    guint chart_limit = context->frame->presentation->layout.density == UI_DENSITY_FULL ? 18u : 12u;
    guint max_bars = MIN(view->ledger_chart_count, chart_limit);
    if (max_bars == 0)
        return;

    double maximum_slot = context->frame->presentation->layout.density == UI_DENSITY_FULL ? 72.0 : 54.0;
    double slot = MIN(plot.width / max_bars, maximum_slot);
    double group_width = slot * max_bars;
    double group_x = plot.x + (plot.width - group_width) * 0.5;
    double bar_width = CLAMP(slot * 0.34, 8.0,
                             context->frame->presentation->layout.density == UI_DENSITY_FULL
                                 ? 28.0 : 22.0);
    guint first = view->ledger_chart_count - max_bars;

    for (guint i = 0; i < max_bars; ++i) {
        const AppLedgerChartEntry *entry = &view->ledger_chart[first + i];
        double ratio = entry->estimate_seconds > 0
            ? (double)entry->elapsed_seconds / (double)entry->estimate_seconds
            : 1.0;
        /* Map a ±50% estimate error to the available half-height.  This keeps
         * ordinary 10–30% deviations legible without allowing a single large
         * overrun to dominate the compact analytical strip. */
        double signed_delta = CLAMP((ratio - 1.0) / 0.5, -1.0, 1.0);
        double bar_height = MAX(fabs(signed_delta) * plot.height * 0.43, 2.5);
        double centre_x = group_x + (i + 0.5) * slot;
        double x0 = centre_x - bar_width * 0.5;
        double x1 = centre_x + bar_width * 0.5;
        double y0 = signed_delta >= 0.0 ? centre_y - bar_height : centre_y;
        double y1 = signed_delta >= 0.0 ? centre_y : centre_y + bar_height;
        float red = signed_delta > 0.02 ? 0.96f : 0.34f;
        float green = signed_delta > 0.02 ? 0.52f : 0.92f;
        float blue = signed_delta > 0.02 ? 0.24f : 0.56f;

        append_geometry_quad(context, x0 - 2.0, y0, x1 + 2.0, y1,
                             red, green, blue, 0.14f);
        append_geometry_quad(context, x0, y0, x1, y1,
                             red, green, blue, 0.72f);
        double cap_y = signed_delta >= 0.0 ? y0 : y1;
        renderer_internal_append_geometry_line(context, x0 - 1.0, cap_y,
                             x1 + 1.0, cap_y,
                             1.5, red, green, blue, 0.92f);
    }
}

static void
build_dynamic_geometry(RendererFrameContext *context,
                       const AppViewSnapshot *view)
{
    g_array_set_size(context->renderer->geometry_vertices, 0);
    if (context->frame->presentation->layout.density == UI_DENSITY_TOO_SMALL)
        return;
    if (context->frame->presentation->active_tab == CHRONO_MODE_FOCUS)
        append_focus_geometry(context, view);
    else if (context->frame->presentation->active_tab == CHRONO_MODE_LEDGER)
        append_ledger_geometry(context, view);
    append_hover_geometry(context);
}

static gboolean ensure_composed_target(Renderer *renderer, int width, int height)
{
    if (width <= 0 || height <= 0 ||
        width > renderer->max_texture_size ||
        height > renderer->max_texture_size)
        return FALSE;
    if (renderer->composed_width == width && renderer->composed_height == height)
        return TRUE;

    GLint saved_texture = 0;
    GLint saved_fbo = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &saved_texture);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &saved_fbo);

    renderer_internal_clear_gl_errors();
    glBindTexture(GL_TEXTURE_2D, renderer->composed_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindFramebuffer(GL_FRAMEBUFFER, renderer->composed_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, renderer->composed_texture, 0);
    gboolean complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
                        GL_FRAMEBUFFER_COMPLETE;

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)saved_fbo);
    glBindTexture(GL_TEXTURE_2D, (GLuint)saved_texture);
    if (!complete || glGetError() != GL_NO_ERROR)
        return FALSE;

    renderer->composed_width = width;
    renderer->composed_height = height;
    renderer->filtered_scene_valid = FALSE;
    renderer->final_scene_valid = FALSE;
    renderer->history_reset_requested = TRUE;
    renderer->history_valid = FALSE;
    return TRUE;
}

gboolean
renderer_render_composed_scene(RendererFrameContext *context, const AppViewSnapshot *view)
{
    Renderer *renderer = context->renderer;
    if (!ensure_composed_target(renderer,
                                context->frame->scene->surface_width_px,
                                context->frame->scene->surface_height_px))
        return FALSE;

    GLint saved_fbo = 0;
    GLint saved_viewport[4] = {0};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &saved_fbo);
    glGetIntegerv(GL_VIEWPORT, saved_viewport);

    glBindFramebuffer(GL_FRAMEBUFFER, renderer->composed_fbo);
    glViewport(0, 0, context->frame->scene->surface_width_px, context->frame->scene->surface_height_px);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(renderer->composite_program);
    glUniform1i(renderer->c_source, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer->scene_texture);
    glBindVertexArray(renderer->vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    build_dynamic_geometry(context, view);
    if (renderer->geometry_vertices->len > 0) {
        GLsizeiptr geometry_bytes = 0;
        GLsizei geometry_count = 0;
        if (!renderer_internal_geometry_upload_arguments(renderer->geometry_vertices,
                                       &geometry_bytes, &geometry_count)) {
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindVertexArray(0);
            glBindTexture(GL_TEXTURE_2D, 0);
            glUseProgram(0);
            glDisable(GL_BLEND);
            glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)saved_fbo);
            glViewport(saved_viewport[0], saved_viewport[1],
                       saved_viewport[2], saved_viewport[3]);
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
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDrawArrays(GL_TRIANGLES, 0, geometry_count);
        glDisable(GL_BLEND);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)saved_fbo);
    glViewport(saved_viewport[0], saved_viewport[1],
               saved_viewport[2], saved_viewport[3]);
    return glGetError() == GL_NO_ERROR;
}
