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

/* The lookup bias CEILING, in light-space texels of world size. Two texels
 * plus one world unit: the first milestone read this flat constant at every
 * fragment, and a flat constant is exactly what leaked light into the
 * corners -- where a wall meets the ceiling that shades it, the depth
 * separating the two runs to nothing, and every world unit of slack is a
 * strip of the wall lit through the joint. The receiver-slope law below
 * replaces it per fragment; the constant survives only as the worst case
 * that law may never exceed. */
#define HWR_SUNM_BIAS_TEXELS 2.0f
#define HWR_SUNM_BIAS_WORLD 1.0f

/* The floor under the receiver-slope bias, in world units: what covers the
 * arithmetic itself (rounding, the interpolators) when the receiver faces
 * the light head-on and its own slope term is nothing. A quarter unit,
 * beside the depth quantum the floor term adds on top: a whole world unit
 * here was most of the horizontal line of light the director found at a
 * narrow wall's top -- where a wall meets the ceiling that shades it the
 * geometric separation runs to nothing, and every unit of floor is a strip
 * of the wall lit through the joint, while the arithmetic the floor exists
 * to cover is on the depth quantum's own scale. */
#define HWR_SUNM_BIAS_FLOOR_WORLD 0.25f

/* How far the softness dial reaches, in light-space texels: the dial's top
 * is a penumbra this many texels wide. The default of a tenth lands at half
 * a texel, which is the single bilinear tap's own footprint -- so the
 * default look IS the first milestone's free 2x2 PCF, to the bit. */
#define HWR_SUNM_SOFT_MAX_TEXELS 5.0f

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

/* The bias ceiling for a window, in the [0,1] depth units the compare runs
 * in: the stated texel count at this window's own texel size, plus the
 * stated world-unit floor, over the window's whole depth extent. */
static inline float hwr_sunm_bias(const struct hwr_sunm_window *window)
{
    float bias_world = HWR_SUNM_BIAS_TEXELS * window->texel +
                       HWR_SUNM_BIAS_WORLD;

    return bias_world / (2.0f * window->depth_half);
}

/* The softness dial as a radius: nothing is hard, the top of the dial is
 * the stated texel reach, and the walk between them is straight. */
static inline float hwr_sunm_soft_radius(float softness)
{
    if (!(softness > 0.0f))
        return 0.0f;
    if (softness > 1.0f)
        softness = 1.0f;
    return softness * HWR_SUNM_SOFT_MAX_TEXELS;
}

/* How many ring taps a tier spends on that radius. A radius inside the
 * single tap's own bilinear footprint takes no ring at all -- the centre
 * tap IS the picture, which is what keeps the default look the first
 * milestone's -- and past it the Medium tier stands four taps on the ring,
 * the High tier eight. `wide` is the High tier saying so. */
static inline int hwr_sunm_soft_taps(int wide, float radius_texels)
{
    if (!(radius_texels > 0.5f))
        return 0;
    return wide ? 8 : 4;
}

/* The receiver-slope bias law, evaluated the same way by the fragment stage
 * and the offline twin: the fragment's own light-space depth gradient
 * (per unit of the [0,1] lookup plane, one component per axis), scaled to
 * the sampling footprint in texels, floored, and held under the ceiling.
 * The gradient is what a flat constant could never be -- a wall facing the
 * light has almost none and gets almost no slack, which is what closes the
 * lit strip under a ceiling; a surface the light grazes has a large one
 * and gets exactly the slack its own texel footprint spans. */
static inline float hwr_sunm_bias_eval(float dzdu, float dzdv, float slope,
                                       float floor_, float cap)
{
    float bias = (fabsf(dzdu) + fabsf(dzdv)) * slope + floor_;

    return bias < cap ? bias : cap;
}

/* The receiver's light-space depth gradient, ANALYTIC: from the receiver's
 * own plane normal and the lookup matrix, never from screen derivatives.
 *
 * Screen derivatives are exact only inside one plane's pixels. At every
 * plane JOINT -- a wall meeting its floor, two walls meeting in a corner, a
 * wall's silhouette against whatever stands behind it -- the rasterizer's
 * 2x2 quads straddle two planes and the derivatives mix them, the gradient
 * blows up, and the bias law's ceiling clamp then hands those exact pixels
 * the flat-constant slack the law exists to retire: a one-pixel line of sky
 * along the joint. That line, on the director's own save, was the pillar-top
 * leak and the floor-seam and corner hairlines beside it. The receiver's
 * plane is a fact the vertices already carry as their packed normal, so the
 * gradient is derived from it instead, which is also exactly what the
 * offline twin computes -- one law, both sides.
 *
 * The rows of `lookup` are the linear map's three gradients in world space:
 * a = d(u)/dP, b = d(v)/dP, c = d(z)/dP. A step t in the receiver's plane
 * with no v movement is t = n x b (perpendicular to both), and along it
 * dz/du = (c.t)/(a.t); the same with a for the v axis. Returns 0 -- caller
 * falls back to the bias ceiling -- where the plane degenerates in the light
 * plane, which is a receiver the light grazes: its key term is fading to
 * nothing on the same dot product, so the ceiling there lights nothing. */
static inline int hwr_sunm_receiver_gradient(const float normal[3],
                                             const float lookup[16],
                                             float *gu, float *gv)
{
    float ax = lookup[0], ay = lookup[4], az = lookup[8];
    float bx = lookup[1], by = lookup[5], bz = lookup[9];
    float cx = lookup[2], cy = lookup[6], cz = lookup[10];
    float t1x = normal[1] * bz - normal[2] * by;
    float t1y = normal[2] * bx - normal[0] * bz;
    float t1z = normal[0] * by - normal[1] * bx;
    float t2x = normal[1] * az - normal[2] * ay;
    float t2y = normal[2] * ax - normal[0] * az;
    float t2z = normal[0] * ay - normal[1] * ax;
    float au = ax * t1x + ay * t1y + az * t1z;
    float bv = bx * t2x + by * t2y + bz * t2z;

    if (!(au > 1e-12f || au < -1e-12f) || !(bv > 1e-12f || bv < -1e-12f))
        return 0;
    *gu = (cx * t1x + cy * t1y + cz * t1z) / au;
    *gv = (cx * t2x + cy * t2y + cz * t2z) / bv;
    return 1;
}

/* The floor term of that law for a window, in [0,1] depth units: a world
 * unit plus the depth map's own quantum. */
static inline float hwr_sunm_bias_floor(const struct hwr_sunm_window *window)
{
    float range = 2.0f * window->depth_half;

    return (HWR_SUNM_BIAS_FLOOR_WORLD + range / 65536.0f) / range;
}

/* The slope scale of that law: the sampling footprint in texels -- half a
 * texel, the distance from a fragment to its own texel's centre, and
 * nothing more at ANY softness: every farther texel the soft blend or the
 * ring reads is compared against the reference CARRIED onto it along the
 * receiver's own depth gradient, so their distance never has to be bought
 * with slack. Slack is a strip of light wherever a caster meets the surface
 * it shades -- the narrow-wall line the director found was made of exactly
 * the texels this footprint no longer pays for. */
static inline float hwr_sunm_bias_slope(int soft, int taps,
                                        float radius_texels, int resolution)
{
    (void)soft;
    (void)taps;
    (void)radius_texels;
    return 0.5f / (float)resolution;
}

/* The texel side of one decoded 3D-prop face, exactly as the eye emitter
 * reads the same DAS fields (the four coordinate laws and the flip), so a
 * tree's leaf holes are the same holes in its shadow that they are in its
 * picture. `s` runs the atlas row axis (wraps by height), `t` the column
 * axis (wraps by width) -- the caster program's own iy*w+ix addressing.
 *
 * The laws, by the face's own flag bits: 0x1000 carries packed per-corner
 * coordinates; 0x20 stretches the picture by the scale nibbles; 0x200 is
 * the floor mapping; everything else is the wall driver's row/column walk,
 * with 0x100 reading the extents instead of the picture's size. The flip is
 * the eye's own condition -- the face flag against the resolved child's --
 * baked here as a reversed row coordinate. */
static inline void hwr_sunm_mesh_uv(uint16_t face_flags, uint16_t extent_u,
                                    uint16_t extent_v, uint8_t scale,
                                    const uint8_t packed_control[4],
                                    const uint8_t packed_a[4],
                                    const uint8_t packed_b[4],
                                    uint16_t width, uint16_t height,
                                    int child_flip, float s[4], float t[4])
{
    int i;

    if ((face_flags & 0x1000u) != 0) {
        for (i = 0; i < 4; i++) {
            s[i] = (float)((((uint16_t)(packed_control[i] & 0x80u)) << 1) |
                           packed_a[i]);
            t[i] = (float)((((uint16_t)(packed_control[i] & 0x40u)) << 2) |
                           packed_b[i]);
        }
    } else if ((face_flags & 0x20u) != 0) {
        uint8_t scale_h = (uint8_t)(scale & 0x0fu);
        uint8_t scale_w = (uint8_t)(scale >> 4);
        float u = (float)height;
        float v = (float)width;

        if (scale_h != 0)
            u *= (float)(scale_h + 1u);
        if (scale_w != 0)
            v *= (float)(scale_w + 1u);
        s[0] = 0.0f; s[1] = u; s[2] = u; s[3] = 0.0f;
        t[0] = 0.0f; t[1] = 0.0f; t[2] = v; t[3] = v;
    } else if ((face_flags & 0x200u) != 0) {
        float u = (float)height;
        float v = (float)width;

        s[0] = u; s[1] = 0.0f; s[2] = 0.0f; s[3] = u;
        t[0] = 0.0f; t[1] = 0.0f; t[2] = v; t[3] = v;
    } else {
        float row = (float)height;
        float column = (float)width;

        if ((face_flags & 0x100u) != 0) {
            row = 0.5f * (float)extent_v;
            column = 0.5f * (float)extent_u;
        }
        s[0] = 0.0f; s[1] = row;  s[2] = row;    s[3] = 0.0f;
        t[0] = 0.0f; t[1] = 0.0f; t[2] = column; t[3] = column;
    }
    /* The eye's flip condition: the authored face flip (never on the packed
     * law, which authors its corners directly) against the child's own. */
    if ((((face_flags & 0x1000u) == 0 && (face_flags & 2u) != 0) ? 1 : 0) !=
        (child_flip != 0)) {
        for (i = 0; i < 4; i++)
            s[i] = (float)height - s[i];
    }
}

/* Which caster flags a decoded 3D-prop face carries: the transparency bit
 * is the face's own (the DAS RENDER_FLAG transparency), and it is the whole
 * of the law -- a masked face's index-0 texels are holes in its shadow, and
 * every painted texel blocks whole, exactly the sprite cards' law. A face
 * without it casts solid: a trunk has no cut-outs to honour. */
static inline uint8_t hwr_sunm_mesh_flags(uint16_t face_flags)
{
    return (face_flags & 1u) != 0 ? (uint8_t)HWR_GL_SUN_MASKED : (uint8_t)0u;
}

/* One object card: an upright quad anchored at the object's position,
 * spanning the given horizontal axis (the light's own `right` for a
 * camera-facing object -- the billboard law with the light standing in for
 * the eye -- or the authored axis for fixed-orientation wall art). `push`
 * moves it toward the light; the shipped emitter passes nothing there, the
 * receiver-slope bias being what keeps the object's own drawn sprite in its
 * light. Texel u runs 0..height across the axis (the atlas row axis,
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
