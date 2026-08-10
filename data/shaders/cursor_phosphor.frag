in vec2 v_uv;
out vec4 out_color;
uniform sampler2D u_current;
uniform sampler2D u_history;
uniform vec2 u_inverse_resolution;
uniform float u_glow;
uniform float u_history_strength;
uniform float u_mask_strength;
uniform float u_vignette;
uniform float u_effects_enabled;
vec4 cursor_current(vec2 uv)
{
    return texture(u_current, uv);
}
vec4 cursor_retained(vec2 uv)
{
    vec4 now = cursor_current(uv);
    if (u_history_strength <= 0.0)
        return now;
    vec4 history = texture(u_history, uv);
    vec3 emission = max(now.rgb, history.rgb * u_history_strength);
    float alpha = max(now.a, history.a * u_history_strength);
    return vec4(emission, alpha);
}
vec3 cursor_straight(vec4 sample_value)
{
    return sample_value.rgb / max(sample_value.a, 0.0001);
}
void main(void)
{
    vec2 uv = vec2(v_uv.x, 1.0 - v_uv.y);
    if (u_effects_enabled < 0.5) {
        out_color = cursor_current(uv);
        return;
    }
    vec2 t = max(u_inverse_resolution, vec2(0.0000001));
    vec4 base = cursor_retained(uv);
    vec3 base_linear = pow(max(cursor_straight(base), vec3(0.0)), vec3(2.2));
    float base_luma = dot(base_linear, vec3(0.2126, 0.7152, 0.0722));
    float alpha = base.a;
    vec3 color = base_linear;
    if (u_glow > 0.0) {
        vec2 axial = t * 1.15;
        vec2 diagonal = t * 2.75;
        vec4 blur = vec4(0.0);
        /* Bloom is sourced from the current cursor scene, not recursively
         * from retained phosphor history. Precomputed offsets also avoid
         * repeating the same scalar multiplies for all eight taps. */
        blur += cursor_current(uv + vec2( axial.x, 0.0)) * 0.20;
        blur += cursor_current(uv + vec2(-axial.x, 0.0)) * 0.20;
        blur += cursor_current(uv + vec2(0.0,  axial.y)) * 0.20;
        blur += cursor_current(uv + vec2(0.0, -axial.y)) * 0.20;
        blur += cursor_current(uv + diagonal) * 0.075;
        blur += cursor_current(uv + vec2(-diagonal.x,  diagonal.y)) * 0.075;
        blur += cursor_current(uv + vec2( diagonal.x, -diagonal.y)) * 0.075;
        blur += cursor_current(uv - diagonal) * 0.075;
        vec3 blur_linear = pow(max(cursor_straight(blur), vec3(0.0)), vec3(2.2));
        float blur_luma = dot(blur_linear, vec3(0.2126, 0.7152, 0.0722));
        float bloom_gate = smoothstep(0.020, 0.24, max(blur_luma, base_luma));
        alpha = max(base.a, blur.a * u_glow * bloom_gate * 0.58);
        vec3 bloom = max(blur_linear - vec3(0.012), vec3(0.0));
        bloom = mix(bloom,
                    vec3(dot(bloom, vec3(0.2126, 0.7152, 0.0722))),
                    0.16);
        color += bloom * u_glow * bloom_gate * 1.55;
    }
    float hot_core = smoothstep(0.20, 0.72, base_luma);
    hot_core *= hot_core;
    float core_luma = base_luma * 1.55 + dot(color, vec3(0.2126, 0.7152, 0.0722)) * 0.18;
    /* Algebraic collapse of the fixed core tint mix. */
    vec3 bright_core = core_luma * vec3(0.94, 1.12, 1.024);
    color = mix(color, bright_core, hot_core * (0.18 + 0.68 * u_glow));
    if (u_mask_strength > 0.0001) {
        float triad = mod(gl_FragCoord.x, 3.0);
        vec3 mask = triad < 1.0 ? vec3(0.78, 1.08, 0.86)
                   : triad < 2.0 ? vec3(0.70, 1.18, 0.80)
                                  : vec3(0.84, 0.98, 1.10);
        float beam = triad < 1.0 ? 0.86 : (triad < 2.0 ? 1.06 : 0.90);
        float row_dim = mod(gl_FragCoord.y, 2.0) < 1.0 ? 1.0 : 0.92;
        color *= mix(vec3(1.0), mask * beam * row_dim,
                     u_mask_strength * 0.92);
    }
    if (u_vignette > 0.0) {
        vec2 centred = uv * 2.0 - 1.0;
        float radius2 = dot(centred, centred);
        float radial = smoothstep(0.16, 1.14, radius2);
        float edge = max(abs(centred.x), abs(centred.y));
        float corner = smoothstep(
            0.54, 1.18,
            edge + 0.34 * min(abs(centred.x), abs(centred.y)));
        float vignette = 1.0 - u_vignette * (0.58 * radial + 0.34 * corner);
        color *= clamp(vignette, 0.05, 1.0);
    }
    vec3 gamma_color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
    out_color = vec4(clamp(gamma_color * alpha, 0.0, 1.0),
                     clamp(alpha, 0.0, 1.0));
}
