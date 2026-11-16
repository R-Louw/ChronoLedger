#pragma once

#include "app_state.h"

G_BEGIN_DECLS

void cursor_state_map_pointer(const AppSceneState *scene,
                              const ChronoPreferences *preferences,
                              const ShaderSettings *settings,
                              double raw_x,
                              double raw_y,
                              double *scene_x,
                              double *scene_y);
/* Solve the moving curvature slider's position against the raw pointer. */
double cursor_state_curvature_drag_value(const AppSceneState *scene, Rect track,
                                          double raw_x, double raw_y);
void cursor_state_refresh_mapping(AppInputState *input,
                                  const AppSceneState *scene,
                                  const ChronoPreferences *preferences,
                                  const ShaderSettings *settings);
double cursor_state_half_life(float persistence);
float cursor_state_emission(float persistence, gint64 age_us);
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
