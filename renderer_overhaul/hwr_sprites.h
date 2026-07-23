#ifndef HWR_SPRITES_H
#define HWR_SPRITES_H

#include "hwr_das.h"
#include "roth_sdk.h"

#include <stddef.h>
#include <stdint.h>

/* The sprite resource layer sits on top of the two DAS archives the map load
 * already opens: the per-map archive (object graphic ids resolve into its tail)
 * and the shared archive (ADEMO), which holds the game-wide sprite set. It
 * pre-decodes every sprite frame at load, resolves an object's graphic id (plus
 * its facing) to a single decoded frame the way the engine's face resolver does,
 * and hands the emitter the frame's pixels and metadata. No pixels reach the GPU
 * here; that is the emitter's job. */

/* Object graphic ids are biased into the archive id space before lookup:
 * emit_world_face_spans adds 0x1000 (renderer.c:5748). Ids below 0x1200 index
 * the per-map archive directly; 0x1200..0x15ff index the shared archive at
 * id-0x1200 (renderer.c:5885-5888, mirrored by the mesh path's resolver). */
#define HWR_SPRITE_ID_BIAS       0x1000u
#define HWR_SPRITE_SHARED_FIRST  0x1200u
#define HWR_SPRITE_ID_LIMIT      0x1600u

struct hwr_sprite_census {
    size_t plain;
    size_t grouped;
    size_t animated;
    size_t animated_delta;
    size_t animated_rle;
    /* Directory placeholders: monster sets (0x24 '$') and dir-object sets
     * (0x20 ' '). They carry no pixels of their own; they indirect to leaf
     * frames through the runtime descriptor tables. */
    size_t placeholder;
    size_t other;
    size_t sprite_frames;
    size_t failures;
};

/* A resolved leaf frame, ready for the emitter to upload. `pixels` is 8bpp
 * palette indices, width*height bytes, owned by the archive entry (valid until
 * the archive is closed or the entry released). */
struct hwr_sprite_frame {
    uint16_t width;
    uint16_t height;
    /* Material flags word (payload+0): bit 0x400 = masked/translucent-capable,
     * 0x100 = animated, 0x40 = grouped. The emitter keys texel-0 masking off
     * this word, never a blanket discard (renderer.c:5970/6288). */
    uint16_t flags;
    const uint8_t *pixels;
};

/* The outcome of resolving one object's graphic id to a drawable frame. */
struct hwr_sprite_lookup {
    struct hwr_das_archive *archive;
    size_t entry_id;
    /* Frame selector within the leaf resource. For an animated block this is the
     * base frame (playback advances it on the mod's own clock, as with surface
     * animations); for a grouped block it is the directional child, whose pixels
     * live at `child_offset`. Plain resources ignore both. */
    uint16_t frame_index;
    size_t child_offset;
    /* Accumulated horizontal-flip parity from every frame-table entry crossed. */
    int flip;
    enum hwr_das_class klass;
    int resolved;
};

/* Maps a bias-applied archive id to its owning archive and entry index, the same
 * split the mesh path uses. Returns 0 and fills the outputs on success, -1 for an
 * id outside either archive. */
int hwr_sprites_locate(struct hwr_das_archive *map, struct hwr_das_archive *shared,
                       uint16_t archive_id, struct hwr_das_archive **owner,
                       size_t *entry_id);

/* Decodes and classifies every entry in [first_id, entry_count) of `archive`,
 * accumulating class, frame, and failure counts. Decoding is idempotent, so a
 * later scene upload reuses this work rather than repeating it. Pass first_id 0
 * for the shared archive (all sprites) and the bias for the per-map archive (its
 * object-graphic tail begins at the bias). */
void hwr_sprites_predecode(struct hwr_das_archive *archive, size_t first_id,
                           struct hwr_sprite_census *census);

/* Resolves an object's graphic id and facing to a single leaf frame, mirroring
 * the engine face resolver (renderer.c:5787-5932): applies the id bias, reads the
 * runtime status table's class byte, walks the monster/dir-object descriptor
 * tables and the directional frame table, and records any x-flip. `yaw` is the
 * view angle (g_sprite_view_angle domain, 0x200) and `screen_angle_corr` is the
 * per-sprite screen-angle term (viewX<<6)/depth the emitter derives from its own
 * projection; pass 0 before projection is available. Returns 0 with out->resolved
 * set on success. */
int hwr_sprites_resolve(const struct roth_api_v1 *api,
                        struct hwr_das_archive *map, struct hwr_das_archive *shared,
                        uint16_t graphic_id, uint8_t facing, uint16_t yaw,
                        int32_t screen_angle_corr, struct hwr_sprite_lookup *out);

/* Fills `frame` from a resolved lookup. Returns 0 on success, -1 when the leaf
 * carries no pixels (a directory placeholder) or the frame index is out of
 * range. */
int hwr_sprites_frame(const struct hwr_sprite_lookup *lookup,
                      struct hwr_sprite_frame *frame);

#endif
