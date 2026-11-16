#include "cursor_state.h"

#include "renderer.h"

#include <math.h>

static gint64
saturating_add_i64(gint64 value, gint64 increment)
{
    if (increment > 0 && value > G_MAXINT64 - increment)
        return G_MAXINT64;
    if (increment < 0 && value < G_MININT64 - increment)
        return G_MININT64;
    return value + increment;
}

static gint64
nonnegative_elapsed_us(gint64 end_us, gint64 start_us)
{
    if (end_us <= start_us)
        return 0;
    if (start_us < 0 && end_us > G_MAXINT64 + start_us)
        return G_MAXINT64;
    return end_us - start_us;
}

void
cursor_state_map_pointer(const AppSceneState *scene,
                         const ChronoPreferences *preferences,
                         const ShaderSettings *settings,
                         double raw_x,
                         double raw_y,
                         double *scene_x,
                         double *scene_y)
{
    g_return_if_fail(scene_x != NULL);
    g_return_if_fail(scene_y != NULL);
    if (!isfinite(raw_x) || !isfinite(raw_y)) {
        *scene_x = 0.0;
        *scene_y = 0.0;
        return;
    }
    if (scene == NULL || preferences == NULL || settings == NULL ||
        !preferences->curvature_enabled) {
        *scene_x = raw_x;
        *scene_y = raw_y;
        return;
    }

    double width = MAX(scene->logical_width, 1);
    double height = MAX(scene->logical_height, 1);
    double u = raw_x / width;
    double v = raw_y / height;
    double px = u * 2.0 - 1.0;
    double py = v * 2.0 - 1.0;
    double px2 = px * px;
    double py2 = py * py;
    double strength = CLAMP(settings->curvature_strength, 0.0f, 1.0f) * CHRONO_CURVATURE_MAX;
    double source_px = px * (1.0 + strength * py2 * py2 * 0.13);
    double source_py = py * (1.0 + strength * px2 * px2 * 0.17);
    double mapped_x = (source_px * 0.5 + 0.5) * width;
    double mapped_y = (source_py * 0.5 + 0.5) * height;
    *scene_x = isfinite(mapped_x) ? mapped_x : raw_x;
    *scene_y = isfinite(mapped_y) ? mapped_y : raw_y;
}

double
cursor_state_curvature_drag_value(const AppSceneState *scene, Rect track,
                                  double raw_x, double raw_y)
{
    if (scene == NULL || track.width <= 0 || !isfinite(raw_x) || !isfinite(raw_y))
        return 0;
    double width = MAX(scene->logical_width, 1);
    double py = raw_y / MAX(scene->logical_height, 1) * 2.0 - 1.0;
    // source_x = raw_x + value * offset. Solving for value keeps the
    // visible handle under the raw cursor even as dragging changes its warp.
    double offset = (raw_x - width * .5) * CHRONO_CURVATURE_MAX * pow(py, 4) * .13;
    double denominator = track.width - offset;
    if (!isfinite(denominator) || denominator <= 0) return 0;
    return CLAMP((raw_x - track.x) / denominator, 0.0, 1.0);
}

void
cursor_state_refresh_mapping(AppInputState *input,
                             const AppSceneState *scene,
                             const ChronoPreferences *preferences,
                             const ShaderSettings *settings)
{
    g_return_if_fail(input != NULL);
    if (!input->pointer_inside)
        return;
    cursor_state_map_pointer(scene, preferences, settings,
                             input->raw_pointer_x,
                             input->raw_pointer_y,
                             &input->pointer_x,
                             &input->pointer_y);
}

/* All decay is evaluated from absolute monotonic age, never render count. */
double
cursor_state_half_life(float persistence)
{
    double amount = isfinite(persistence) ? CLAMP(persistence, 0.0f, 1.0f) : 0;
    return amount > 0.0001 ? 0.025 + amount * amount * 0.62 : 0;
}

float
cursor_state_emission(float persistence, gint64 age_us)
{
    double half_life = cursor_state_half_life(persistence);
    if (half_life == 0 || age_us < 0 || age_us >= half_life * 10 * G_USEC_PER_SEC)
        return 0;
    return (float)((0.10 + CLAMP(persistence, 0.0f, 1.0f) * 0.30) *
                   exp2(-(double)age_us / G_USEC_PER_SEC / half_life));
}

void
cursor_state_clear(AppInputState *input, Renderer *renderer)
{
    g_return_if_fail(input != NULL);
    input->cursor_sample_head = input->cursor_sample_count = 0;
    input->cursor_anchor_us = 0;
    renderer_clear_cursor_damage(renderer);
}

static void
append_sample(AppInputState *input, double x, double y, gint64 time_us)
{
    input->cursor_samples[input->cursor_sample_head] =
        (CursorPhosphorPoint){x, y, time_us};
    input->cursor_sample_head = (input->cursor_sample_head + 1) % CHRONO_CURSOR_SAMPLES;
    input->cursor_sample_count = MIN(input->cursor_sample_count + 1, CHRONO_CURSOR_SAMPLES);
}

void
cursor_state_update(AppInputState *input, const ShaderSettings *settings,
                    const ChronoPreferences *preferences, Renderer *renderer,
                    gint64 now_us)
{
    g_return_if_fail(input != NULL && settings != NULL && preferences != NULL);
    if (!preferences->cursor_shader_effects_enabled ||
        cursor_state_half_life(settings->persistence) == 0) {
        cursor_state_clear(input, renderer);
        return;
    }
    while (input->cursor_sample_count > 0) {
        guint oldest = (input->cursor_sample_head + CHRONO_CURSOR_SAMPLES -
                        input->cursor_sample_count) % CHRONO_CURSOR_SAMPLES;
        gint64 age = nonnegative_elapsed_us(now_us, input->cursor_samples[oldest].timestamp_us);
        if (cursor_state_emission(settings->persistence, age) > 0)
            break;
        --input->cursor_sample_count;
    }
}

void
cursor_state_record_motion(AppInputState *input, const ShaderSettings *settings,
                           const ChronoPreferences *preferences, Renderer *renderer,
                           double x, double y, gint64 now_us)
{
    g_return_if_fail(input != NULL && settings != NULL && preferences != NULL);
    if (!input->pointer_inside || !isfinite(x) || !isfinite(y) || now_us <= 0)
        return;
    if (!preferences->cursor_shader_effects_enabled ||
        cursor_state_half_life(settings->persistence) == 0) {
        cursor_state_clear(input, renderer);
        return;
    }
    if (input->cursor_anchor_us == 0) {
        append_sample(input, x, y, now_us);
    } else {
        now_us = MAX(now_us, input->cursor_anchor_us);
        /* No input path is known across a long pause or stalled event loop.
         * Re-excite departure now; never invent a seconds-long sweep. */
        if (nonnegative_elapsed_us(now_us, input->cursor_anchor_us) > 100000)
            input->cursor_anchor_us = now_us;
        double distance = hypot(x - input->cursor_anchor_x, y - input->cursor_anchor_y);
        if (!isfinite(distance)) return;
        if (distance > 0.01) {
            /* Re-excite the old live cross when departure follows a pause. */
            append_sample(input, input->cursor_anchor_x, input->cursor_anchor_y,
                          input->cursor_anchor_us);
            guint count = (guint)CLAMP(ceil(distance / CHRONO_CURSOR_SAMPLE_SPACING), 1, CHRONO_CURSOR_SAMPLES / 2);
            gint64 elapsed = nonnegative_elapsed_us(now_us, input->cursor_anchor_us);
            for (guint i = 1; i <= count; ++i) {
                double t = (double)i / count;
                append_sample(input, input->cursor_anchor_x + (x - input->cursor_anchor_x) * t,
                              input->cursor_anchor_y + (y - input->cursor_anchor_y) * t,
                              saturating_add_i64(input->cursor_anchor_us,
                                  elapsed / count * i + (elapsed % count) * i / count));
            }
        }
    }
    input->cursor_anchor_x = x;
    input->cursor_anchor_y = y;
    input->cursor_anchor_us = now_us;
}
