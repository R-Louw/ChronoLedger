in vec2 v_uv;
out vec4 out_color;
uniform sampler2D u_current;
uniform sampler2D u_history;
uniform vec2 u_inverse_resolution;
uniform float u_decay;
uniform float u_history_valid;
void main(void)
{
    vec2 uv = vec2(v_uv.x, 1.0 - v_uv.y);
    vec4 current = texture(u_current, uv);
    float current_luma = dot(current.rgb, vec3(0.2126, 0.7152, 0.0722));
    float emissive_gate = smoothstep(0.018, 0.18, current_luma);
    vec3 current_emissive = current.rgb * emissive_gate;
    float current_coverage = current.a * emissive_gate;
    vec4 previous = vec4(0.0);
    if (u_history_valid > 0.5) {
        vec2 dx = vec2(u_inverse_resolution.x, 0.0);
        vec2 dy = vec2(0.0, u_inverse_resolution.y);
        previous = texture(u_history, uv) * 0.56;
        previous += texture(u_history, uv + dx) * 0.11;
        previous += texture(u_history, uv - dx) * 0.11;
        previous += texture(u_history, uv + dy) * 0.11;
        previous += texture(u_history, uv - dy) * 0.11;
        previous *= u_decay;
    }
    vec3 delta = current_emissive - previous.rgb;
    vec3 retained_rgb = 0.5 * (current_emissive + previous.rgb
                             + sqrt(delta * delta + vec3(0.000025))
                             - vec3(0.005));
    float retained_alpha = max(current_coverage, previous.a);
    out_color = vec4(clamp(retained_rgb, 0.0, 1.0),
                     clamp(retained_alpha, 0.0, 1.0));
}
