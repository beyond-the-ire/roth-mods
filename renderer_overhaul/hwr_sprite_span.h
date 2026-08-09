#ifndef HWR_SPRITE_SPAN_H
#define HWR_SPRITE_SPAN_H

#include "hwr_das.h"
#include "hwr_objects.h"
#include "hwr_sprites.h"
#include "roth_sdk.h"

#include <stdint.h>

/* HOW TALL A SPRITE STANDS IN THE WORLD, AND WHERE ITS TOP AND BOTTOM ARE.
 *
 * One law, in one place, because two things need the answer and they must never
 * disagree: the emitter, which draws the quad, and the seeding, which puts a
 * thing's light at the middle of the sprite it is drawn as. A light hung at the
 * middle of a transcription of this arithmetic would drift out of the sprite it
 * belongs to the first time either copy was touched, and nothing in the picture
 * would say so.
 *
 * The arithmetic itself is the engine's, and the citations beside each part are
 * to the engine's own routines. Nothing here reads the world or the settings:
 * it is given the sprite's numbers and answers in world units. */

/* The engine's type-1 billboard projects its horizontal extent with
 * g_view_params_block+0xc and its vertical with g_perspective_scale, dividing by
 * g_view_clip_plane -- a view depth carried in a x256 fixed point. The mod's
 * world shader projects with the camera's own focals against a x1 world depth,
 * so matching the engine's projected pixels scales the pre-projection extent by
 * (engine focal / mod focal) and divides by that 256. */
#ifndef VA_G_VIEW_PARAMS_XSCALE
#define VA_G_VIEW_PARAMS_XSCALE  0x8527cu
#endif
#ifndef VA_G_PERSPECTIVE_SCALE
#define VA_G_PERSPECTIVE_SCALE   0x85288u
#endif

/* The two scales, read off the engine's live projection state:
 *   half_w_world = extent_h * Xscale / (focal_x * 256)
 *   vext_world   = extent_v * Pscale / (focal_y * 256)
 * (finalize_draw_list_entry renderer.c:7738-7768; proj64 :7284). Reading both
 * focals keeps it exact whatever the Xscale:focal ratios are.
 *
 * Answers 0 where no projection is staged yet, which is not a size of nothing:
 * a caller with no answer here has to leave whatever it was going to size
 * alone. */
static inline int hwr_sprite_scales(const struct roth_api_v1 *api,
                                    double focal_x, double focal_y,
                                    double *hscale, double *vscale)
{
    double xscale;
    double pscale;

    if (api == NULL || api->game_ram == NULL)
        return 0;
    xscale = (double)(int32_t)api->game_ram->u32(VA_G_VIEW_PARAMS_XSCALE);
    pscale = (double)(int32_t)api->game_ram->u32(VA_G_PERSPECTIVE_SCALE);
    if (focal_x <= 0.0 || focal_y <= 0.0 || xscale <= 0.0 || pscale <= 0.0)
        return 0;
    *hscale = xscale / (focal_x * 256.0);
    *vscale = pscale / (focal_y * 256.0);
    return 1;
}

/* The block modifier's own two terms (renderer.c:7723-7738's cl / w980 adjust):
 * bit 0x80 with 0x6000 scales both axes by 2^t, bit 0x80 alone halves them. The
 * shift `cl` is the horizontal half of that and `famod` the vertical. */
static inline void hwr_sprite_modifier(uint16_t fa, double *famod, int *cl)
{
    *famod = 1.0;
    *cl = 7;
    if ((fa & 0x80u) != 0) {
        if ((fa & 0x6000u) != 0) {
            int t = (int)(((uint32_t)(fa << 3) | (fa >> 13)) & 3u);

            *famod = (double)(1u << t);
            *cl = 7 + t;
        } else {
            *famod = 0.5;
            *cl = 6;
        }
    }
}

/* The signed DAS origin prefix (FAT flag 8; hwr_das.c:527), which the engine
 * applies before the anchor branch: the low word is a flip-aware HORIZONTAL
 * origin added to the view X the quad centres on (renderer.c:5667-5669), the
 * high word a VERTICAL origin folded into the displacement (renderer.c:7758).
 * It is per-resolved-leaf: grouped children carry their own child data rather
 * than the entry prefix, and the grouped path already matches at origin 0, so
 * the prefix belongs to plain and animated leaves alone. */
static inline void hwr_sprite_origin(const struct hwr_sprite_lookup *lookup,
                                     int16_t *origin_x, int16_t *origin_y)
{
    *origin_x = 0;
    *origin_y = 0;
    if (lookup == NULL || lookup->archive == NULL ||
        lookup->entry_id >= lookup->archive->entry_count)
        return;
    if (lookup->klass == HWR_DAS_CLASS_PLAIN ||
        lookup->klass == HWR_DAS_CLASS_ANIMATED) {
        const struct hwr_das_entry *entry =
            &lookup->archive->entries[lookup->entry_id];

        *origin_x = (int16_t)(uint16_t)(entry->size_prefix & 0xffffu);
        *origin_y = (int16_t)(uint16_t)(entry->size_prefix >> 16);
    }
    if (lookup->flip)                       /* x-flip mirrors it (renderer.c:5668) */
        *origin_x = (int16_t)(-*origin_x);
}

/* WHERE THE SPRITE'S TOP AND BOTTOM EDGES ARE, in world z.
 *
 * The vertical extent is the frame's WIDTH -- the sprite's column-major storage
 * means its on-screen height is its texture width -- at 2 world units per texel,
 * the same density the wall driver maps, scaled by the block modifier and by the
 * projection match above.
 *
 * The anchor is the engine's (finalize_draw_list_entry renderer.c:7754-7768).
 * Its vertical anchor is eye_z - objZ in screen-DOWN units and the modifier
 * byte's low nibble is a displacement of 2*(dl&0xf), plus the block's authored
 * vertical origin -- which the ROTATED path omits (renderer.c:7756-7758 against
 * 6129-6134):
 *   default 0x8N (bit 0x10 clear): the FEET sit at objZ - displacement
 *   0x9N     (bit 0x10 set):       the HEAD sits at objZ + displacement
 * World z increases UPWARD, so the top edge is the LARGER z and the sign of the
 * displacement differs between the two anchors. */
static inline void hwr_sprite_vertical_span(int32_t object_z, uint8_t flags9,
                                            uint16_t fa, uint16_t frame_width,
                                            int16_t origin_y, double vscale,
                                            int32_t *z_top, int32_t *z_bottom)
{
    double famod;
    double vext_d;
    double disp_units;
    double disp_d;
    int32_t vext;
    int32_t disp;
    int cl;

    hwr_sprite_modifier(fa, &famod, &cl);
    vext_d = 2.0 * (double)frame_width * famod * vscale;
    vext = (int32_t)(vext_d + 0.5);
    disp_units = 2.0 * (double)(fa & 0xfu)
               + ((flags9 & HWR_OBJECT_FLAGS9_ROTATED) ? 0.0 : (double)origin_y);
    disp_d = disp_units * vscale;
    disp = (int32_t)(disp_d + (disp_d >= 0.0 ? 0.5 : -0.5));
    if ((fa & 0x10u) != 0) {
        *z_top = object_z + disp;               /* head at objZ + displacement */
        *z_bottom = *z_top - vext;
    } else {
        *z_bottom = object_z - disp;            /* feet at objZ - displacement */
        *z_top = *z_bottom + vext;
    }
}

#endif
