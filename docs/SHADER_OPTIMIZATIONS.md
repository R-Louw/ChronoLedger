# Shader optimizations

## Scope

Reviewed every shader in `data/shaders/` plus the renderer code that supplies shader uniforms and texture state. The goal was to reduce redundant ALU, branches, texture work, and per-fragment invariant computation without materially changing the rendered image.

No changes were needed in `composite.frag`, `geometry.vert`, or `geometry.frag`; they are already essentially minimal.

## Changes

### `crt.frag`

- Replaced `u_resolution` with `u_inverse_resolution`; the CPU now computes the reciprocal dimensions once per draw instead of the fragment shader doing two reciprocals per fragment when glow is enabled.
- Reused the `p^4` curvature term for both UV bending and `curve_load`, eliminating duplicated polynomial work.
- Replaced the branchy scalar Catmull-Rom weight function with the four closed-form basis weights evaluated once per axis. The old 4x4 reconstruction source evaluated the scalar weight function 20 times (4 Y + 16 X); the new path evaluates two vector bases.
- Hoisted Catmull-Rom Y coordinate clamping/flipping out of the inner X loop: four Y calculations instead of sixteen.
- Factored the separable 4x4 normalization weight sum into the product of the two 1D sums, removing sixteen per-tap scalar weight accumulations.
- Removed repeated normalized-UV `clamp()` calls from filtered texture sampling. Renderer textures are configured `GL_CLAMP_TO_EDGE`, which provides the same edge behavior. Integer `texelFetch()` coordinates remain explicitly clamped because sampler wrap modes do not apply to `texelFetch()`.
- Precomputed bloom sample offsets (`1.15 * texel`, `2.75 * texel`) once rather than for every tap.
- Removed a dead luminance `dot()` whose result was immediately overwritten.
- Simplified diagonal palette coordinates algebraically and shares one radial-distance calculation between radial and inverse-radial modes.
- Skips vignette math when vignette is exactly disabled.
- Skips the expensive clarity-restoration gamma conversion and second palette mapping on pixels where `detail_protection` is exactly zero. When history is disabled, the clarity path also reuses the already-computed `base_linear` value instead of repeating `pow(..., 2.2)`.

### `anti_alias.frag`

- Removed redundant normalized-UV clamps for textures whose sampler state is `GL_CLAMP_TO_EDGE`.
- Hoisted Ultra-mode similarity thresholds out of each `aa_similarity()` call.
- Reused center-to-cardinal similarity values shared by two corner tests. Ultra topology classification drops from 24 to 20 smoothstep-based similarity evaluations at source level.
- Removed the final corner-score clamp because all score factors are already bounded to `[0, 1]` (`smoothstep`, `step`, and bounded similarities), so their product is bounded as well.

### Cursor shaders

- `cursor_aa.frag`: removed redundant sampler clamps and reuses X/Y texel offset vectors.
- `cursor_phosphor.frag`: avoids the history texture fetch entirely when history strength is zero; avoids all eight bloom texture taps plus bloom gamma/math when glow is exactly zero; precomputes bloom offsets; skips vignette math when disabled.
- Collapsed the fixed cursor hot-core tint expression to `core_luma * vec3(0.94, 1.12, 1.024)`.
- `cursor_persistence.frag`: inverse history dimensions are supplied once per draw instead of using `textureSize()` plus reciprocal in every fragment; the five history taps are skipped on a reset frame where history validity is zero.

### Main persistence shader

- Same inverse-resolution uniform optimization as cursor persistence.
- Reuses one UV value for current/history sampling.
- Skips all five history taps on the first/reset frame where `u_history_valid == 0`.

### Full-screen vertex shader

- Collapsed the two-step UV transform into `position * vec2(0.5, -0.5) + 0.5`.

### Renderer-side support

- Added/cached the new inverse-resolution uniforms for CRT and main persistence.
- Supplies inverse dimensions once per draw for CRT, main persistence, and cursor persistence.
- Documented the invariant that normalized shader sampling relies on the renderer's `GL_CLAMP_TO_EDGE` texture state.

## Wolfram verification

Wolfram Language was used to verify the algebraic transformations before patching:

- The four closed-form Catmull-Rom basis expressions exactly equal the previous piecewise polynomial weights for `0 <= t <= 1` (difference `{0,0,0,0}`).
- The four 1D Catmull-Rom basis weights sum to exactly `1` symbolically.
- The 4x4 separable normalization sum equals `Sum[wx] * Sum[wy]` exactly.
- Both diagonal palette coordinate rewrites simplify to zero difference from the original expressions.
- The full-screen Y UV rewrite simplifies to zero difference.
- The cursor fixed core-tint mix simplifies to `{0.94, 1.12, 1.024}`.

## Shader compiler/link validation

The optimized shader set was compiled and linked using Mesa 25.0.7 software OpenGL in both runtime profiles used by ChronoLedger:

- Desktop: OpenGL 4.5 compatibility context, shader header `#version 130` — all 10 shaders compile; all fragment/vertex program pairs link.
- GLES: OpenGL ES 3.2 context, shader header `#version 300 es` with the same precision declarations used by `renderer_shaders.c` — all 10 shaders compile; all program pairs link.
- Required CRT, AA, persistence, and cursor-persistence uniforms remained active/non-negative after linking.

## Differential rendering validation

Baseline and optimized shaders were rendered through Mesa into RGBA32F targets using deterministic synthetic scene/history/mask textures and `GL_CLAMP_TO_EDGE`, then compared channel-by-channel.

- Anti-aliasing: FXAA off/normal/high and both Ultra passes were bit-identical in the test (`max abs diff = 0`).
- Cursor AA: strengths `0`, `0.48`, `0.78`, `1.0` were bit-identical (`max abs diff = 0`).
- CRT: palette modes 1, 4, 5, 6, 7 plus no-curvature/no-glow/no-vignette/no-history cases had maximum absolute channel difference `4.47e-7`; no channel differed by more than `1e-4`.
- Cursor phosphor: maximum absolute channel difference `5.96e-8`; no channel differed by more than `1e-5`.
- Main persistence, including a half-resolution history target: maximum absolute channel difference `5.96e-8`.
- Cursor persistence: maximum absolute channel difference `5.96e-8`.

The nonzero differences are sub-ULP/order-of-evaluation effects from algebraically equivalent floating-point expressions; they are far below an 8-bit output step (`1/255 ~= 3.92e-3`).

## Caveat

These changes are structurally cheaper, but exact GPU speedup depends on the driver/compiler and hardware. A modern driver may already CSE or hoist some of the original expressions. The largest likely wins are the Catmull-Rom weight/loop cleanup, conditional clarity/persistence/cursor work, and removal of explicit per-sample clamps/reciprocals. Measure with your target GPU's timer queries or profiler if you want hardware-specific percentages.
