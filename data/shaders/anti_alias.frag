in vec2 v_uv;
out vec4 out_color;
uniform sampler2D u_source;
uniform sampler2D u_original;
uniform sampler2D u_text_mask;
uniform vec2 u_inverse_resolution;
uniform int u_quality;
uniform float u_strength;
uniform int u_selective;
uniform int u_refinement_pass;

float aa_luma(vec3 color)
{
    return dot(color, vec3(0.299, 0.587, 0.114));
}

vec3 aa_sample(vec2 uv)
{
    return texture(u_source, uv).rgb;
}

vec3 aa_original(vec2 uv)
{
    return texture(u_original, uv).rgb;
}

float aa_text_coverage(vec2 uv, vec2 texel)
{
    vec2 mask_uv = vec2(uv.x, 1.0 - uv.y);
    float coverage = texture(u_text_mask, mask_uv).r;
    coverage = max(coverage, texture(u_text_mask, mask_uv + vec2( texel.x, 0.0)).r);
    coverage = max(coverage, texture(u_text_mask, mask_uv + vec2(-texel.x, 0.0)).r);
    coverage = max(coverage, texture(u_text_mask, mask_uv + vec2(0.0,  texel.y)).r);
    coverage = max(coverage, texture(u_text_mask, mask_uv + vec2(0.0, -texel.y)).r);
    coverage = max(coverage, texture(u_text_mask, mask_uv + vec2( texel.x,  texel.y)).r);
    coverage = max(coverage, texture(u_text_mask, mask_uv + vec2(-texel.x,  texel.y)).r);
    coverage = max(coverage, texture(u_text_mask, mask_uv + vec2( texel.x, -texel.y)).r);
    coverage = max(coverage, texture(u_text_mask, mask_uv + vec2(-texel.x, -texel.y)).r);
    return coverage;
}

vec3 aa_to_linear(vec3 color)
{
    return color * color;
}

vec3 aa_to_gamma(vec3 color)
{
    return sqrt(max(color, vec3(0.0)));
}

vec3 aa_finalize(vec2 uv, vec2 texel, vec3 filtered)
{
    if (u_selective == 0)
        return filtered;
    float coverage = aa_text_coverage(uv, texel);
    float preserve = smoothstep(0.015, 0.30, coverage);
    return mix(filtered, aa_original(uv), preserve);
}

float aa_similarity(float a, float b, float low, float high)
{
    return 1.0 - smoothstep(low, high, abs(a - b));
}

float aa_corner_score(float center,
                      float axial_a,
                      float axial_b,
                      float diagonal,
                      float support,
                      float threshold,
                      float contrast_high,
                      float similarity_low,
                      float similarity_high)
{
    float consistency = min(aa_similarity(axial_a, axial_b,
                                          similarity_low, similarity_high),
                            min(aa_similarity(axial_a, diagonal,
                                              similarity_low, similarity_high),
                                aa_similarity(axial_b, diagonal,
                                              similarity_low, similarity_high)));
    float opposite_mean = (axial_a + axial_b + diagonal) / 3.0;
    float contrast = smoothstep(threshold, contrast_high,
                                abs(center - opposite_mean));
    float side_ab = step(0.0, (axial_a - center) * (axial_b - center));
    float side_ad = step(0.0, (axial_a - center) * (diagonal - center));
    return consistency * contrast * side_ab * side_ad * support;
}

void main(void)
{
    vec2 uv = vec2(v_uv.x, 1.0 - v_uv.y);
    vec2 texel = max(u_inverse_resolution, vec2(0.0000001));

    if (u_quality == 4 && u_refinement_pass == 0) {
        if (aa_text_coverage(uv, texel) > 0.012) {
            out_color = vec4(0.0);
            return;
        }

        float luma_m  = aa_luma(aa_sample(uv));
        float luma_n  = aa_luma(aa_sample(uv + vec2(0.0,  texel.y)));
        float luma_s  = aa_luma(aa_sample(uv + vec2(0.0, -texel.y)));
        float luma_e  = aa_luma(aa_sample(uv + vec2( texel.x, 0.0)));
        float luma_w  = aa_luma(aa_sample(uv + vec2(-texel.x, 0.0)));
        float luma_ne = aa_luma(aa_sample(uv + vec2( texel.x,  texel.y)));
        float luma_nw = aa_luma(aa_sample(uv + vec2(-texel.x,  texel.y)));
        float luma_se = aa_luma(aa_sample(uv + vec2( texel.x, -texel.y)));
        float luma_sw = aa_luma(aa_sample(uv + vec2(-texel.x, -texel.y)));

        float local_max = max(luma_m,
                              max(max(max(luma_n, luma_s), max(luma_e, luma_w)),
                                  max(max(luma_ne, luma_nw), max(luma_se, luma_sw))));
        float local_min = min(luma_m,
                              min(min(min(luma_n, luma_s), min(luma_e, luma_w)),
                                  min(min(luma_ne, luma_nw), min(luma_se, luma_sw))));
        float threshold = max(0.020, local_max * 0.082);
        if (local_max - local_min < threshold) {
            out_color = vec4(0.0);
            return;
        }

        /* Every corner uses the same similarity thresholds.  Hoist them and
         * reuse centre-to-cardinal similarities shared by two corner tests. */
        float similarity_low = threshold * 0.55;
        float similarity_high = threshold * 1.85;
        float contrast_high = threshold * 3.25;
        float similar_n = aa_similarity(luma_m, luma_n,
                                        similarity_low, similarity_high);
        float similar_s = aa_similarity(luma_m, luma_s,
                                        similarity_low, similarity_high);
        float similar_e = aa_similarity(luma_m, luma_e,
                                        similarity_low, similarity_high);
        float similar_w = aa_similarity(luma_m, luma_w,
                                        similarity_low, similarity_high);
        float support_se = max(similar_w, max(similar_n,
            aa_similarity(luma_m, luma_nw, similarity_low, similarity_high)));
        float support_sw = max(similar_e, max(similar_n,
            aa_similarity(luma_m, luma_ne, similarity_low, similarity_high)));
        float support_ne = max(similar_w, max(similar_s,
            aa_similarity(luma_m, luma_sw, similarity_low, similarity_high)));
        float support_nw = max(similar_e, max(similar_s,
            aa_similarity(luma_m, luma_se, similarity_low, similarity_high)));

        float score_se = aa_corner_score(luma_m, luma_e, luma_s, luma_se,
                                         support_se, threshold, contrast_high,
                                         similarity_low, similarity_high);
        float score_sw = aa_corner_score(luma_m, luma_w, luma_s, luma_sw,
                                         support_sw, threshold, contrast_high,
                                         similarity_low, similarity_high);
        float score_ne = aa_corner_score(luma_m, luma_e, luma_n, luma_ne,
                                         support_ne, threshold, contrast_high,
                                         similarity_low, similarity_high);
        float score_nw = aa_corner_score(luma_m, luma_w, luma_n, luma_nw,
                                         support_nw, threshold, contrast_high,
                                         similarity_low, similarity_high);
        out_color = vec4(score_se, score_sw, score_ne, score_nw);
        return;
    }

    if (u_quality == 4) {
        vec4 scores = texture(u_source, uv);
        float peak = max(max(scores.r, scores.g), max(scores.b, scores.a));
        vec3 center = aa_original(uv);
        if (peak < 0.012) {
            out_color = vec4(aa_finalize(uv, texel, center), 1.0);
            return;
        }

        vec3 north = aa_original(uv + vec2(0.0,  texel.y));
        vec3 south = aa_original(uv + vec2(0.0, -texel.y));
        vec3 east  = aa_original(uv + vec2( texel.x, 0.0));
        vec3 west  = aa_original(uv + vec2(-texel.x, 0.0));
        vec3 ne = aa_original(uv + vec2( texel.x,  texel.y));
        vec3 nw = aa_original(uv + vec2(-texel.x,  texel.y));
        vec3 se = aa_original(uv + vec2( texel.x, -texel.y));
        vec3 sw = aa_original(uv + vec2(-texel.x, -texel.y));

        vec3 candidate = vec3(0.0);
        float score_sum = dot(scores, vec4(1.0));
        candidate += (aa_to_linear(east) + aa_to_linear(south) + aa_to_linear(se))
                   * (scores.r / 3.0);
        candidate += (aa_to_linear(west) + aa_to_linear(south) + aa_to_linear(sw))
                   * (scores.g / 3.0);
        candidate += (aa_to_linear(east) + aa_to_linear(north) + aa_to_linear(ne))
                   * (scores.b / 3.0);
        candidate += (aa_to_linear(west) + aa_to_linear(north) + aa_to_linear(nw))
                   * (scores.a / 3.0);
        candidate /= max(score_sum, 0.0001);

        float topology_gate = smoothstep(0.10, 0.78, peak);
        float blend = clamp(u_strength * 0.265 * topology_gate, 0.0, 0.265);
        vec3 result = aa_to_gamma(mix(aa_to_linear(center), candidate, blend));
        result = aa_finalize(uv, texel, clamp(result, 0.0, 1.0));
        out_color = vec4(result, 1.0);
        return;
    }

    vec3 rgb_m = aa_sample(uv);
    if (u_quality <= 0) {
        out_color = vec4(aa_finalize(uv, texel, rgb_m), 1.0);
        return;
    }

    vec3 rgb_nw = aa_sample(uv + vec2(-texel.x,  texel.y));
    vec3 rgb_ne = aa_sample(uv + vec2( texel.x,  texel.y));
    vec3 rgb_sw = aa_sample(uv + vec2(-texel.x, -texel.y));
    vec3 rgb_se = aa_sample(uv + vec2( texel.x, -texel.y));

    float luma_m = aa_luma(rgb_m);
    float luma_nw = aa_luma(rgb_nw);
    float luma_ne = aa_luma(rgb_ne);
    float luma_sw = aa_luma(rgb_sw);
    float luma_se = aa_luma(rgb_se);
    float luma_min = min(luma_m, min(min(luma_nw, luma_ne), min(luma_sw, luma_se)));
    float luma_max = max(luma_m, max(max(luma_nw, luma_ne), max(luma_sw, luma_se)));
    float luma_range = luma_max - luma_min;
    float threshold = max(0.026, luma_max * 0.095);
    if (u_refinement_pass != 0)
        threshold *= 1.18;
    if (luma_range < threshold) {
        out_color = vec4(aa_finalize(uv, texel, rgb_m), 1.0);
        return;
    }

    vec2 direction;
    direction.x = -((luma_nw + luma_ne) - (luma_sw + luma_se));
    direction.y =  ((luma_nw + luma_sw) - (luma_ne + luma_se));
    float direction_reduce = max((luma_nw + luma_ne + luma_sw + luma_se)
                                 * (0.25 * 0.125), 0.0078125);
    float inverse_minimum = 1.0 / (min(abs(direction.x), abs(direction.y))
                                   + direction_reduce);
    float span_max = u_quality > 1 ? 8.0 : 4.0;
    direction = clamp(direction * inverse_minimum,
                      vec2(-span_max), vec2(span_max)) * texel;

    vec3 rgb_a0 = aa_sample(uv + direction * (1.0 / 3.0 - 0.5));
    vec3 rgb_a1 = aa_sample(uv + direction * (2.0 / 3.0 - 0.5));
    vec3 linear_a = 0.5 * (aa_to_linear(rgb_a0) + aa_to_linear(rgb_a1));
    vec3 candidate_linear = linear_a;

    if (u_quality > 1) {
        vec3 rgb_b0 = aa_sample(uv + direction * -0.5);
        vec3 rgb_b1 = aa_sample(uv + direction *  0.5);
        vec3 linear_b = linear_a * 0.5
                      + (aa_to_linear(rgb_b0) + aa_to_linear(rgb_b1)) * 0.25;
        float luma_b = aa_luma(aa_to_gamma(linear_b));
        candidate_linear = (luma_b < luma_min || luma_b > luma_max)
                         ? linear_a : linear_b;
    }

    float edge_gate = smoothstep(threshold, max(threshold * 3.5, 0.085),
                                 luma_range);
    float neighbour_peak = max(max(luma_nw, luma_ne), max(luma_sw, luma_se));
    float isolated_core = smoothstep(0.08, 0.52,
                                     max(luma_m - neighbour_peak, 0.0)
                                     / max(luma_range, 0.0001));
    float core_guard = u_selective != 0 ? 0.76 : 0.52;
    float blend = clamp(u_strength * edge_gate * mix(1.0, core_guard, isolated_core),
                        0.0, 1.0);
    vec3 result = aa_to_gamma(mix(aa_to_linear(rgb_m), candidate_linear, blend));
    result = aa_finalize(uv, texel, clamp(result, 0.0, 1.0));
    out_color = vec4(result, 1.0);
}
