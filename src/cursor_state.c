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
    if (scene == NULL || preferences == NULL ||
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
    double source_px = px *
        (1.0 + CHRONO_CURVATURE_STRENGTH * py2 * py2 * 0.13);
    double source_py = py *
        (1.0 + CHRONO_CURVATURE_STRENGTH * px2 * px2 * 0.17);
    double mapped_x = (source_px * 0.5 + 0.5) * width;
    double mapped_y = (source_py * 0.5 + 0.5) * height;
    *scene_x = isfinite(mapped_x) ? mapped_x : raw_x;
    *scene_y = isfinite(mapped_y) ? mapped_y : raw_y;
}

void
cursor_state_refresh_mapping(AppInputState *input,
                             const AppSceneState *scene,
                             const ChronoPreferences *preferences)
{
    g_return_if_fail(input != NULL);
    if (!input->pointer_inside)
        return;
    cursor_state_map_pointer(scene, preferences,
                             input->raw_pointer_x,
                             input->raw_pointer_y,
                             &input->pointer_x,
                             &input->pointer_y);
}

static void
cursor_trail_reset_samples(AppInputState *input)
{
    input->cursor_trail_head = 0;
    input->cursor_trail_count = 0;
    input->cursor_trail_last_sample_us = 0;
    input->cursor_trail_last_x = 0.0;
    input->cursor_trail_last_y = 0.0;
}

void
cursor_state_clear(AppInputState *input, Renderer *renderer)
{
    g_return_if_fail(input != NULL);
    cursor_trail_reset_samples(input);
    input->cursor_phosphor_active_until_us = 0;
    renderer_clear_cursor_history(renderer);
}

static double
cursor_trail_duration_seconds(float amount)
{
    if (!isfinite(amount) || amount <= 0.001f)
        return 0.0;
    double normalized = CLAMP((double)amount, 0.0, 1.0);
    return 0.15 + pow(normalized, 4.0) * 29.85;
}

static void
cursor_trail_append(AppInputState *input,
                    double x,
                    double y,
                    gint64 timestamp_us)
{
    if (!isfinite(x) || !isfinite(y) || timestamp_us <= 0)
        return;
    input->cursor_trail_head %= CHRONO_CURSOR_TRAIL_SAMPLES;
    input->cursor_trail_count = MIN(input->cursor_trail_count,
                                    (unsigned int)CHRONO_CURSOR_TRAIL_SAMPLES);
    CursorTrailPoint *point = &input->cursor_trail[input->cursor_trail_head];
    *point = (CursorTrailPoint){x, y, timestamp_us};
    input->cursor_trail_head =
        (input->cursor_trail_head + 1u) % CHRONO_CURSOR_TRAIL_SAMPLES;
    if (input->cursor_trail_count < CHRONO_CURSOR_TRAIL_SAMPLES)
        ++input->cursor_trail_count;
}

static void
cursor_trail_prune(AppInputState *input,
                   const ShaderSettings *settings,
                   gint64 now_us)
{
    gint64 retention_us = (gint64)llround(
        cursor_trail_duration_seconds(settings->cursor_persistence) *
        G_USEC_PER_SEC);
    input->cursor_trail_count = MIN(input->cursor_trail_count,
                                    (unsigned int)CHRONO_CURSOR_TRAIL_SAMPLES);
    gint64 retention_limit_us = saturating_add_i64(retention_us, 50000);
    while (input->cursor_trail_count > 1) {
        unsigned int oldest =
            (input->cursor_trail_head + CHRONO_CURSOR_TRAIL_SAMPLES -
             input->cursor_trail_count) % CHRONO_CURSOR_TRAIL_SAMPLES;
        const CursorTrailPoint *point = &input->cursor_trail[oldest];
        if (point->timestamp_us > 0 && point->timestamp_us <= now_us &&
            now_us - point->timestamp_us <= retention_limit_us)
            break;
        --input->cursor_trail_count;
    }
}

static void
cursor_phosphor_charge(AppInputState *input,
                       const ShaderSettings *settings,
                       const ChronoPreferences *preferences,
                       Renderer *renderer,
                       gint64 now_us)
{
    if (!preferences->cursor_shader_effects_enabled) {
        input->cursor_phosphor_active_until_us = 0;
        renderer_invalidate_cursor_history(renderer);
        return;
    }

    double raw_amount = settings->persistence;
    double amount = isfinite(raw_amount)
        ? CLAMP(raw_amount, 0.0, 1.0)
        : 0.0;
    if (amount <= 0.0001) {
        input->cursor_phosphor_active_until_us = 0;
        renderer_invalidate_cursor_history(renderer);
        return;
    }
    if (input->cursor_phosphor_active_until_us > 0 &&
        now_us >= input->cursor_phosphor_active_until_us) {
        renderer_clear_cursor_history(renderer);
    }
    double half_life = 0.025 + amount * amount * 0.62;
    gint64 tail_us = (gint64)llround(MAX(0.18, half_life * 5.0) *
                                     G_USEC_PER_SEC);
    input->cursor_phosphor_active_until_us =
        MAX(input->cursor_phosphor_active_until_us,
            saturating_add_i64(now_us, tail_us));
}

void
cursor_state_record_motion(AppInputState *input,
                           const ShaderSettings *settings,
                           const ChronoPreferences *preferences,
                           Renderer *renderer,
                           double current_x,
                           double current_y,
                           gint64 now_us)
{
    g_return_if_fail(input != NULL);
    g_return_if_fail(settings != NULL);
    g_return_if_fail(preferences != NULL);
    if (!input->pointer_inside || !isfinite(current_x) ||
        !isfinite(current_y) || now_us <= 0)
        return;

    cursor_phosphor_charge(input, settings, preferences, renderer, now_us);
    if (cursor_trail_duration_seconds(settings->cursor_persistence) <= 0.0) {
        cursor_trail_reset_samples(input);
        return;
    }

    cursor_trail_prune(input, settings, now_us);
    if (input->cursor_trail_count == 0 ||
        input->cursor_trail_last_sample_us <= 0) {
        cursor_trail_append(input, current_x, current_y, now_us);
        input->cursor_trail_last_sample_us = now_us;
        input->cursor_trail_last_x = current_x;
        input->cursor_trail_last_y = current_y;
        return;
    }

    double start_x = input->cursor_trail_last_x;
    double start_y = input->cursor_trail_last_y;
    if (!isfinite(start_x) || !isfinite(start_y)) {
        cursor_trail_reset_samples(input);
        cursor_trail_append(input, current_x, current_y, now_us);
        input->cursor_trail_last_sample_us = now_us;
        input->cursor_trail_last_x = current_x;
        input->cursor_trail_last_y = current_y;
        return;
    }
    double dx = current_x - start_x;
    double dy = current_y - start_y;
    double distance = hypot(dx, dy);
    gint64 start_us = input->cursor_trail_last_sample_us;
    if (!isfinite(distance)) {
        cursor_trail_reset_samples(input);
        cursor_trail_append(input, current_x, current_y, now_us);
        input->cursor_trail_last_sample_us = now_us;
        input->cursor_trail_last_x = current_x;
        input->cursor_trail_last_y = current_y;
        return;
    }
    gint64 elapsed_us = nonnegative_elapsed_us(now_us, start_us);
    if (now_us < start_us)
        now_us = start_us;

    if (distance < 0.5) {
        unsigned int latest_index =
            (input->cursor_trail_head + CHRONO_CURSOR_TRAIL_SAMPLES - 1u) %
            CHRONO_CURSOR_TRAIL_SAMPLES;
        CursorTrailPoint *latest = &input->cursor_trail[latest_index];
        latest->x = current_x;
        latest->y = current_y;
        latest->timestamp_us = now_us;
        input->cursor_trail_last_sample_us = now_us;
        input->cursor_trail_last_x = current_x;
        input->cursor_trail_last_y = current_y;
        return;
    }

    double requested_samples = ceil(
        distance / CHRONO_REFERENCE_CURSOR_SAMPLE_SPACING);
    requested_samples = CLAMP(requested_samples, 1.0, 2048.0);
    unsigned int sample_count = (unsigned int)requested_samples;
    gint64 whole = elapsed_us / sample_count;
    gint64 remainder = elapsed_us % sample_count;
    for (unsigned int i = 1; i <= sample_count; ++i) {
        double t = (double)i / (double)sample_count;
        gint64 interpolated_us = whole * i +
            (remainder * i) / sample_count;
        cursor_trail_append(input,
                            start_x + dx * t,
                            start_y + dy * t,
                            saturating_add_i64(start_us, interpolated_us));
    }

    input->cursor_trail_last_sample_us = now_us;
    input->cursor_trail_last_x = current_x;
    input->cursor_trail_last_y = current_y;
}

void
cursor_state_update(AppInputState *input,
                    const ShaderSettings *settings,
                    const ChronoPreferences *preferences,
                    Renderer *renderer,
                    gint64 now_us)
{
    g_return_if_fail(input != NULL);
    g_return_if_fail(settings != NULL);
    g_return_if_fail(preferences != NULL);
    if (!isfinite(input->raw_pointer_x) ||
        !isfinite(input->raw_pointer_y) || now_us <= 0)
        return;
    if (cursor_trail_duration_seconds(settings->cursor_persistence) <= 0.0) {
        cursor_trail_reset_samples(input);
        return;
    }
    if (!input->pointer_inside)
        return;

    cursor_trail_prune(input, settings, now_us);
    if (input->cursor_trail_count == 0 ||
        input->cursor_trail_last_sample_us <= 0) {
        cursor_state_record_motion(input, settings, preferences, renderer,
                                   input->raw_pointer_x,
                                   input->raw_pointer_y, now_us);
        return;
    }

    double dx = input->raw_pointer_x - input->cursor_trail_last_x;
    double dy = input->raw_pointer_y - input->cursor_trail_last_y;
    if (hypot(dx, dy) >= 0.5) {
        cursor_state_record_motion(input, settings, preferences, renderer,
                                   input->raw_pointer_x,
                                   input->raw_pointer_y, now_us);
        return;
    }

    unsigned int latest_index =
        (input->cursor_trail_head + CHRONO_CURSOR_TRAIL_SAMPLES - 1u) %
        CHRONO_CURSOR_TRAIL_SAMPLES;
    CursorTrailPoint *latest = &input->cursor_trail[latest_index];
    latest->x = input->raw_pointer_x;
    latest->y = input->raw_pointer_y;
    latest->timestamp_us = now_us;
    input->cursor_trail_last_sample_us = now_us;
    input->cursor_trail_last_x = input->raw_pointer_x;
    input->cursor_trail_last_y = input->raw_pointer_y;
}
