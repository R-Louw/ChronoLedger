#pragma once

#include "app_state.h"

G_BEGIN_DECLS

typedef struct {
    gboolean settings_changed;
    gboolean anti_aliasing_changed;
    gboolean transition_completed;
} ShaderProfileStep;

void shader_settings_reset(ShaderSettings *settings);
ShaderSettings shader_profile_settings(PresentationMode mode);
void shader_profile_begin(AppPresentationState *presentation,
                          PresentationMode mode);
ShaderProfileStep shader_profile_advance(AppPresentationState *presentation,
                                         double delta_seconds);

G_END_DECLS
