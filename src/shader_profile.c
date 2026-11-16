#include "shader_profile.h"

#include <math.h>

void
shader_settings_reset(ShaderSettings *settings)
{
    g_return_if_fail(settings != NULL);
    *settings = (ShaderSettings){
        .glow = 0.70f,
        .persistence = 0.24f,
        .vignette = 0.44f,
        .mask_strength = 0.36f,
        .curvature_strength = CHRONO_CURVATURE_DEFAULT,
        .aa_mode = AA_FXAA_HIGH,
        .palette = PALETTE_GREEN,
    };
}

ShaderSettings
shader_profile_settings(PresentationMode mode)
{
    ShaderSettings settings;
    shader_settings_reset(&settings);
    switch (mode) {
    case PRESENTATION_BALANCED:
        break;
    case PRESENTATION_AUTHENTIC:
        settings.glow = 0.82f;
        settings.persistence = 0.36f;
        settings.vignette = 0.58f;
        settings.mask_strength = 0.55f;
        settings.curvature_strength = 0.60f;
        break;
    case PRESENTATION_READABLE:
        settings.glow = 0.34f;
        settings.persistence = 0.10f;
        settings.vignette = 0.20f;
        settings.mask_strength = 0.14f;
        settings.curvature_strength = 0.18f;
        break;
    case PRESENTATION_OFF:
        settings.glow = 0.0f;
        settings.persistence = 0.0f;
        settings.vignette = 0.0f;
        settings.mask_strength = 0.0f;
        settings.curvature_strength = 0.0f;
        settings.aa_mode = AA_OFF;
        break;
    default:
        break;
    }
    return settings;
}

void
shader_profile_begin(AppPresentationState *presentation,
                     PresentationMode mode)
{
    g_return_if_fail(presentation != NULL);
    if (mode < PRESENTATION_BALANCED ||
        mode >= CHRONO_PRESENTATION_MODE_COUNT)
        mode = PRESENTATION_BALANCED;

    presentation->presentation_mode = mode;
    presentation->profile_from = presentation->settings;
    presentation->profile_target = shader_profile_settings(mode);
    presentation->profile_target.palette = presentation->settings.palette;
    presentation->profile_transition_elapsed = 0.0;
    presentation->profile_transition_duration = 0.24;
    presentation->profile_transition_active = TRUE;
}

static float
interpolate_float(float from, float to, double amount)
{
    return (float)(from + (to - from) * amount);
}

ShaderProfileStep
shader_profile_advance(AppPresentationState *presentation,
                       double delta_seconds)
{
    ShaderProfileStep result = {0};
    g_return_val_if_fail(presentation != NULL, result);
    if (!presentation->profile_transition_active)
        return result;

    if (!isfinite(delta_seconds) || delta_seconds < 0.0)
        delta_seconds = 0.0;
    if (!isfinite(presentation->profile_transition_elapsed) ||
        presentation->profile_transition_elapsed < 0.0)
        presentation->profile_transition_elapsed = 0.0;
    if (!isfinite(presentation->profile_transition_duration) ||
        presentation->profile_transition_duration < 0.0)
        presentation->profile_transition_duration = 0.0;

    presentation->profile_transition_elapsed += delta_seconds;
    double linear = presentation->profile_transition_duration > 0.0
        ? CLAMP(presentation->profile_transition_elapsed /
                presentation->profile_transition_duration, 0.0, 1.0)
        : 1.0;
    double amount = linear * linear * (3.0 - 2.0 * linear);

#define INTERPOLATE_SETTING(field) \
    presentation->settings.field = interpolate_float( \
        presentation->profile_from.field, \
        presentation->profile_target.field, amount)
    INTERPOLATE_SETTING(glow);
    INTERPOLATE_SETTING(persistence);
    INTERPOLATE_SETTING(vignette);
    INTERPOLATE_SETTING(mask_strength);
    INTERPOLATE_SETTING(curvature_strength);
#undef INTERPOLATE_SETTING

    if (linear >= 0.5) {
        AntiAliasingMode previous_aa = presentation->settings.aa_mode;
        presentation->settings.aa_mode = presentation->profile_target.aa_mode;
        presentation->settings.palette = presentation->profile_target.palette;
        result.anti_aliasing_changed =
            presentation->settings.aa_mode != previous_aa;
    }
    if (linear >= 1.0) {
        presentation->settings = presentation->profile_target;
        presentation->profile_transition_active = FALSE;
        result.transition_completed = TRUE;
    }
    result.settings_changed = TRUE;
    return result;
}
