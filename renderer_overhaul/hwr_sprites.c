#include "hwr_sprites.h"

#include <string.h>

/* Runtime tables the resolver reads. The status table classifies each archive id
 * (renderer.c:5787); its low byte is the class and its high byte the descriptor
 * index. The two descriptor-table pointer globals hold the base of the directory
 * the 0xfe path indirects through (renderer.c:5885-5888); the monster static
 * table backs the 0xfc path (renderer.c:5849). The part table maps a
 * non-directional selector to a child index (renderer.c:5910). */
#define VA_G_STATUS_TABLE         0x86d30u
#define VA_G_DESC_TABLE_MAP       0x90944u  /* g_world_surface_draw_flags+0x8 */
#define VA_G_DESC_TABLE_SHARED    0x85cecu  /* g_map_das_dir_table_buffer+0x4 */
#define VA_G_MONSTER_STATIC_TABLE 0x85cf4u  /* g_ademo_das_fat_buffer+0x4 */
#define VA_G_PART_TABLE           0x909b4u  /* g_floor_tex_caps+0x2 */
/* The live cache slot directory (renderer.c:2b7e0): stride 6, holding a handle
 * whose deref is the resident block pointer. */
#define VA_G_DAS_CACHE_SLOTS      0x89930u
/* Actor state pool (0x91e04, stride 0x22). The map-object +0xc backref is
 * actor_index*0x22 + 1, so the slot record VA is 0x91e03 + backref. */
#define VA_G_ACTOR_SLOT_BIAS      0x91e03u

/* Selector-word flags (renderer.c:5894-5908). They live on the selector word, not
 * on the resolved child entry. */
#define HWR_SEL_DIRECTIONAL 0x8000u
#define HWR_SEL_FINE        0x2000u
#define HWR_SEL_ACTOR       0x4000u

/* The engine's monster static-appearance stride: 13 direction words of 8 bytes
 * (renderer.c:5849, 0x68 = 13*8), with the directional frame words beginning at
 * +0x14 within each record. */
#define HWR_MONSTER_STRIDE 0x68u
#define HWR_MONSTER_FRAME  0x14u

static uint16_t hwr_sprites_u16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
}

int hwr_sprites_locate(struct hwr_das_archive *map, struct hwr_das_archive *shared,
                       uint16_t archive_id, struct hwr_das_archive **owner,
                       size_t *entry_id)
{
    if (owner == NULL || entry_id == NULL)
        return -1;
    if (archive_id < HWR_SPRITE_SHARED_FIRST) {
        *owner = map;
        *entry_id = archive_id;
    } else if (archive_id < HWR_SPRITE_ID_LIMIT) {
        *owner = shared;
        *entry_id = (size_t)(archive_id - HWR_SPRITE_SHARED_FIRST);
    } else {
        return -1;
    }
    if (*owner == NULL || *entry_id >= (*owner)->entry_count)
        return -1;
    return 0;
}

/* Splits an animated block into its two schemes the same way hwr_das does: the
 * discriminator word at payload+0xc equals the sentinel for run-length sub-image
 * frames, any other value counts delta frames (hwr_das.c:566). */
static void hwr_sprites_tally_animated(const struct hwr_das_entry *entry,
                                       struct hwr_sprite_census *census)
{
    census->animated++;
    if (entry->raw_payload != NULL && entry->raw_payload_size >= 0x0eu &&
        hwr_sprites_u16(entry->raw_payload + 0x0cu) == 0xfffeu)
        census->animated_rle++;
    else
        census->animated_delta++;
}

void hwr_sprites_predecode(struct hwr_das_archive *archive, size_t first_id,
                           struct hwr_sprite_census *census)
{
    size_t id;
    if (census == NULL)
        return;
    if (archive == NULL)
        return;
    for (id = first_id; id < archive->entry_count; id++) {
        int result = hwr_das_decode(archive, id);
        enum hwr_das_class klass = archive->entries[id].resource_class;
        if (result != 0) {
            census->failures++;
            continue;
        }
        switch (klass) {
        case HWR_DAS_CLASS_PLAIN:
            census->plain++;
            census->sprite_frames++;
            break;
        case HWR_DAS_CLASS_GROUPED:
            census->grouped++;
            break;
        case HWR_DAS_CLASS_ANIMATED:
            hwr_sprites_tally_animated(&archive->entries[id], census);
            census->sprite_frames += archive->entries[id].frame_count;
            break;
        case HWR_DAS_CLASS_PLACEHOLDER_SPACE:
        case HWR_DAS_CLASS_PLACEHOLDER_DOLLAR:
            census->placeholder++;
            break;
        default:
            census->other++;
            break;
        }
    }
}

/* Reads a little-endian u16 directly from a stored engine host pointer. The
 * monster/dir-object descriptor tables are separate engine heap allocations that
 * live OUTSIDE the bounded game_ram accessor window (the plugin shares the
 * process address space, exactly as the world-geometry sections are reached), so
 * they cannot go through the window-checked peek. A null/low pointer reads 0. */
static uint16_t hwr_sprites_host_u16(uint32_t host_ptr, uint32_t offset)
{
    if (host_ptr < 0x10000u)
        return 0;
    {
        const uint8_t *p = (const uint8_t *)(uintptr_t)(host_ptr + offset);
        return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
    }
}

static uint32_t hwr_sprites_host_u32(uint32_t host_ptr, uint32_t offset)
{
    if (host_ptr < 0x10000u)
        return 0;
    {
        const uint8_t *p = (const uint8_t *)(uintptr_t)(host_ptr + offset);
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
               ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
}

/* The engine's per-face surface-id priority ladder (resolve_face_surface_id,
 * renderer.c:12700-12742, VA 0x4f0ab). `rec_va` is the live actor slot record;
 * `base` is the monster mapping-table entry (host ptr); `ci` is the coarse
 * direction. The field is picked by the actor STATE flags byte at rec+8, with
 * the priority 0x20 > 0x40 > 8 > 0x80 > ==5, each with a bit0 sub-select and a
 * zero-fallback to the default word[base+ci+0x14]; a set cache byte rec+0x21
 * validates the cached id first. Returns 0 (invalid) when every field is zero
 * or a read leaves the window. */
static uint16_t hwr_sprites_resolve_face_id(const struct roth_api_v1 *api,
                                            uint32_t rec_va, uint32_t base,
                                            uint32_t ci)
{
    uint32_t flags = api->game_ram->u32(rec_va + 8u);
    uint8_t bl = (uint8_t)flags;
    uint16_t v;
    if (api->game_ram->u8(rec_va + 0x21u) != 0) {          /* cached id -> validate */
        uint16_t primary = hwr_sprites_host_u16(base, ci + 0x14u);
        uint16_t cached = hwr_sprites_host_u16(base, ci + 0x44u);
        return (cached == 0 || cached == primary) ? primary : cached;
    }
    if (bl & 0xfcu) {
        if (bl & 0x20u) {
            if ((bl & 1u) && (v = hwr_sprites_host_u16(base, 0x58u)) != 0) return v;
            if ((v = hwr_sprites_host_u16(base, 0x54u)) != 0) return v;
            goto default_lookup;
        }
        if (bl & 0x40u) {
            if ((v = hwr_sprites_host_u16(base, (bl & 1u) ? 0x5eu : 0x5cu)) != 0) return v;
            goto dispatch_default;
        }
        if (bl & 8u) {
            if ((bl & 1u) && (v = hwr_sprites_host_u16(base, ci + 0x34u)) != 0) return v;
            if ((v = hwr_sprites_host_u16(base, ci + 0x24u)) != 0) return v;
            goto default_lookup;
        }
        if (bl & 0x80u) {
            if ((v = hwr_sprites_host_u16(base, ci + 0x44u)) != 0) return v;
            goto default_lookup;
        }
        if (bl == 5u) {
            if ((v = hwr_sprites_host_u16(base, 0x62u)) != 0) return v;
            goto default_lookup;
        }
    }
dispatch_default:
    if (flags & 0x20000u) {
        if ((v = hwr_sprites_host_u16(base, ci + 4u)) != 0) return v;
    }
default_lookup:
    return hwr_sprites_host_u16(base, ci + 0x14u);
}

int hwr_sprites_live_frame_index(const struct roth_api_v1 *api,
                                 uint16_t archive_id)
{
    uint16_t status;
    uint8_t low;
    uint32_t handle, block;
    if (api == NULL || api->game_ram == NULL)
        return -1;
    status = api->game_ram->u16(VA_G_STATUS_TABLE + (uint32_t)archive_id * 2u);
    low = (uint8_t)status;
    if (low >= 0xfcu)
        return -1;                                          /* not resident */
    handle = api->game_ram->u32(VA_G_DAS_CACHE_SLOTS + (uint32_t)low * 6u);
    if (handle < 0x10000u)
        return -1;
    /* The Pool handle and the block it points at are out-of-window engine heap
     * (as the descriptor tables), so read the block pointer and its header
     * current-frame index word (+0x18) by direct host deref. */
    block = hwr_sprites_host_u32(handle, 0u);
    if (block < 0x10000u)
        return -1;
    return (int)hwr_sprites_host_u16(block, 0x18u);
}

/* The 8- and 16-direction frame index (renderer.c:5896-5907). The direction is
 * picked relative to the sprite's on-screen position, so the caller's per-sprite
 * screen-angle correction is subtracted before quantizing. Domain is 0x200. */
static uint32_t hwr_sprites_direction_index(uint8_t facing, uint16_t yaw,
                                            int32_t screen_angle_corr, int fine)
{
    uint32_t t = (uint32_t)facing * 2u + (fine ? 0x110u : 0x120u);
    t = (uint16_t)(t - yaw);
    t = (uint32_t)((int32_t)t - screen_angle_corr);
    return fine ? ((t >> 4) & 0x1eu) : ((t >> 5) & 0xeu);
}

/* Resolves a grouped block's directional/part child, mirroring the mesh path's
 * grouped resolver but with the sprite directional index. The selector sits at
 * payload+6 and the child table at payload+8 (the engine's block+0x10/+0x12 seen
 * past the ten-byte runtime header). Records the child's flip bit. */
static int hwr_sprites_resolve_grouped(const struct roth_api_v1 *api,
                                       const struct hwr_das_entry *entry,
                                       uint8_t facing, uint16_t yaw,
                                       int32_t screen_angle_corr,
                                       size_t *child_offset, int *flip)
{
    uint16_t selector;
    uint16_t child_word;
    size_t child_word_offset;
    int child_flip = 0;
    if (entry->raw_payload == NULL || entry->raw_payload_size < 8u)
        return -1;
    selector = hwr_sprites_u16(entry->raw_payload + 6u);
    if ((selector & HWR_SEL_DIRECTIONAL) != 0) {
        uint32_t ci = hwr_sprites_direction_index(facing, yaw, screen_angle_corr,
                                                  (selector & HWR_SEL_FINE) != 0);
        child_word_offset = 8u + ci;
    } else if ((selector & HWR_SEL_ACTOR) != 0) {
        /* Selector bit 0x4000 is the mesh-face SUB_TEXTURE_INDEX path (a
         * Graphics-Folder sub-image pick, DAS.md FACE +0x1c) -- NOT an actor
         * index; the record it reads lives in the geometry arena, never the
         * actor pool, and a monster never takes it. The mesh renderer owns the
         * real sub-image selection; a grouped block reached here keeps its first
         * variant as a stand-in. */
        child_word_offset = 8u;
    } else {
        uint16_t part = api->game_ram->u16(VA_G_PART_TABLE + (uint32_t)selector);
        child_word_offset = 8u + (size_t)part;
    }
    if (child_word_offset > entry->raw_payload_size ||
        entry->raw_payload_size - child_word_offset < 2u)
        return -1;
    child_word = hwr_sprites_u16(entry->raw_payload + child_word_offset);
    if (hwr_das_group_child_offset(entry, child_word, child_offset, &child_flip) != 0)
        return -1;
    *flip ^= child_flip;
    return 0;
}

/* Fills a lookup for a leaf archive id (already reclassified out of any monster
 * or dir-object placeholder). Grouped leaves resolve their directional child;
 * animated leaves keep their base frame for the animation clock; plain leaves are
 * a single frame. */
static int hwr_sprites_resolve_leaf(const struct roth_api_v1 *api,
                                    struct hwr_das_archive *map,
                                    struct hwr_das_archive *shared,
                                    uint16_t archive_id, uint8_t facing,
                                    uint16_t yaw, int32_t screen_angle_corr,
                                    struct hwr_sprite_lookup *out)
{
    struct hwr_das_archive *owner;
    size_t entry_id;
    struct hwr_das_entry *entry;
    if (hwr_sprites_locate(map, shared, archive_id, &owner, &entry_id) != 0)
        return -1;
    if (hwr_das_decode(owner, entry_id) != 0)
        return -1;
    entry = &owner->entries[entry_id];
    out->archive = owner;
    out->entry_id = entry_id;
    out->archive_id = archive_id;
    out->klass = entry->resource_class;
    if (entry->resource_class == HWR_DAS_CLASS_GROUPED) {
        if (hwr_sprites_resolve_grouped(api, entry, facing, yaw, screen_angle_corr,
                                        &out->child_offset, &out->flip) != 0)
            return -1;
    }
    out->resolved = 1;
    return 0;
}

int hwr_sprites_resolve(const struct roth_api_v1 *api,
                        struct hwr_das_archive *map, struct hwr_das_archive *shared,
                        uint16_t graphic_id, uint8_t facing, uint16_t yaw,
                        int32_t screen_angle_corr, uint16_t backref,
                        struct hwr_sprite_lookup *out)
{
    uint16_t archive_id;
    uint16_t status;
    uint8_t klass_byte;
    if (out == NULL || api == NULL || api->game_ram == NULL)
        return -1;
    memset(out, 0, sizeof *out);
    if (map == NULL && shared == NULL)
        return -1;

    /* Object graphic ids are biased up into the archive id space. */
    archive_id = (uint16_t)(graphic_id + HWR_SPRITE_ID_BIAS);
    status = api->game_ram->u16(VA_G_STATUS_TABLE + (uint32_t)archive_id * 2u);
    klass_byte = (uint8_t)status;

    if (klass_byte == 0xfcu) {
        /* Monster set (renderer.c:5827-5880). The coarse 8-direction index is
         * always used. With a live actor backref (map-object +0xc) the field is
         * chosen from that actor's state byte by the priority ladder
         * (resolve_face_surface_id, renderer.c:12700); an unlinked/out-of-range
         * backref falls back to the static appearance table's default word
         * (word[ent + ci + 0x14]), the engine's own pool-empty path. */
        uint32_t aid = (uint32_t)(status >> 8);
        uint32_t ent = api->game_ram->u32(VA_G_MONSTER_STATIC_TABLE);
        uint32_t ci = hwr_sprites_direction_index(facing, yaw, screen_angle_corr, 0);
        uint16_t nid = 0;
        if (ent < 0x10000u)
            return -1;
        ent += aid * HWR_MONSTER_STRIDE;
        if (backref != 0u)
            nid = hwr_sprites_resolve_face_id(api,
                      VA_G_ACTOR_SLOT_BIAS + (uint32_t)backref, ent, ci);
        if (nid == 0u)                                     /* pool-empty fallback */
            nid = hwr_sprites_host_u16(ent, ci + HWR_MONSTER_FRAME);
        if (nid == 0u)
            return -1;
        if ((nid & 0x8000u) != 0) {
            out->flip ^= 1;
            nid &= 0x7fffu;
        }
        return hwr_sprites_resolve_leaf(api, map, shared, nid, facing, yaw,
                                        screen_angle_corr, out);
    }

    if (klass_byte == 0xfeu) {
        /* Dir-object set: indirect through the descriptor directory, then pick
         * the directional (or part-table) child id (renderer.c:5885-5913). */
        uint32_t aid = (uint32_t)(status >> 8);
        uint32_t table = api->game_ram->u32(archive_id < HWR_SPRITE_SHARED_FIRST
                                            ? VA_G_DESC_TABLE_MAP
                                            : VA_G_DESC_TABLE_SHARED);
        uint16_t rec_off;
        uint16_t selector;
        uint16_t nid;
        uint32_t ci;
        if (table < 0x10000u)
            return -1;
        /* The descriptor directory is an out-of-window engine allocation, read
         * by direct host deref (as the monster table above). */
        rec_off = hwr_sprites_host_u16(table, aid * 2u);
        selector = hwr_sprites_host_u16(table, rec_off);
        if ((selector & HWR_SEL_DIRECTIONAL) != 0) {
            ci = hwr_sprites_direction_index(facing, yaw, screen_angle_corr,
                                             (selector & HWR_SEL_FINE) != 0);
        } else {
            ci = api->game_ram->u16(VA_G_PART_TABLE + (uint32_t)selector);
        }
        nid = hwr_sprites_host_u16(table, (uint32_t)rec_off + ci + 2u);
        if (nid == 0u)
            return -1;
        if ((nid & 0x8000u) != 0) {
            out->flip ^= 1;
            nid &= 0x7fffu;
        }
        return hwr_sprites_resolve_leaf(api, map, shared, nid, facing, yaw,
                                        screen_angle_corr, out);
    }

    /* Any other class byte (a resident cache slot, or an id the mod already holds
     * decoded) is a leaf: the mod classifies it from the archive rather than the
     * runtime cache slot. */
    return hwr_sprites_resolve_leaf(api, map, shared, archive_id, facing, yaw,
                                    screen_angle_corr, out);
}

int hwr_sprites_frame(const struct hwr_sprite_lookup *lookup,
                      struct hwr_sprite_frame *frame)
{
    const struct hwr_das_entry *entry;
    if (lookup == NULL || frame == NULL || !lookup->resolved ||
        lookup->archive == NULL || lookup->entry_id >= lookup->archive->entry_count)
        return -1;
    entry = &lookup->archive->entries[lookup->entry_id];
    memset(frame, 0, sizeof *frame);
    switch (lookup->klass) {
    case HWR_DAS_CLASS_PLAIN:
        if (entry->raw_payload == NULL || entry->raw_payload_size < 6u)
            return -1;
        frame->width = entry->width;
        frame->height = entry->height;
        frame->flags = entry->payload_flags;
        frame->pixels = entry->raw_payload + 6u;
        return 0;
    case HWR_DAS_CLASS_ANIMATED: {
        uint16_t index = lookup->frame_index < entry->frame_count
                       ? lookup->frame_index : 0;
        if (entry->frames == NULL || entry->frame_count == 0 ||
            entry->frames[index].pixels == NULL)
            return -1;
        frame->width = entry->frames[index].width;
        frame->height = entry->frames[index].height;
        frame->flags = entry->payload_flags;
        frame->pixels = entry->frames[index].pixels;
        return 0;
    }
    case HWR_DAS_CLASS_GROUPED: {
        struct hwr_das_texture_view view;
        if (hwr_das_group_child_view(entry, lookup->child_offset, &view) != 0)
            return -1;
        frame->width = view.width;
        frame->height = view.height;
        frame->flags = view.flags;
        frame->pixels = view.pixels;
        return 0;
    }
    default:
        return -1;
    }
}
