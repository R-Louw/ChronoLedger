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
    vec3 current = texture(u_current, uv).rgb;
    float current_luma = dot(current, vec3(0.2126, 0.7152, 0.0722));
    float emissive_gate = smoothstep(0.018, 0.18, current_luma);
    vec3 current_emissive = current * emissive_gate;

    vec3 previous = vec3(0.0);
    if (u_history_valid > 0.5) {
        vec2 dx = vec2(u_inverse_resolution.x, 0.0);
        vec2 dy = vec2(0.0, u_inverse_resolution.y);
        previous = texture(u_history, uv).rgb * 0.56;
        previous += texture(u_history, uv + dx).rgb * 0.11;
        previous += texture(u_history, uv - dx).rgb * 0.11;
        previous += texture(u_history, uv + dy).rgb * 0.11;
        previous += texture(u_history, uv - dy).rgb * 0.11;
        previous *= u_decay;
    }

    vec3 delta = current_emissive - previous;
    vec3 retained = 0.5 * (current_emissive + previous
                         + sqrt(delta * delta + vec3(0.000025))
                         - vec3(0.005));
    out_color = vec4(clamp(retained, 0.0, 1.0), 1.0);
}
