#pragma once

#include <epoxy/gl.h>
#include <glib.h>

#include "app_types.h"

typedef struct {
    float x;
    float y;
    float r;
    float g;
    float b;
    float a;
} GeometryVertex;

struct _Renderer {
    GLuint program;
    GLuint persistence_program;
    GLuint composite_program;
    GLuint aa_program;
    GLuint geometry_program;
    GLuint cursor_aa_program;
    GLuint cursor_persistence_program;
    GLuint cursor_phosphor_program;
    GLuint vao;
    GLuint geometry_vao;
    GLuint geometry_vbo;
    GLuint scene_texture;
    GLuint composed_texture;
    GLuint composed_fbo;
    GLuint aa_texture;
    GLuint aa_refine_texture;
    GLuint aa_fbo;
    GLuint aa_refine_fbo;
    GLuint text_mask_texture;
    GLuint filtered_scene_texture;
    GLuint final_texture;
    GLuint final_fbo;
    GLuint history_textures[2];
    GLuint history_fbo;
    GLuint cursor_scene_texture;
    GLuint cursor_aa_texture;
    GLuint cursor_history_textures[2];
    GLuint cursor_scene_fbo;
    GLuint cursor_aa_fbo;
    GLuint cursor_history_fbo;
    GLuint upload_pbos[CHRONO_UPLOAD_PBO_COUNT];
    GLuint text_mask_pbos[CHRONO_UPLOAD_PBO_COUNT];
    unsigned int upload_pbo_index;
    unsigned int text_mask_pbo_index;
    GArray *geometry_vertices;
    int texture_width;
    int texture_height;
    int composed_width;
    int composed_height;
    int aa_width;
    int aa_height;
    int text_mask_width;
    int text_mask_height;
    int final_width;
    int final_height;
    int history_width;
    int history_height;
    int history_read_index;
    int cursor_width;
    int cursor_height;
    int cursor_history_read_index;
    int cursor_damage_x0;
    int cursor_damage_y0;
    int cursor_damage_x1;
    int cursor_damage_y1;
    GLint max_texture_size;

    GLint u_scene;
    GLint u_history;
    GLint u_inverse_resolution;
    GLint u_curvature;
    GLint u_glow;
    GLint u_vignette;
    GLint u_mask_strength;
    GLint u_palette_low;
    GLint u_palette_mid;
    GLint u_palette_high;
    GLint u_palette_midpoint;
    GLint u_palette_mix;
    GLint u_palette_mode;
    GLint u_palette_spatial_mix;
    GLint u_history_enabled;
    GLint u_history_strength;

    GLint c_source;
    GLint a_source;
    GLint a_original;
    GLint a_text_mask;
    GLint a_inverse_resolution;
    GLint a_quality;
    GLint a_strength;
    GLint a_selective;
    GLint a_refinement_pass;
    GLint p_current;
    GLint p_history;
    GLint p_decay;
    GLint p_history_valid;
    GLint p_inverse_resolution;

    gboolean is_gles;
    gboolean history_valid;
    gboolean history_reset_requested;
    gboolean cursor_history_valid;
    gboolean cursor_history_reset_requested;
    gboolean pbo_upload_enabled;
    gboolean text_mask_pbo_upload_enabled;
    gboolean nvidia_gpu;
    gboolean filtered_scene_valid;
    gboolean final_scene_valid;
    gint64 last_crt_update_us;
    double crt_frame_delta;
    gboolean ready;
    char *vendor_string;
    char *renderer_string;
};
