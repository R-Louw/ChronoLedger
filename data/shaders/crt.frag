in vec2 v_uv;
out vec4 out_color;

uniform sampler2D u_scene;
uniform sampler2D u_history;
uniform vec2 u_inverse_resolution;
uniform float u_curvature;
uniform float u_glow;
uniform float u_vignette;
uniform float u_mask_strength;
uniform vec3 u_palette_low;
uniform vec3 u_palette_mid;
uniform vec3 u_palette_high;
uniform float u_palette_midpoint;
uniform float u_palette_mix;
uniform int u_palette_mode;
uniform float u_palette_spatial_mix;
uniform float u_history_enabled;
uniform float u_history_strength;

float palette_coordinate(float luma, vec2 uv)
{
    float intensity = smoothstep(0.002, 0.42, max(luma, 0.0));
    intensity = pow(intensity, 0.78);
    float spatial = intensity;
    if (u_palette_mode == 2)
        spatial = uv.x;
    else if (u_palette_mode == 3)
        spatial = 1.0 - uv.y;
    else if (u_palette_mode == 4)
        spatial = clamp(0.5 + 0.5 * (uv.x - uv.y), 0.0, 1.0);
    else if (u_palette_mode == 5 || u_palette_mode == 6) {
        float radial = clamp(length(uv - 0.5) * 1.42, 0.0, 1.0);
        spatial = u_palette_mode == 5 ? radial : 1.0 - radial;
    } else if (u_palette_mode == 7)
        spatial = clamp(0.5 + 0.5 * (uv.y - uv.x), 0.0, 1.0);
    return mix(intensity, spatial, clamp(u_palette_spatial_mix, 0.0, 1.0));
}

vec3 palette_color(float luma, vec2 uv)
{
    float t = palette_coordinate(luma, uv);
    float midpoint = clamp(u_palette_midpoint, 0.08, 0.92);
    float lower_width = max(midpoint * 0.42, 0.08);
    float upper_width = max((1.0 - midpoint) * 0.42, 0.08);
    vec3 low_to_mid = mix(u_palette_low, u_palette_mid,
                          smoothstep(midpoint - lower_width, midpoint, t));
    return mix(low_to_mid, u_palette_high,
               smoothstep(midpoint, midpoint + upper_width, t));
}

vec3 palette_match_luma(vec3 hue, float target_luma)
{
    float hue_luma = dot(hue, vec3(0.2126, 0.7152, 0.0722));
    float gain = min(target_luma / max(hue_luma, 0.045), 3.2);
    return hue * gain;
}

vec3 apply_palette(vec3 linear_color, vec2 uv)
{
    float luma = dot(linear_color, vec3(0.2126, 0.7152, 0.0722));
    vec3 mapped = palette_match_luma(palette_color(luma, uv), luma);
    return mix(mapped, linear_color, clamp(u_palette_mix, 0.0, 1.0));
}

vec2 curve_uv(vec2 uv, out float curve_load)
{
    vec2 p = uv * 2.0 - 1.0;
    vec2 p2 = p * p;
    vec2 p4 = p2 * p2;
    /* p^4 is required by both the warp and reconstruction load; calculate it
     * once so the curved path does not duplicate the same polynomial work. */
    curve_load = u_curvature * max(p4.x, p4.y);
    p *= 1.0 + u_curvature * p4.yx * vec2(0.13, 0.17);
    return p * 0.5 + 0.5;
}

vec3 read_scene(vec2 uv)
{
    return texture(u_scene, vec2(uv.x, 1.0 - uv.y)).rgb;
}

vec3 read_history(vec2 uv)
{
    return texture(u_history, vec2(uv.x, 1.0 - uv.y)).rgb;
}

vec4 catmull_rom_weights(float t)
{
    float t2 = t * t;
    float t3 = t2 * t;
    /* These are the four Catmull-Rom basis weights for offsets -1..2.
     * Precomputing them once per axis replaces twenty branchy scalar weight
     * evaluations in the 4x4 reconstruction loop. */
    return vec4(-0.5 * t + t2 - 0.5 * t3,
                1.0 - 2.5 * t2 + 1.5 * t3,
                0.5 * t + 2.0 * t2 - 1.5 * t3,
                -0.5 * t2 + 0.5 * t3);
}

vec3 sample_catmull_rom(vec2 uv)
{
    ivec2 size_i = textureSize(u_scene, 0);
    ivec2 max_pixel = size_i - ivec2(1);
    vec2 pixel = uv * vec2(size_i) - 0.5;
    ivec2 base = ivec2(floor(pixel));
    vec2 fraction = fract(pixel);
    vec4 weights_x = catmull_rom_weights(fraction.x);
    vec4 weights_y = catmull_rom_weights(fraction.y);
    vec3 accumulated = vec3(0.0);
    /* The 4x4 separable weight sum factors into the product of the two 1D
     * sums, avoiding one scalar accumulation for every texture tap. */
    float accumulated_weight = dot(weights_x, vec4(1.0))
                             * dot(weights_y, vec4(1.0));
    vec3 local_minimum = vec3(1.0);
    vec3 local_maximum = vec3(0.0);
    for (int y = -1; y <= 2; ++y) {
        float weight_y = weights_y[y + 1];
        int sample_y = clamp(base.y + y, 0, max_pixel.y);
        sample_y = max_pixel.y - sample_y;
        for (int x = -1; x <= 2; ++x) {
            float weight = weight_y * weights_x[x + 1];
            int sample_x = clamp(base.x + x, 0, max_pixel.x);
            vec3 sample_value = texelFetch(u_scene,
                                           ivec2(sample_x, sample_y), 0).rgb;
            accumulated += sample_value * weight;
            if (x >= 0 && x <= 1 && y >= 0 && y <= 1) {
                local_minimum = min(local_minimum, sample_value);
                local_maximum = max(local_maximum, sample_value);
            }
        }
    }
    vec3 reconstructed = accumulated / max(accumulated_weight, 0.00001);
    vec3 local_range = local_maximum - local_minimum;
    return clamp(reconstructed,
                 local_minimum - local_range * 0.04,
                 local_maximum + local_range * 0.04);
}

struct SceneSample {
    vec3 color;
    float detail;
};

SceneSample sample_reconstructed(vec2 uv, float curve_load)
{
    SceneSample result;
    vec3 bilinear = read_scene(uv);
    vec3 reconstructed = bilinear;
    if (curve_load > 0.0060) {
        vec3 catmull = sample_catmull_rom(uv);
        float reconstruction_mix = smoothstep(0.0040, 0.040, curve_load);
        reconstructed = mix(bilinear, catmull, reconstruction_mix);
    }
    vec3 screen_derivative = fwidth(reconstructed);
    float edge = max(max(screen_derivative.r, screen_derivative.g),
                     screen_derivative.b);
    float gate = smoothstep(0.0020, 0.070, edge);
    vec3 reconstruction_detail = reconstructed - bilinear;
    float sharpening = clamp(curve_load * 0.14, 0.0, 0.08) * gate;
    result.color = clamp(reconstructed + reconstruction_detail * sharpening,
                         0.0, 1.0);
    result.detail = gate;
    return result;
}

void main(void)
{
    vec2 uv = v_uv;
    vec3 scene_color;
    float reconstructed_detail = -1.0;
    if (u_curvature > 0.0001) {
        float curve_load;
        uv = curve_uv(v_uv, curve_load);
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
            out_color = vec4(0.0010, 0.0022, 0.0018, 1.0);
            return;
        }
        SceneSample curved_sample = sample_reconstructed(uv, curve_load);
        scene_color = curved_sample.color;
        reconstructed_detail = curved_sample.detail;
    } else {
        scene_color = read_scene(uv);
    }
    vec3 base = scene_color;
    if (u_history_enabled > 0.5) {
        vec3 history = read_history(uv);
        base = mix(base, max(base, history), clamp(u_history_strength, 0.0, 1.0));
    }

    vec3 base_linear = pow(max(base, vec3(0.0)), vec3(2.2));
    vec3 color = base_linear;
    float source_luma = dot(base_linear, vec3(0.2126, 0.7152, 0.0722));
    float detail_gate = reconstructed_detail;
    if (detail_gate < 0.0) {
        vec3 derivative = fwidth(base_linear);
        float edge = max(max(derivative.r, derivative.g), derivative.b);
        detail_gate = smoothstep(0.002, 0.070, edge);
    }
    float detail_protection = detail_gate
                            * smoothstep(0.003, 0.20, source_luma);

    if (u_glow > 0.0001) {
        vec2 texel = u_inverse_resolution;
        vec2 axial = texel * 1.15;
        vec2 diagonal = texel * 2.75;
        vec3 blur = vec3(0.0);
        blur += read_scene(uv + vec2( axial.x, 0.0)) * 0.20;
        blur += read_scene(uv + vec2(-axial.x, 0.0)) * 0.20;
        blur += read_scene(uv + vec2(0.0,  axial.y)) * 0.20;
        blur += read_scene(uv + vec2(0.0, -axial.y)) * 0.20;
        blur += read_scene(uv + diagonal) * 0.075;
        blur += read_scene(uv + vec2(-diagonal.x,  diagonal.y)) * 0.075;
        blur += read_scene(uv + vec2( diagonal.x, -diagonal.y)) * 0.075;
        blur += read_scene(uv - diagonal) * 0.075;
        vec3 blur_linear = pow(max(blur, vec3(0.0)), vec3(2.2));
        float blur_luma = dot(blur_linear, vec3(0.2126, 0.7152, 0.0722));
        float bloom_gate = smoothstep(0.020, 0.24, max(blur_luma, source_luma));
        vec3 bloom_energy = max(blur_linear - vec3(0.012), vec3(0.0));
        vec3 bloom_color = mix(bloom_energy,
                               vec3(dot(bloom_energy, vec3(0.2126, 0.7152, 0.0722))),
                               0.16);
        color += bloom_color * u_glow * bloom_gate * 1.55;
    }

    color = apply_palette(color, uv);
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float hot_core = smoothstep(0.20, 0.72, source_luma);
    hot_core *= hot_core;
    float core_luma = source_luma * 1.55 + luminance * 0.18;
    vec3 palette_core = palette_match_luma(u_palette_high, core_luma);
    float white_mix = u_palette_mode == 1 ? 0.52 : 0.16;
    vec3 bright_core = mix(palette_core, vec3(core_luma), white_mix);
    color = mix(color, bright_core, hot_core * (0.18 + 0.68 * u_glow));

    if (u_mask_strength > 0.0001) {
        float triad = mod(gl_FragCoord.x, 3.0);
        vec3 mask = triad < 1.0 ? vec3(0.78, 1.08, 0.86)
                   : triad < 2.0 ? vec3(0.70, 1.18, 0.80)
                                  : vec3(0.84, 0.98, 1.10);
        float beam = triad < 1.0 ? 0.86 : (triad < 2.0 ? 1.06 : 0.90);
        float row_dim = mod(gl_FragCoord.y, 2.0) < 1.0 ? 1.0 : 0.92;
        float mask_protection = mix(1.0, 0.58, detail_protection);
        color *= mix(vec3(1.0), mask * beam * row_dim,
                     u_mask_strength * 0.92 * mask_protection);
    }

    if (u_vignette > 0.0) {
        vec2 centred = uv * 2.0 - 1.0;
        float radius2 = dot(centred, centred);
        float radial_falloff = smoothstep(0.16, 1.14, radius2);
        float edge_shape = max(abs(centred.x), abs(centred.y));
        float corner_falloff = smoothstep(
            0.54, 1.18,
            edge_shape + 0.34 * min(abs(centred.x), abs(centred.y)));
        float vignette = 1.0 - u_vignette *
            (0.58 * radial_falloff + 0.34 * corner_falloff);
        color *= clamp(vignette, 0.05, 1.0);
    }

    /* Clarity restoration contributes exactly zero away from detected detail.
     * Keep its second gamma conversion and palette mapping off flat pixels. */
    if (detail_protection > 0.0) {
        vec3 clean_linear = base_linear;
        if (u_history_enabled > 0.5)
            clean_linear = pow(max(scene_color, vec3(0.0)), vec3(2.2));
        vec3 clean_tinted = apply_palette(clean_linear, uv);
        float effect_pressure = clamp(u_curvature * 0.50 + u_glow * 0.18
                                    + u_history_strength * 0.22
                                    + u_vignette * 0.14 + u_mask_strength * 0.22,
                                      0.0, 1.0);
        float clarity_restore = detail_protection *
            (0.30 + 0.38 * effect_pressure);
        vec3 clarity_floor = clean_tinted * mix(0.72, 0.90, effect_pressure);
        color = mix(color, max(color, clarity_floor), clarity_restore);
    }
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
    out_color = vec4(color, 1.0);
}
