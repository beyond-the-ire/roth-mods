#ifndef HWR_OBJECTS_H
#define HWR_OBJECTS_H

#include "hwr_das.h"
#include "hwr_world.h"
#include "roth_sdk.h"

#include <stddef.h>
#include <stdint.h>

/* The object layer enumerates every billboard the engine would draw from its
 * persistent state and flattens it into one per-frame array, including the
 * synthetic third-person player record used only by reflected subviews.
 * Nothing is cached across frames: pool-derived pointers are re-read from live
 * state each rebuild, so unlinked (collected/killed) objects disappear by
 * construction. */

enum hwr_object_source {
    HWR_OBJECT_MAP = 0,     /* per-sector map-object list */
    HWR_OBJECT_PARTICLE,    /* particle pool */
    HWR_OBJECT_PICKUP_FLY,  /* pickup-fly overlay (persistent source) */
    HWR_OBJECT_PLAYER_REFLECTION, /* mirror-only synthetic player billboard */
    HWR_OBJECT_SOURCE_COUNT
};

/* Object record flags carried forward for the emitter. */
enum hwr_object_flags {
    /* Map record +9 bit1: skip this object during a reflection/secondary walk.
     * The primary pass ignores it; a mirror subview must honour it. */
    HWR_OBJECT_FLAG_REFLECTION_SKIP = 1u << 0,
    /* Map record +7 bit0: the engine's type-3 draw-entry discriminator
     * (renderer.c:8042). Type-3 faces are mostly authored as 3D-object meshes
     * (resolved block flag 0x8000) and are skipped with the mesh class; a
     * non-mesh type-3 takes the linear (camera-facing) path unless it also
     * carries +9 bit0x80. The FIXED-ORIENTATION (flat-on-wall) rendering is the
     * engine's ROTATED path, keyed on record +9 bit0x80 (see flags9 and
     * rwss_rotated_tail renderer.c:6093-6148) -- the emitter orients those by
     * facing. This flag is retained as the type-3 marker for diagnostics. */
    HWR_OBJECT_FLAG_FLAT = 1u << 1,
    /* The stock mirror walk synthesizes the player as a world billboard; it is
     * absent from the primary view's ordinary object enumeration. */
    HWR_OBJECT_FLAG_REFLECTION_ONLY = 1u << 2,
    /* The synthetic player record carries raw +9 bit0 for stock bookkeeping,
     * but does not receive a map-object ground shadow. */
    HWR_OBJECT_FLAG_NO_SHADOW = 1u << 3
};

struct hwr_object {
    int16_t world_x;
    int16_t world_y;
    int16_t world_z;
    uint16_t graphic_id;
    uint8_t facing;
    /* Record +0xe: command-target id used by object-related map commands. */
    uint16_t object_id;
    /* Record +8: the shade seed the type-1 billboard path composes with the
     * owner sector's light (renderer.c:6180-6182). */
    uint8_t seed;
    uint8_t source;
    uint8_t flags;
    /* Raw record flag bytes +7 and +9, kept so the emitter can read the type-3
     * sub-discriminators (record +9 bit0x80 selects the engine's rotated /
     * fixed-orientation projection for a flat face). */
    uint8_t flags7;
    uint8_t flags9;
    /* Owner sector index recorded during the per-sector enumeration, so the
     * emitter can gate a billboard on the engine's visible-sector set and
     * inherit that sector's shade/tint selection. HWR_OBJECT_NO_SECTOR when the
     * source carries no owning sector (particles, pickup overlay). */
    uint16_t sector_index;
    /* Record +0xc: live-pool backref. Monsters resolve actor appearance through
     * actor_index*0x22+1; command-driven RLE animation can also address the
     * actor or walker pool through this same value. Zero means unlinked. */
    uint16_t backref;
    /* Live address of the appearance record used as draw-entry +8. The
     * software renderer repeats this exact pointer in every type-1 worklist
     * entry, allowing the hardware layer to recover the object's own portal
     * windows instead of approximating them from its owner sector. */
    uint32_t appearance_ptr;
};

/* Sentinel owner-sector value for objects with no owning sector. */
#define HWR_OBJECT_NO_SECTOR 0xffffu

/* An object's effective shade byte, composed the way the engine's own type-1
 * billboard path composes it (renderer.c:6180-6182): the record's seed biased
 * by its owner sector's light where both are nonzero, and the seed alone
 * otherwise. A neutral 0x80 seed reduces to the sector's light, exactly like a
 * wall.
 *
 * Zero is the author's unshaded class -- fullbright at every distance -- which
 * is also the map's own mark for a surface that gives light rather than takes
 * it. Whatever reads that mark and the emitter that draws by it compose it HERE,
 * in one place: a mark read from a transcription of the rule is a mark that can
 * drift away from the rule without anything noticing. */
uint8_t hwr_object_composed_light(uint8_t seed, uint8_t sector_light);

struct hwr_objects {
    struct hwr_object *items;
    size_t count;
    size_t capacity;
    size_t source_counts[HWR_OBJECT_SOURCE_COUNT];
    int enabled;
    int valid;
    char error[192];
};

/* Enables or disables the whole layer (HWR_OBJECTS=0 disables). A disabled layer
 * rebuilds to an empty list. */
void hwr_objects_set_enabled(struct hwr_objects *objects, int enabled);

/* Rebuilds the flat billboard array from the currently loaded map's persistent
 * state. Returns 0 on success (including a deliberately empty list when the layer
 * is disabled), -1 if the object section is not staged. */
int hwr_objects_rebuild(struct hwr_objects *objects, const struct hwr_world *world,
                        const struct roth_api_v1 *api);

/* Writes a human-readable snapshot of the current list to `path` (atomically),
 * resolving each object's frame through the sprite layer for the WxH columns. */
void hwr_objects_dump(const struct hwr_objects *objects, const char *path,
                      const struct roth_api_v1 *api,
                      struct hwr_das_archive *map_archive,
                      struct hwr_das_archive *shared_archive);

/* The light-state instrument (HWR_LIGHT_DUMP=<path>).
 *
 * A map's lighting is not authoring alone: the engine rewrites a sector's light
 * byte while a switch ramps and rewrites the same record's flags byte when the
 * ramp finishes, and both are read fresh out of the sector table every frame.
 * This writes what those two bytes are doing, so a claim about them is a
 * measurement rather than a reading of the engine's source.
 *
 * The first call after a map load appends a full census -- every sector's
 * flags and light, and every enumerated object's identity, seed, composed byte
 * and owner sector. Every call after that appends a block ONLY when something
 * moved: a sector whose flags or light changed since the previous call, or a
 * change in the set of objects the map marks unshaded. A map on which no light
 * command runs therefore produces a census and nothing else, however long it is
 * watched, which is what makes "nothing moved" readable rather than inferred.
 *
 * Diagnostic only: it appends to a file and touches no renderer state. */
void hwr_objects_dump_lights(const struct hwr_objects *objects,
                             const struct hwr_world *world, const char *path);

/* Forgets the previous light state, so the next dump writes a census. Called
 * when a map is staged: a new map's sector 12 is not the old map's sector 12. */
void hwr_objects_dump_lights_reset(void);

void hwr_objects_clear(struct hwr_objects *objects);

#endif
