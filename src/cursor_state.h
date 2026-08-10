#pragma once

#include "app_state.h"

G_BEGIN_DECLS

void cursor_state_map_pointer(const AppSceneState *scene,
                              const ChronoPreferences *preferences,
                              double raw_x,
                              double raw_y,
                              double *scene_x,
                              double *scene_y);
void cursor_state_refresh_mapping(AppInputState *input,
                                  const AppSceneState *scene,
                                  const ChronoPreferences *preferences);
void cursor_state_clear(AppInputState *input, Renderer *renderer);
void cursor_state_record_motion(AppInputState *input,
                                const ShaderSettings *settings,
                                const ChronoPreferences *preferences,
                                Renderer *renderer,
                                double x,
                                double y,
                                gint64 timestamp_us);
void cursor_state_update(AppInputState *input,
                         const ShaderSettings *settings,
                         const ChronoPreferences *preferences,
                         Renderer *renderer,
                         gint64 now_us);

G_END_DECLS
