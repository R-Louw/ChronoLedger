#include "renderer_internal.h"

#include <math.h>

void
renderer_internal_clear_gl_errors(void)
{
    for (int i = 0; i < 32 && glGetError() != GL_NO_ERROR; ++i) {
    }
}

gboolean
renderer_internal_size_to_gl_sizeiptr(gsize size, GLsizeiptr *out)
{
    if (size > (gsize)G_MAXSSIZE)
        return FALSE;
    *out = (GLsizeiptr)size;
    return TRUE;
}

gboolean
renderer_internal_geometry_upload_arguments(const GArray *vertices,
                          GLsizeiptr *bytes_out,
                          GLsizei *count_out)
{
    if (vertices == NULL)
        return FALSE;
    gsize maximum_vertex_count = MIN((gsize)G_MAXINT,
                                     G_MAXSIZE / sizeof(GeometryVertex));
    if ((gsize)vertices->len > maximum_vertex_count)
        return FALSE;
    gsize bytes = (gsize)vertices->len * sizeof(GeometryVertex);
    if (!renderer_internal_size_to_gl_sizeiptr(bytes, bytes_out))
        return FALSE;
    *count_out = (GLsizei)vertices->len;
    return TRUE;
}

float
renderer_internal_unit_float_or_zero(float value)
{
    return isfinite(value) ? CLAMP(value, 0.0f, 1.0f) : 0.0f;
}

gint64
renderer_internal_nonnegative_elapsed_us(gint64 end_us, gint64 start_us)
{
    if (end_us <= start_us)
        return 0;
    if (start_us < 0 && end_us > G_MAXINT64 + start_us)
        return G_MAXINT64;
    return end_us - start_us;
}
