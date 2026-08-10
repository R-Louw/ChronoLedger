in vec2 v_uv;
out vec4 out_color;
uniform sampler2D u_source;
uniform vec2 u_inverse_resolution;
uniform float u_strength;
vec4 cursor_sample(vec2 uv)
{
    return texture(u_source, uv);
}
void main(void)
{
    vec2 uv = vec2(v_uv.x, 1.0 - v_uv.y);
    vec2 t = max(u_inverse_resolution, vec2(0.0000001));
    vec4 c = cursor_sample(uv);
    if (u_strength <= 0.0001) {
        out_color = c;
        return;
    }
    vec2 dx = vec2(t.x, 0.0);
    vec2 dy = vec2(0.0, t.y);
    vec4 n = cursor_sample(uv + dy);
    vec4 s = cursor_sample(uv - dy);
    vec4 e = cursor_sample(uv + dx);
    vec4 w = cursor_sample(uv - dx);
    vec4 ne = cursor_sample(uv + t);
    vec4 nw = cursor_sample(uv + vec2(-t.x, t.y));
    vec4 se = cursor_sample(uv + vec2(t.x, -t.y));
    vec4 sw = cursor_sample(uv - t);
    vec4 filtered = (c * 4.0 + (n + s + e + w) * 1.25
                   + (ne + nw + se + sw) * 0.50) / 11.0;
    float edge = max(max(abs(c.a - n.a), abs(c.a - s.a)),
                     max(abs(c.a - e.a), abs(c.a - w.a)));
    edge = max(edge, max(max(abs(c.a - ne.a), abs(c.a - nw.a)),
                         max(abs(c.a - se.a), abs(c.a - sw.a))));
    float blend = smoothstep(0.015, 0.42, edge) * 0.78 * u_strength;
    out_color = clamp(mix(c, filtered, blend), 0.0, 1.0);
}
