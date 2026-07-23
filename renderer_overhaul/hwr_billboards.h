#ifndef HWR_BILLBOARDS_H
#define HWR_BILLBOARDS_H

#include "hwr_das.h"
#include "hwr_gl.h"
#include "hwr_objects.h"
#include "hwr_world.h"
#include "roth_sdk.h"

#include <stddef.h>
#include <stdint.h>

/* The billboard layer turns the per-frame object enumeration (hwr_objects) into
 * camera-facing textured quads streamed alongside the world geometry. It packs
 * every sprite frame into the same texel store the world textures use (so a
 * quad references a byte offset exactly as a wall vertex does), resolves each
 * object's graphic id + facing + live animation state to a frame the way the
 * engine's face resolver does, and emits a quad sized and anchored from the
 * resolved frame the way the engine's type-1 draw path does.
 *
 * The 3D-object mesh class (resource flag bit 0x8000 on the resolved block) is
 * SKIPPED here and counted; that is a later change. Nothing here is cached across
 * frames except the immutable per-map sprite atlas built at map load. */

/* One packed directional child of a grouped resource, keyed by its payload-
 * relative byte offset (== the value hwr_das_group_child_offset yields, so the
 * emitter can match the resolved child). */
struct hwr_bb_child {
    uint32_t payload_offset;
    uint32_t atlas_offset;
    uint16_t width;
    uint16_t height;
    uint16_t flags;
};

/* One packed sprite resource, addressed by (archive, entry id). Plain and
 * grouped resources hold a single image; animated resources hold frame_count
 * images packed back to back at base_offset, stride frame_bytes. A grouped
 * resource packs EVERY directional child into the store and records them in the
 * billboards' shared child table [child_first, child_first+child_count); the
 * emitter picks the child matching the resolved directional index, so a
 * grouped statue shows the correct facing (base_offset stays the first child as
 * a fallback when a resolved child is not among the packed set). */
struct hwr_bb_slot {
    uint32_t base_offset;
    uint32_t frame_bytes;
    uint32_t child_first;
    uint16_t child_count;
    uint16_t width;
    uint16_t height;
    uint16_t flags;
    uint16_t frame_count;
    uint8_t klass;
    uint8_t present;
};

struct hwr_billboards {
    struct hwr_bb_slot *map_dir;
    size_t map_dir_count;
    struct hwr_bb_slot *shared_dir;
    size_t shared_dir_count;
    struct hwr_das_archive *map_archive;
    struct hwr_das_archive *shared_archive;
    /* Shared table of packed grouped-children; slots index into it. */
    struct hwr_bb_child *children;
    size_t child_count;
    size_t child_capacity;
    /* The engine's ground-shadow: a 7x32 translucent oval from the obj1 default
     * texture record (VA 0x29e5c), packed once at map load. The emitter draws it
     * as a camera-facing translucent billboard under each object whose map record
     * has +9 bit0 set (the engine's type-1 "mirror double" gate, which stores a
     * second entry whose texture id 0xefff biases to the 0xffff default record). */
    struct hwr_bb_slot shadow;
    int built;

    /* Per-frame emit scratch. Opaque (raw) quads depth-test naturally; masked/
     * blend-capable quads (block flag 0x400) join the translucent machinery,
     * one run each so a stacked pair blends against the right destination. */
    struct hwr_gl_vertex *opaque;
    size_t opaque_count;
    size_t opaque_capacity;
    /* Fixed-orientation (rotated) opaque quads, kept apart from the camera-facing
     * opaque quads so they can be drawn with a small depth bias toward the viewer:
     * a flat wall decal is coplanar with its wall and would z-fight it, where the
     * engine's painter order paints the decal after the wall. */
    struct hwr_gl_vertex *flat;
    size_t flat_count;
    size_t flat_capacity;
    struct hwr_gl_vertex *translucent;
    size_t translucent_count;
    size_t translucent_capacity;
    uint32_t *runs;
    size_t run_count;
    size_t run_capacity;

    size_t stat_emitted;
    size_t stat_enumerated;
    size_t stat_skipped_mesh;
    char error[192];
};

/* HWR_BILLBOARDS=0 disables the whole layer: no atlas is packed and no quads
 * are emitted, so the stream is byte-identical to the world-only path. */
int hwr_billboards_enabled(void);

void hwr_billboards_init(struct hwr_billboards *bb);
void hwr_billboards_clear(struct hwr_billboards *bb);

/* Builds the per-map sprite atlas and appends its pixels to the texel store the
 * caller is assembling (the same buffer the world textures were packed into).
 * A no-op that leaves the store untouched when the layer is disabled. Returns 0
 * on success, -1 with bb->error set on allocation failure. */
int hwr_billboards_pack(struct hwr_billboards *bb,
                        struct hwr_das_archive *map_archive,
                        struct hwr_das_archive *shared_archive,
                        const struct roth_api_v1 *api,
                        uint8_t **bytes, size_t *size, size_t *capacity);

/* Resolves and projects this frame's billboards into bb->opaque / bb->translucent
 * (+ bb->runs). `visible`/`visible_count` are the PVS drawn-sector set; when
 * pvs_valid is set an object whose owner sector is not drawn is dropped, exactly
 * as the engine emits billboards per visible sector. Returns 0 (including an
 * empty result when disabled). */
int hwr_billboards_emit(struct hwr_billboards *bb,
                        const struct hwr_objects *objects,
                        const struct hwr_world *world,
                        const struct hwr_gl_camera *camera,
                        const uint8_t *visible, size_t visible_count,
                        int pvs_valid,
                        const struct roth_api_v1 *api);

#endif
