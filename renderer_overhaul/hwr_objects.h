#ifndef HWR_OBJECTS_H
#define HWR_OBJECTS_H

#include "hwr_das.h"
#include "hwr_world.h"
#include "roth_sdk.h"

#include <stddef.h>
#include <stdint.h>

/* The object layer enumerates every billboard the engine would draw from its
 * persistent state and flattens it into one per-frame array. It reproduces the
 * two primary sources fully and reads the pickup-fly overlay's persistent form;
 * the player weapon viewmodel is deliberately left to the engine. Nothing is
 * cached across frames: pool-derived pointers are re-read from live state each
 * rebuild, so unlinked (collected/killed) objects disappear by construction. */

enum hwr_object_source {
    HWR_OBJECT_MAP = 0,     /* per-sector map-object list */
    HWR_OBJECT_PARTICLE,    /* particle pool */
    HWR_OBJECT_PICKUP_FLY,  /* pickup-fly overlay (persistent source) */
    HWR_OBJECT_SOURCE_COUNT
};

/* Object record flags carried forward for the emitter. */
enum hwr_object_flags {
    /* Map record +9 bit1: skip this object during a reflection/secondary walk.
     * The primary pass ignores it; a mirror subview must honour it. */
    HWR_OBJECT_FLAG_REFLECTION_SKIP = 1u << 0
};

struct hwr_object {
    int16_t world_x;
    int16_t world_y;
    int16_t world_z;
    uint16_t graphic_id;
    uint8_t facing;
    uint8_t shade;
    uint8_t source;
    uint8_t flags;
};

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

void hwr_objects_clear(struct hwr_objects *objects);

#endif
