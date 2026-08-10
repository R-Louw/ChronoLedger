in vec2 v_uv;
out vec4 out_color;
uniform sampler2D u_source;
void main(void)
{
    vec4 raw = texture(u_source, v_uv);
    out_color = raw.bgra;
}
