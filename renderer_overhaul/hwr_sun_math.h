#ifndef HWR_SUN_MATH_H
#define HWR_SUN_MATH_H

#include "hwr_gl.h"

#include <math.h>
#include <stdint.h>

/* The sun/moon policy arithmetic, in one header of inline functions on
 * purpose: the renderer, the offline light-space twin and the negative
 * controls all compile exactly these lines, so what the gates hold is what
 * the game runs rather than a transcription of it.
 *
 * THE YAW AXIS CONVENTION, pinned here (maps were authored with no sun, so
 * this is a definition rather than a discovery -- the debug readout exists so
 * it can be checked against a map's own windows in-game):
 *
 *   - Map north is world +Y; map east is world +X; world Z is up.
 *   - `yaw_deg` is the compass bearing of where the sun or moon STANDS in
 *     the sky, clockwise from north looking down at the map: 0 = due north,
 *     90 = due east, 180 = due south, 270 = due west.
 *   - `elevation_deg` is its angle above the horizon.
 *
 * The direction handed around is the light's direction of TRAVEL, from the
 * sky toward the ground, so its Z component is negative. */

/* Degrees to radians without depending on a nonstandard M_PI. */
#define HWR_SUNM_DEG (3.14159265358979323846 / 180.0)

/* How far the camera may drift from the held window centre before the window
 * re-centres, as a share of the span. Snapping to whole texels is what stops
 * static shadow edges swimming as the player walks; the guard band is what
 * keeps the re-centre itself rare. */
#define HWR_SUNM_GUARD_SHARE (1.0f / 8.0f)

/* The constant lookup bias, in light-space texels of world size. Two texels
 * plus one world unit: zero-thickness walls have no back face to hide acne
 * on, so the constant term carries what the caster pass's slope-scaled
 * polygon offset does not. */
#define HWR_SUNM_BIAS_TEXELS 2.0f
#define HWR_SUNM_BIAS_WORLD 1.0f

static inline void hwr_sunm_direction(float yaw_deg, float elevation_deg,
                                      float out[3])
{
    double yaw = (double)yaw_deg * HWR_SUNM_DEG;
    double elevation = (double)elevation_deg * HWR_SUNM_DEG;
    double flat = cos(elevation);

    /* Toward the sun, then negated: the light travels the other way. */
    out[0] = (float)-(sin(yaw) * flat);
    out[1] = (float)-(cos(yaw) * flat);
    out[2] = (float)-sin(elevation);
}

/* The light's own orthonormal basis: `right` horizontal (the elevation range
 * 5..85 keeps the horizontal component real), `up` completing it. Both unit,
 * both perpendicular to the direction of travel. */
static inline void hwr_sunm_basis(const float dir[3], float right[3],
                                  float up[3])
{
    float horizontal = sqrtf(dir[0] * dir[0] + dir[1] * dir[1]);

    if (horizontal < 1e-6f) {
        right[0] = 1.0f;
        right[1] = 0.0f;
    } else {
        right[0] = dir[1] / horizontal;
        right[1] = -dir[0] / horizontal;
    }
    right[2] = 0.0f;
    /* up = right x dir */
    up[0] = right[1] * dir[2] - right[2] * dir[1];
    up[1] = right[2] * dir[0] - right[0] * dir[2];
    up[2] = right[0] * dir[1] - right[1] * dir[0];
}

/* The moving window: a span-sized square about a held centre in the light
 * plane, the centre snapped to whole light-space texels. Held in the light
 * plane's own coordinates (dot products against `right` and `up`); the depth
 * centre follows the camera's projection on the travel axis. */
struct hwr_sunm_window {
    float center_a;
    float center_b;
    float center_d;
    float span;
    float depth_half;
    float texel;
    int valid;
};

static inline float hwr_sunm_snap(float value, float texel)
{
    return floorf(value / texel) * texel;
}

/* Advances the window for one frame. Re-centres -- snapped -- only when the
 * camera has left the guard band, when the window's own shape moved (span or
 * resolution), or when `force` says the world behind it changed (a new map,
 * a new direction). Returns nonzero when the window moved, which is when the
 * depth map's contents stop describing it. */
static inline int hwr_sunm_window_update(struct hwr_sunm_window *window,
                                         const float dir[3],
                                         const float right[3],
                                         const float up[3],
                                         float cam_x, float cam_y, float cam_z,
                                         float span, int resolution,
                                         int force)
{
    float texel = span / (float)resolution;
    float cam_a = cam_x * right[0] + cam_y * right[1] + cam_z * right[2];
    float cam_b = cam_x * up[0] + cam_y * up[1] + cam_z * up[2];
    float cam_d = cam_x * dir[0] + cam_y * dir[1] + cam_z * dir[2];
    float guard = span * HWR_SUNM_GUARD_SHARE;
    int recenter = force || !window->valid || window->span != span ||
                   window->texel != texel ||
                   fabsf(cam_a - window->center_a) > guard ||
                   fabsf(cam_b - window->center_b) > guard;

    if (!recenter)
        return 0;
    window->center_a = hwr_sunm_snap(cam_a, texel);
    window->center_b = hwr_sunm_snap(cam_b, texel);
    window->center_d = cam_d;
    window->span = span;
    /* Depth reaches a whole span either side of the centre; what stands
     * beyond that is pancaked onto the near plane by the caster pass's depth
     * clamp, which is what keeps the roof above a window counting however
     * high it is. */
    window->depth_half = span;
    window->texel = texel;
    window->valid = 1;
    return 1;
}

/* Both matrices of one window, column-major. `clip` rasterises the caster
 * pass (light NDC, -1..1); `lookup` is the fragment's world -> [0,1]^3 read,
 * whose z equals the depth the map stored under the default depth range --
 * the two describe the same window by construction. */
static inline void hwr_sunm_matrices(const struct hwr_sunm_window *window,
                                     const float dir[3], const float right[3],
                                     const float up[3], float clip[16],
                                     float lookup[16])
{
    float half = window->span * 0.5f;
    int i;

    for (i = 0; i < 16; i++) {
        clip[i] = 0.0f;
        lookup[i] = 0.0f;
    }
    /* clip row 0: (P.right - center_a) / half */
    clip[0] = right[0] / half;
    clip[4] = right[1] / half;
    clip[8] = right[2] / half;
    clip[12] = -window->center_a / half;
    /* clip row 1: (P.up - center_b) / half */
    clip[1] = up[0] / half;
    clip[5] = up[1] / half;
    clip[9] = up[2] / half;
    clip[13] = -window->center_b / half;
    /* clip row 2: (P.dir - center_d) / depth_half */
    clip[2] = dir[0] / window->depth_half;
    clip[6] = dir[1] / window->depth_half;
    clip[10] = dir[2] / window->depth_half;
    clip[14] = -window->center_d / window->depth_half;
    clip[15] = 1.0f;
    /* lookup: the same three rows taken from -1..1 to 0..1. */
    lookup[0] = clip[0] * 0.5f;
    lookup[4] = clip[4] * 0.5f;
    lookup[8] = clip[8] * 0.5f;
    lookup[12] = clip[12] * 0.5f + 0.5f;
    lookup[1] = clip[1] * 0.5f;
    lookup[5] = clip[5] * 0.5f;
    lookup[9] = clip[9] * 0.5f;
    lookup[13] = clip[13] * 0.5f + 0.5f;
    lookup[2] = clip[2] * 0.5f;
    lookup[6] = clip[6] * 0.5f;
    lookup[10] = clip[10] * 0.5f;
    lookup[14] = clip[14] * 0.5f + 0.5f;
    lookup[15] = 1.0f;
}

/* The constant lookup bias for a window, in the [0,1] depth units the compare
 * runs in: the stated texel count at this window's own texel size, plus the
 * stated world-unit floor, over the window's whole depth extent. */
static inline float hwr_sunm_bias(const struct hwr_sunm_window *window)
{
    float bias_world = HWR_SUNM_BIAS_TEXELS * window->texel +
                       HWR_SUNM_BIAS_WORLD;

    return bias_world / (2.0f * window->depth_half);
}

/* One object card: an upright quad anchored at the object's position,
 * spanning the given horizontal axis (the light's own `right` for a
 * camera-facing object -- the billboard law with the light standing in for
 * the eye -- or the authored axis for fixed-orientation wall art), pushed
 * toward the light so the object's own drawn sprite is never darkened by its
 * own card. Texel u runs 0..height across the axis (the atlas row axis,
 * reversed by `flip`), v runs 0..width top-down. Emits two triangles. */
static inline void hwr_sunm_card(float center_x, float center_y,
                                 const float axis[2], float half_w,
                                 float z_top, float z_bottom,
                                 const float dir[3], float push,
                                 uint32_t offset, uint16_t width,
                                 uint16_t height, int flip,
                                 struct hwr_gl_sun_vertex out[6])
{
    float ax = center_x - axis[0] * half_w - dir[0] * push;
    float ay = center_y - axis[1] * half_w - dir[1] * push;
    float bx = center_x + axis[0] * half_w - dir[0] * push;
    float by = center_y + axis[1] * half_w - dir[1] * push;
    float zt = z_top - dir[2] * push;
    float zb = z_bottom - dir[2] * push;
    float u0 = flip ? (float)height : 0.0f;
    float u1 = flip ? 0.0f : (float)height;
    int i;

    for (i = 0; i < 6; i++) {
        out[i].offset = offset;
        out[i].width = width;
        out[i].height = height;
        out[i].flags = (uint8_t)(HWR_GL_SUN_MASKED | HWR_GL_SUN_CARD);
        out[i].pad0 = 0;
        out[i].pad1 = 0;
    }
    out[0].x = ax; out[0].y = ay; out[0].z = zt;
    out[0].u = u0; out[0].v = 0.0f;
    out[1].x = bx; out[1].y = by; out[1].z = zt;
    out[1].u = u1; out[1].v = 0.0f;
    out[2].x = bx; out[2].y = by; out[2].z = zb;
    out[2].u = u1; out[2].v = (float)width;
    out[3].x = ax; out[3].y = ay; out[3].z = zt;
    out[3].u = u0; out[3].v = 0.0f;
    out[4].x = bx; out[4].y = by; out[4].z = zb;
    out[4].u = u1; out[4].v = (float)width;
    out[5].x = ax; out[5].y = ay; out[5].z = zb;
    out[5].u = u0; out[5].v = (float)width;
}

#endif
