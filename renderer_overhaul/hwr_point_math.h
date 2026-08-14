#ifndef HWR_POINT_MATH_H
#define HWR_POINT_MATH_H

#include "hwr_sun_math.h"

#include <math.h>
#include <stdint.h>

/* The point-light shadow arithmetic, in one header of inline functions for
 * the same reason the sun's is: the renderer, the offline twin and the
 * negative controls all compile exactly these lines.
 *
 * ============================ THE SAMPLING LAW, CARRIED OVER ==============
 *
 * The sun's law (hwr_sun_math.h) in one line: every compare's reference is
 * carried along the receiver's own plane to the exact texel centre it reads,
 * and the bias pays for arithmetic alone. That statement does not mention
 * orthography; the gradient carry is merely its orthographic SPELLING, exact
 * because an orthographic depth is affine over a plane. A point light is a
 * perspective from six sides and the stored value here is RADIAL DISTANCE,
 * which is not affine over a plane in face texels -- so the carry is spelled
 * the general way instead: the reference at a tap is the distance from the
 * light to where that TEXEL CENTRE'S OWN RAY meets the receiver's plane,
 * computed exactly (two dot products) rather than linearized. One law, two
 * spellings; the twin compiles this one.
 *
 * WRITING the maps: six perspective faces per shadowed light, cube topology
 * laid out in one two-dimensional atlas (three faces across, two down, one
 * such tile per light), each face a 90-degree-plus-guard frustum down its
 * axis. The caster slice is the sun's own static slice -- every sector,
 * wall, slab and door, visibility-gated by nothing, the same classify and
 * the same cut-out law: a masked surface's index-0 texels are holes, the
 * translucent range keeps depth only at its fully-opaque paint -- plus the
 * light's own neighbourhood of object cards, each faced to THIS light. What
 * a texel stores is the nearest caster's radial distance over the light's
 * range, in a colour plane of its own (the face depth buffer only orders
 * fragments; both are monotone along one ray, so the nearest fragment's
 * distance is what lands). The same slice is then drawn once more as LINES,
 * exactly the sun's conservative edge pass: a line texel is written the
 * caster PLANE's distance where that texel's centre ray meets the plane,
 * held no nearer than the line's own sample, so fill and line agree texel
 * for texel and no phantom caster is stamped past a physical edge.
 *
 * READING the maps: the fragment's direction from the light picks the face
 * (major axis); the reference for every tap is the ray-plane distance above
 * wherever the receiver's plane is known (every world surface, via its
 * packed vertex normal), minus the FLOOR -- a small world-units constant
 * against interpolator and matrix rounding, plus the store's own quantum --
 * and the flat CEILING wherever the plane degenerates (a receiver the ray
 * grazes: its light is dying on the same dot product). Every LIT answer is
 * held to the BAND rule: a lit texel whose stored caster stands far beyond
 * the receiver's own band crossed ground no surface models, and the compare
 * retakes uncarried at the fragment itself. The guard band on every face
 * edge (the write frustum is one texel wider than the read window) is what
 * keeps the 2x2 blend's outer taps on honest samples at a face seam.
 *
 * ========================================================================== */

/* Faces: 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z. The atlas lays them out three
 * across and two down, face f at column f%3, row f/3, one tile per light. */
#define HWR_PSM_FACES 6

/* The CEILING on how many lights may carry a shadow map at once: the storage
 * the tile ledger and the atlas are built for, rather than the number in
 * force.
 *
 * How many actually cast is a settings row, and the atlas is sized to it (see
 * hwr_pshadow.c). The ceiling is the LIGHT ARRAY's own, because a light with
 * no slot in the picture has nothing to cast into: there can never be more
 * shadowed lights than there are lights, so the array's own maximum is the
 * only ceiling here that is a fact rather than a budget somebody picked. */
#define HWR_PSM_MAX_LIGHTS 16

/* One face's texels on a side. */
#define HWR_PSM_RESOLUTION 256

/* The guard band, in texels: the write frustum reaches this much past the
 * read window on every face edge, so a soft tap that lands in the outer
 * ring of a face still reads geometry rendered there rather than clamp
 * residue. One texel covers the 2x2 blend's farthest reach. */
#define HWR_PSM_GUARD_TEXELS 1

/* The caster pass's near plane, in world units: casters nearer the light
 * than this are inside the fixture that emits it (the wall-clearance law
 * stands lights 2..8 units off their surface) and are not casters. */
#define HWR_PSM_NEAR 0.5f

/* The lookup bias FLOOR, in world units: what a compare keeps wherever the
 * receiver's plane is known. It pays for arithmetic alone -- the ray-plane
 * reference is exact at the texel centre it is compared at -- and for the
 * write side's own units offset. A quarter unit: the perspective chain's
 * rounding is coarser than the sun's orthographic one at grazing rays, and
 * the twin's write-side gate banks the measured whole-chain error well
 * under it. */
#define HWR_PSM_BIAS_FLOOR_WORLD 0.25f

/* The lookup bias CEILING, in world units: the flat constant the plane-less
 * and degenerate classes read, and the clamp on every carried reference --
 * the same three jobs the sun's ceiling kept. Stated in world units because
 * the store is world distance over range; callers divide by range. */
#define HWR_PSM_BIAS_CEILING_WORLD 6.0f

/* The face a direction from the light leaves through: its major axis. */
static inline int hwr_psm_face(float dx, float dy, float dz)
{
    float ax = fabsf(dx);
    float ay = fabsf(dy);
    float az = fabsf(dz);

    if (ax >= ay && ax >= az)
        return dx >= 0.0f ? 0 : 1;
    if (ay >= az)
        return dy >= 0.0f ? 2 : 3;
    return dz >= 0.0f ? 4 : 5;
}

/* One face's basis: the axis it looks down and the two tangents its texel
 * coordinates run along, so that direction = axis + s*u + t*v with s and t
 * in -1..1 across the read window. The tables are the law; the GLSL in the
 * renderer transcribes exactly these rows and the twin compiles them. */
static inline void hwr_psm_face_basis(int face, float axis[3], float u[3],
                                      float v[3])
{
    static const float bases[HWR_PSM_FACES][3][3] = {
        { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
        { { -1.0f, 0.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
        { { 0.0f, 1.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
        { { 0.0f, -1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
        { { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
        { { 0.0f, 0.0f, -1.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } }
    };
    int i;

    for (i = 0; i < 3; i++) {
        axis[i] = bases[face][0][i];
        u[i] = bases[face][1][i];
        v[i] = bases[face][2][i];
    }
}

/* The guard shrink: the share of a face's -1..1 window the read side uses,
 * leaving the outer guard texels to the write side's wider frustum. */
static inline float hwr_psm_guard_share(int resolution)
{
    return (float)(resolution - 2 * HWR_PSM_GUARD_TEXELS) / (float)resolution;
}

/* A direction out of the light into its face's texel coordinates, in 0..1
 * across the face. Returns the face. */
static inline int hwr_psm_face_uv(float dx, float dy, float dz,
                                  int resolution, float *u, float *v)
{
    int face = hwr_psm_face(dx, dy, dz);
    float axis[3];
    float tu[3];
    float tv[3];
    float d[3];
    float major;
    float g = hwr_psm_guard_share(resolution);

    d[0] = dx;
    d[1] = dy;
    d[2] = dz;
    hwr_psm_face_basis(face, axis, tu, tv);
    major = d[0] * axis[0] + d[1] * axis[1] + d[2] * axis[2];
    if (!(major > 0.0f)) {
        *u = 0.5f;
        *v = 0.5f;
        return face;
    }
    *u = (d[0] * tu[0] + d[1] * tu[1] + d[2] * tu[2]) / major * g * 0.5f +
         0.5f;
    *v = (d[0] * tv[0] + d[1] * tv[1] + d[2] * tv[2]) / major * g * 0.5f +
         0.5f;
    return face;
}

/* The un-normalized ray of one face texel coordinate (0..1), major one:
 * the inverse of the map above, texel centres included. */
static inline void hwr_psm_face_dir(int face, float u, float v,
                                    int resolution, float out[3])
{
    float axis[3];
    float tu[3];
    float tv[3];
    float g = hwr_psm_guard_share(resolution);
    float s = (u - 0.5f) * 2.0f / g;
    float t = (v - 0.5f) * 2.0f / g;
    int i;

    hwr_psm_face_basis(face, axis, tu, tv);
    for (i = 0; i < 3; i++)
        out[i] = axis[i] + s * tu[i] + t * tv[i];
}

/* THE CARRY, spelled the general way: the radial distance from the light to
 * where a tap's own ray meets the receiver's plane. `p` is a point of the
 * plane (the fragment), `n` its normal, `dir` the tap ray out of the light
 * (un-normalized, as hwr_psm_face_dir hands it). Returns 0 -- caller falls
 * back to the fragment's own distance under the ceiling -- where the ray
 * grazes the plane or the intersection stands behind the light, both of
 * them receivers the light is dying on anyway. */
static inline int hwr_psm_carry(const float light[3], const float p[3],
                                const float n[3], const float dir[3],
                                float *distance)
{
    float denom = n[0] * dir[0] + n[1] * dir[1] + n[2] * dir[2];
    float num = n[0] * (p[0] - light[0]) + n[1] * (p[1] - light[1]) +
                n[2] * (p[2] - light[2]);
    float t;

    if (fabsf(denom) < 1e-6f)
        return 0;
    t = num / denom;
    if (!(t > 0.0f))
        return 0;
    *distance = t * sqrtf(dir[0] * dir[0] + dir[1] * dir[1] +
                          dir[2] * dir[2]);
    return 1;
}

/* The band a lit answer must land inside to count as evidence about THIS
 * receiver, in world units -- twice the bias plus the floor, the sun's own
 * band rule restated over distance. */
static inline float hwr_psm_band(float bias_world)
{
    return 2.0f * bias_world + HWR_PSM_BIAS_FLOOR_WORLD;
}

/* One face's world -> clip matrix for the caster pass, column-major: a
 * perspective frustum down the face axis, one guard texel wider than the
 * read window, near plane pinned, far at the light's range. The read and
 * write sides describe ONE window by construction: what the write side
 * rasterizes at s = +-1/g is what the read side files one guard texel
 * inside the face edge. */
static inline void hwr_psm_face_matrix(int face, const float light[3],
                                       float range, int resolution,
                                       float out[16])
{
    float axis[3];
    float tu[3];
    float tv[3];
    float g = hwr_psm_guard_share(resolution);
    float near_z = HWR_PSM_NEAR;
    float far_z = range > near_z * 2.0f ? range : near_z * 2.0f;
    float a = (far_z + near_z) / (far_z - near_z);
    float b = -2.0f * far_z * near_z / (far_z - near_z);
    int i;

    hwr_psm_face_basis(face, axis, tu, tv);
    /* Rows: x = g * dot(e, u); y = g * dot(e, v); z = a*w + b; w = dot(e,
     * axis); with e = P - light. Column-major storage. */
    for (i = 0; i < 16; i++)
        out[i] = 0.0f;
    out[0] = tu[0] * g;
    out[4] = tu[1] * g;
    out[8] = tu[2] * g;
    out[12] = -(tu[0] * light[0] + tu[1] * light[1] + tu[2] * light[2]) * g;
    out[1] = tv[0] * g;
    out[5] = tv[1] * g;
    out[9] = tv[2] * g;
    out[13] = -(tv[0] * light[0] + tv[1] * light[1] + tv[2] * light[2]) * g;
    out[2] = axis[0] * a;
    out[6] = axis[1] * a;
    out[10] = axis[2] * a;
    out[14] = -(axis[0] * light[0] + axis[1] * light[1] +
                axis[2] * light[2]) * a + b;
    out[3] = axis[0];
    out[7] = axis[1];
    out[11] = axis[2];
    out[15] = -(axis[0] * light[0] + axis[1] * light[1] +
                axis[2] * light[2]);
}

/* One object card faced to a POINT light: the billboard law with the light
 * standing in for the eye, per object -- the card's horizontal axis is the
 * horizontal perpendicular of the line from the light to the object.
 * Returns 0 for an object standing (in plan) on top of the light, whose
 * facing is undefined and whose card could only shadow everything. */
static inline int hwr_psm_card_axis(float light_x, float light_y,
                                    float object_x, float object_y,
                                    float axis[2])
{
    float dx = object_x - light_x;
    float dy = object_y - light_y;
    float len = sqrtf(dx * dx + dy * dy);

    if (len < 1.0f)
        return 0;
    axis[0] = dy / len;
    axis[1] = -dx / len;
    return 1;
}

#endif
