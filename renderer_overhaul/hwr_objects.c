#include "hwr_objects.h"

#include "hwr_sprites.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Persistent state the enumeration reads. The object section is a per-map heap
 * allocation the engine publishes as a pointer (like the geometry sections), so
 * it is dereferenced directly and its interior offsets bounded against the
 * section window. The particle list head and the pickup overlay live inside the
 * bounded game_ram window and are read through the accessors. */
#define VA_G_MAP_OBJECTS_BUFFER 0x90aa4u
#define VA_G_PARTICLE_POOL      0x91864u
#define VA_G_PICKUP_FLY_HEAD    0x71144u
#define VA_G_ITEM_PICKUP_FLAGS  0x7fd84u
#define VA_G_PLAYER_MIRROR_NODE 0x853dcu
#define VA_G_PLAYER_MIRROR_APPEARANCE (VA_G_PLAYER_MIRROR_NODE + 8u)

/* The object section is bounded the same way the geometry sections are. */
#define HWR_OBJ_SECTION_EXTENT 0x10000u

/* Map-object / appearance-record layout (stride 0x10). The pickup overlay's
 * appearance sub-record shares this layout because it is copied from a map
 * record when the item is collected. */
#define HWR_OBJ_RECORD_STRIDE 0x10u
#define HWR_OBJ_OFF_X      0x00u
#define HWR_OBJ_OFF_Y      0x02u
#define HWR_OBJ_OFF_GFX    0x04u
#define HWR_OBJ_OFF_FACING 0x06u
#define HWR_OBJ_OFF_FLAGS7 0x07u  /* map: bit0x80 hidden; appearance: bit0 hidden */
#define HWR_OBJ_OFF_FLAGS9 0x09u  /* bit1 = reflection-skip */
#define HWR_OBJ_OFF_Z      0x0au
#define HWR_OBJ_OFF_BACKREF 0x0cu  /* actor pool backref (actor_index*0x22 + 1) */
#define HWR_OBJ_OFF_ID     0x0eu
#define HWR_OBJ_OFF_SEED   0x08u   /* billboard shade seed (type-1 path) */

/* Particle record layout: the rendered position is the integer triple, and the
 * busy byte marks a live slot. The 16.16 accumulators at +0x24.. are physics
 * state, not render position. */
#define HWR_PARTICLE_OFF_NEXT   0x00u
#define HWR_PARTICLE_OFF_X      0x08u
#define HWR_PARTICLE_OFF_Y      0x0au
#define HWR_PARTICLE_OFF_GFX    0x0cu
#define HWR_PARTICLE_OFF_FACING 0x0eu
/* The particle's appearance record starts at node+8 (its X), so the billboard
 * shade seed (appearance +8, like a map record's +8) is node+0x10. */
#define HWR_PARTICLE_OFF_SEED   0x10u
#define HWR_PARTICLE_OFF_Z      0x12u
#define HWR_PARTICLE_OFF_BUSY   0x30u

/* A malformed active list must not spin forever; the pool is small in practice. */
#define HWR_PARTICLE_WALK_LIMIT 0x8000u

static uint16_t hwr_obj_u16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
}

static int16_t hwr_obj_i16(const uint8_t *bytes)
{
    return (int16_t)hwr_obj_u16(bytes);
}

static int hwr_objects_fail(struct hwr_objects *objects, const char *format, ...)
{
    va_list args;
    objects->valid = 0;
    va_start(args, format);
    vsnprintf(objects->error, sizeof objects->error, format, args);
    va_end(args);
    return -1;
}

void hwr_objects_clear(struct hwr_objects *objects)
{
    if (objects == NULL)
        return;
    free(objects->items);
    objects->items = NULL;
    objects->count = 0;
    objects->capacity = 0;
    free(objects->hidden);
    objects->hidden = NULL;
    objects->hidden_count = 0;
    objects->hidden_capacity = 0;
    objects->numbered = 0;
    memset(objects->source_counts, 0, sizeof objects->source_counts);
    objects->valid = 0;
    objects->error[0] = 0;
}

void hwr_objects_set_enabled(struct hwr_objects *objects, int enabled)
{
    if (objects != NULL)
        objects->enabled = enabled;
}

static int hwr_objects_push(struct hwr_objects *objects, const struct hwr_object *item)
{
    if (objects->count == objects->capacity) {
        size_t next = objects->capacity != 0 ? objects->capacity * 2u : 256u;
        struct hwr_object *grown = (struct hwr_object *)realloc(
            objects->items, next * sizeof *grown);
        if (grown == NULL)
            return -1;
        objects->items = grown;
        objects->capacity = next;
    }
    objects->items[objects->count++] = *item;
    objects->source_counts[item->source]++;
    return 0;
}

/* One more number the map holds and this walk will not draw. The list is grown
 * rather than sized from a count, because the count is only known once the walk
 * that finds them is over. */
static int hwr_objects_push_hidden(struct hwr_objects *objects, uint32_t number)
{
    if (objects->hidden_count == objects->hidden_capacity) {
        size_t next = objects->hidden_capacity != 0
                    ? objects->hidden_capacity * 2u : 32u;
        uint32_t *grown = (uint32_t *)realloc(objects->hidden,
                                              next * sizeof *grown);

        if (grown == NULL)
            return -1;
        objects->hidden = grown;
        objects->hidden_capacity = next;
    }
    objects->hidden[objects->hidden_count++] = number;
    return 0;
}

/* Reads the object section base. It is a raw host pointer the engine stores in a
 * pointer global; the plugin shares the address space and dereferences it, as the
 * geometry snapshot does for its sections. */
static const uint8_t *hwr_objects_section(const struct roth_api_v1 *api)
{
    uintptr_t stored = (uintptr_t)api->game_ram->u32(VA_G_MAP_OBJECTS_BUFFER);
    return stored >= 0x1000u ? (const uint8_t *)stored : NULL;
}

static int hwr_objects_section_fits(uint32_t offset, uint32_t size)
{
    return offset <= HWR_OBJ_SECTION_EXTENT &&
           size <= HWR_OBJ_SECTION_EXTENT - offset;
}

/* Enumerates every sector's object list. The list is iterated for every sector,
 * never gated on the sector's has-objects bit, so a stale-flag sector cannot hide
 * its objects. Hidden records (flag +7 bit0x80) are skipped.
 *
 * THE ORDER OF THIS WALK IS A PUBLISHED FACT, not an implementation detail: the
 * running counter below is the map-wide object NUMBER, and it is the number the
 * community map editor shows as `Object: N` -- the per-sector object lists
 * flattened with the sectors in ascending order. A game config addresses one
 * placed object by it, so the walk may not be reordered without moving what
 * every such entry addresses.
 *
 * The counter advances for EVERY record the map holds, including the hidden ones
 * skipped just below and the ones a malformed section costs: what numbers an
 * object is the map's own list, not this walk's willingness to draw it. */
static int hwr_objects_walk_map(struct hwr_objects *objects,
                                const struct hwr_world *world,
                                const struct roth_api_v1 *api)
{
    const uint8_t *section = hwr_objects_section(api);
    uint32_t number = 0;
    size_t sector;
    if (section == NULL)
        return hwr_objects_fail(objects, "object section is not staged");
    for (sector = 0; sector < world->sector_count; sector++) {
        uint32_t dir_offset = 2u + (uint32_t)sector * 2u;
        uint32_t list_offset;
        uint32_t record_offset;
        uint8_t count;
        uint8_t j;
        if (!hwr_objects_section_fits(dir_offset, 2u))
            continue;
        list_offset = hwr_obj_u16(section + dir_offset);
        /* A sector with no list has no directory target to follow -- and holds
         * no objects, so it numbers none either. */
        if (list_offset < 2u || !hwr_objects_section_fits(list_offset, 1u))
            continue;
        count = section[list_offset];
        if (count == 0)
            continue;
        if (!hwr_objects_section_fits(list_offset + 2u,
                                      (uint32_t)count * HWR_OBJ_RECORD_STRIDE)) {
            /* The count was readable and the records are not. The map still
             * says this sector holds that many, so they still take their
             * numbers; skipping them silently would shift every object after
             * them. */
            number += count;
            continue;                               /* records begin at +2, not +1 */
        }
        record_offset = list_offset + 2u;
        for (j = 0; j < count; j++, record_offset += HWR_OBJ_RECORD_STRIDE) {
            const uint8_t *record = section + record_offset;
            struct hwr_object item;
            uint32_t mine = number++;
            if (record[HWR_OBJ_OFF_FLAGS7] & 0x80u) {
                /* Numbered, never drawn -- and remembered, so that an entry of
                 * a game config naming one is told what it named rather than
                 * told the map has no such object. */
                if (hwr_objects_push_hidden(objects, mine) != 0)
                    return hwr_objects_fail(objects, "out of memory enumerating objects");
                continue;                                    /* hidden */
            }
            memset(&item, 0, sizeof item);
            item.map_index = mine;
            item.source = HWR_OBJECT_MAP;
            item.world_x = hwr_obj_i16(record + HWR_OBJ_OFF_X);
            item.world_y = hwr_obj_i16(record + HWR_OBJ_OFF_Y);
            item.world_z = hwr_obj_i16(record + HWR_OBJ_OFF_Z);
            item.graphic_id = hwr_obj_u16(record + HWR_OBJ_OFF_GFX);
            item.facing = record[HWR_OBJ_OFF_FACING];
            item.object_id = hwr_obj_u16(record + HWR_OBJ_OFF_ID);
            item.seed = record[HWR_OBJ_OFF_SEED];
            item.sector_index = (uint16_t)sector;
            item.backref = hwr_obj_u16(record + HWR_OBJ_OFF_BACKREF);
            item.flags7 = record[HWR_OBJ_OFF_FLAGS7];
            item.flags9 = record[HWR_OBJ_OFF_FLAGS9];
            item.appearance_ptr = (uint32_t)(uintptr_t)record;
            if (record[HWR_OBJ_OFF_FLAGS9] & 2u)
                item.flags |= HWR_OBJECT_FLAG_REFLECTION_SKIP;
            if (record[HWR_OBJ_OFF_FLAGS7] & 1u)
                item.flags |= HWR_OBJECT_FLAG_FLAT;         /* type-3 fixed-orientation */
            if (hwr_objects_push(objects, &item) != 0)
                return hwr_objects_fail(objects, "out of memory enumerating objects");
        }
    }
    objects->numbered = number;
    return 0;
}

/* Converts a stored host pointer into a bounded game_ram offset. Returns 0 and
 * writes the offset when the pointer resolves inside the window; -1 otherwise, so
 * a heap pointer (or a null link) fails closed rather than reading wild memory. */
static int hwr_objects_va_of(const struct roth_api_v1 *api, uint32_t host_ptr,
                             uint32_t need, uint32_t *offset)
{
    uintptr_t base = (uintptr_t)api->game_ram->to_ptr(0);
    if (base == 0 || (uintptr_t)host_ptr < base)
        return -1;
    {
        uint64_t va = (uint64_t)((uintptr_t)host_ptr - base);
        if (va + need > (uint64_t)api->game_ram->size())
            return -1;
        *offset = (uint32_t)va;
        return 0;
    }
}

/* Walks the singly-linked particle list from its head, emitting each live
 * (busy) node as a billboard at its integer position. */
static int hwr_objects_walk_particles(struct hwr_objects *objects,
                                      const struct roth_api_v1 *api)
{
    uint32_t node = api->game_ram->u32(VA_G_PARTICLE_POOL);
    uint32_t guard;
    for (guard = 0; node != 0 && guard < HWR_PARTICLE_WALK_LIMIT; guard++) {
        uint32_t off;
        uint32_t next;
        if (hwr_objects_va_of(api, node, HWR_PARTICLE_OFF_BUSY + 1u, &off) != 0)
            break;
        next = api->game_ram->u32(off + HWR_PARTICLE_OFF_NEXT);
        if (api->game_ram->u8(off + HWR_PARTICLE_OFF_BUSY) != 0) {
            struct hwr_object item;
            memset(&item, 0, sizeof item);
            item.source = HWR_OBJECT_PARTICLE;
            item.map_index = HWR_OBJECT_NO_INDEX;  /* no map numbers this */
            item.sector_index = (uint16_t)HWR_OBJECT_NO_SECTOR;
            item.world_x = (int16_t)api->game_ram->u16(off + HWR_PARTICLE_OFF_X);
            item.world_y = (int16_t)api->game_ram->u16(off + HWR_PARTICLE_OFF_Y);
            item.world_z = (int16_t)api->game_ram->u16(off + HWR_PARTICLE_OFF_Z);
            item.graphic_id = api->game_ram->u16(off + HWR_PARTICLE_OFF_GFX);
            item.facing = api->game_ram->u8(off + HWR_PARTICLE_OFF_FACING);
            item.seed = api->game_ram->u8(off + HWR_PARTICLE_OFF_SEED);
            item.appearance_ptr = node + 8u;
            if (hwr_objects_push(objects, &item) != 0)
                return hwr_objects_fail(objects, "out of memory enumerating particles");
        }
        node = next;
    }
    return 0;
}

/* Reads the pickup-fly overlay's persistent form. It is armed by a flag bit; when
 * set, the head node's appearance sub-record (record layout, eight bytes into the
 * node) carries the flying item's billboard. */
static int hwr_objects_walk_pickup(struct hwr_objects *objects,
                                   const struct roth_api_v1 *api)
{
    uint32_t appearance = VA_G_PICKUP_FLY_HEAD + 8u;
    struct hwr_object item;
    if ((api->game_ram->u8(VA_G_ITEM_PICKUP_FLAGS) & 1u) == 0)
        return 0;
    if (api->game_ram->u8(appearance + HWR_OBJ_OFF_FLAGS7) & 1u)
        return 0;                                            /* not visible */
    memset(&item, 0, sizeof item);
    item.source = HWR_OBJECT_PICKUP_FLY;
    item.map_index = HWR_OBJECT_NO_INDEX;   /* no map numbers this */
    item.sector_index = (uint16_t)HWR_OBJECT_NO_SECTOR;
    item.world_x = (int16_t)api->game_ram->u16(appearance + HWR_OBJ_OFF_X);
    item.world_y = (int16_t)api->game_ram->u16(appearance + HWR_OBJ_OFF_Y);
    item.world_z = (int16_t)api->game_ram->u16(appearance + HWR_OBJ_OFF_Z);
    item.graphic_id = api->game_ram->u16(appearance + HWR_OBJ_OFF_GFX);
    item.facing = api->game_ram->u8(appearance + HWR_OBJ_OFF_FACING);
    item.object_id = api->game_ram->u16(appearance + HWR_OBJ_OFF_ID);
    item.seed = api->game_ram->u8(appearance + HWR_OBJ_OFF_SEED);
    item.appearance_ptr =
        (uint32_t)(uintptr_t)api->game_ram->to_ptr(appearance);
    if (hwr_objects_push(objects, &item) != 0)
        return hwr_objects_fail(objects, "out of memory enumerating pickup overlay");
    return 0;
}

/* The stock reflected walk creates a third-person player as a normal type-1
 * billboard in this synthetic node. By the hardware callback the software
 * pass has already selected and advanced its settled animation state, so this
 * layer only reads the record and never advances it again. */
static int hwr_objects_walk_reflected_player(
    struct hwr_objects *objects, const struct hwr_world *world,
    const struct roth_api_v1 *api)
{
    const uint32_t appearance = VA_G_PLAYER_MIRROR_APPEARANCE;
    uint16_t sector_offset =
        api->game_ram->u16(VA_G_PLAYER_MIRROR_NODE + 4u);
    uint16_t sector_index;
    struct hwr_object item;

    if (sector_offset == 0 || world->sector_offset_to_index == NULL)
        return 0;
    sector_index = world->sector_offset_to_index[sector_offset];
    if (sector_index == UINT16_MAX || sector_index >= world->sector_count)
        return 0;
    if (api->game_ram->u16(appearance + HWR_OBJ_OFF_GFX) <= 0x0200u)
        return 0;
    memset(&item, 0, sizeof item);
    item.source = HWR_OBJECT_PLAYER_REFLECTION;
    item.map_index = HWR_OBJECT_NO_INDEX;   /* no map numbers this */
    item.world_x =
        (int16_t)api->game_ram->u16(appearance + HWR_OBJ_OFF_X);
    item.world_y =
        (int16_t)api->game_ram->u16(appearance + HWR_OBJ_OFF_Y);
    item.world_z =
        (int16_t)api->game_ram->u16(appearance + HWR_OBJ_OFF_Z);
    item.graphic_id =
        api->game_ram->u16(appearance + HWR_OBJ_OFF_GFX);
    item.facing = api->game_ram->u8(appearance + HWR_OBJ_OFF_FACING);
    item.object_id =
        api->game_ram->u16(appearance + HWR_OBJ_OFF_ID);
    item.seed = api->game_ram->u8(appearance + HWR_OBJ_OFF_SEED);
    item.sector_index = sector_index;
    item.backref =
        api->game_ram->u16(appearance + HWR_OBJ_OFF_BACKREF);
    item.flags7 = api->game_ram->u8(appearance + HWR_OBJ_OFF_FLAGS7);
    item.flags9 = api->game_ram->u8(appearance + HWR_OBJ_OFF_FLAGS9);
    item.flags = HWR_OBJECT_FLAG_REFLECTION_ONLY |
                 HWR_OBJECT_FLAG_NO_SHADOW;
    item.appearance_ptr =
        (uint32_t)(uintptr_t)api->game_ram->to_ptr(appearance);
    if (hwr_objects_push(objects, &item) != 0)
        return hwr_objects_fail(
            objects, "out of memory enumerating reflected player");
    return 0;
}

int hwr_objects_rebuild(struct hwr_objects *objects, const struct hwr_world *world,
                        const struct roth_api_v1 *api)
{
    if (objects == NULL || world == NULL || api == NULL || api->game_ram == NULL)
        return -1;
    objects->count = 0;
    objects->hidden_count = 0;
    objects->numbered = 0;
    memset(objects->source_counts, 0, sizeof objects->source_counts);
    objects->valid = 0;
    objects->error[0] = 0;
    if (!objects->enabled) {
        objects->valid = 1;
        return 0;
    }
    if (!world->valid)
        return hwr_objects_fail(objects, "world snapshot is not valid");
    if (hwr_objects_walk_map(objects, world, api) != 0)
        return -1;
    if (hwr_objects_walk_particles(objects, api) != 0)
        return -1;
    if (hwr_objects_walk_pickup(objects, api) != 0)
        return -1;
    if (hwr_objects_walk_reflected_player(objects, world, api) != 0)
        return -1;
    objects->valid = 1;
    return 0;
}

uint8_t hwr_object_composed_light(uint8_t seed, uint8_t sector_light)
{
    return (seed != 0 && sector_light != 0)
         ? (uint8_t)((uint8_t)(seed - 0x80u) + sector_light)
         : seed;
}

static const char *hwr_objects_source_name(uint8_t source)
{
    static const char *const names[HWR_OBJECT_SOURCE_COUNT] = {
        "map", "particle", "pickup-fly", "player-reflection"
    };
    return source < HWR_OBJECT_SOURCE_COUNT ? names[source] : "?";
}

void hwr_objects_dump(const struct hwr_objects *objects, const char *path,
                      const struct roth_api_v1 *api,
                      struct hwr_das_archive *map_archive,
                      struct hwr_das_archive *shared_archive)
{
    char temp_path[2620];
    FILE *file;
    size_t i;
    uint16_t yaw;
    if (objects == NULL || path == NULL || path[0] == 0 || api == NULL ||
        api->game_ram == NULL)
        return;
    snprintf(temp_path, sizeof temp_path, "%s.tmp", path);
    file = fopen(temp_path, "w");
    if (file == NULL)
        return;
    yaw = api->game_ram->u16(0x909f8u);   /* g_sprite_view_angle */
    fprintf(file,
            "objects total=%zu map=%zu particle=%zu pickup-fly=%zu player-reflection=%zu"
            " numbered=%lu hidden=%zu\n",
            objects->count, objects->source_counts[HWR_OBJECT_MAP],
            objects->source_counts[HWR_OBJECT_PARTICLE],
            objects->source_counts[HWR_OBJECT_PICKUP_FLY],
            objects->source_counts[HWR_OBJECT_PLAYER_REFLECTION],
            (unsigned long)objects->numbered, objects->hidden_count);
    for (i = 0; i < objects->count; i++) {
        const struct hwr_object *o = &objects->items[i];
        struct hwr_sprite_lookup lookup;
        struct hwr_sprite_frame frame;
        char number[16];
        int have_frame = 0;
        memset(&frame, 0, sizeof frame);
        if (hwr_sprites_resolve(api, map_archive, shared_archive, o->graphic_id,
                                o->facing, yaw, 0, o->backref, &lookup) == 0 &&
            hwr_sprites_frame(&lookup, &frame) == 0)
            have_frame = 1;
        /* THE OBJECT NUMBER FIRST, because it is what a game config addresses a
         * placed object by and it is the same number the community map editor
         * shows for the same thing. The dump, the config and the editor naming
         * one object by one number is the whole of what makes this file an
         * authoring aid rather than a listing. A dash where no map numbers the
         * thing at all. */
        if (o->map_index == HWR_OBJECT_NO_INDEX)
            snprintf(number, sizeof number, "-");
        else
            snprintf(number, sizeof number, "%lu", (unsigned long)o->map_index);
        /* THE IDENTIFIERS ARE THE ONES THE EDITOR SHOWS, in the spelling it
         * shows them in. The sprite is the texture and source PAIR of the
         * object panel, in that order, which is also how a game config keys the
         * table that answers for a kind of thing; the tag is the panel's `ID:`;
         * and both are decimal, because that is what the panel prints and this
         * file exists to be read beside it.
         *
         * THE POSITION IS THE GAME'S OWN, straight out of the record. It is the
         * space the world is measured in and the space a game config's `placed`
         * entry is written in, so what is printed here can be pasted into one
         * unchanged. The community editor DISPLAYS X with the opposite sign,
         * which is a fact about its screen rather than about the world -- so a
         * number taken off that screen is the one that needs flipping, not this
         * one. */
        fprintf(file,
                "%-5s %-10s x=%d y=%d z=%d facing=%u sprite=%u/%u light=%u id=%u flags=%#x f7=%#04x f9=%#04x",
                number, hwr_objects_source_name(o->source), o->world_x,
                o->world_y, o->world_z, o->facing,
                (unsigned)(o->graphic_id & 0xffu),
                (unsigned)(o->graphic_id >> 8), o->seed,
                o->object_id,
                o->flags, o->flags7, o->flags9);
        if (have_frame)
            fprintf(file, " frame=%u %ux%u fflags=%#x flip=%d\n",
                    lookup.frame_index, frame.width, frame.height, frame.flags,
                    lookup.flip);
        else
            fprintf(file, " frame=unresolved\n");
    }
    /* And the records the map numbers and never draws. They are not objects
     * this layer knows anything else about -- it declined to read them -- but
     * their NUMBERS are what a person needs, because the editor shows them and
     * a config entry naming one can never do anything. */
    for (i = 0; i < objects->hidden_count; i++)
        fprintf(file, "%-5lu hidden     never drawn\n",
                (unsigned long)objects->hidden[i]);
    fclose(file);
    rename(temp_path, path);
}

/* The two sector bytes the engine rewrites while the world is running, held
 * from one dump to the next so a block is written only where one of them moved.
 * Packed as (flags << 8) | light, one entry per sector. */
static uint16_t *g_light_state;
static size_t g_light_state_count;
/* The set of objects the map marks unshaded, as a hash of the records the block
 * would print, so a light appearing or leaving is noticed without keeping the
 * list. */
static uint32_t g_light_unshaded_hash;
static size_t g_light_unshaded_count;
static int g_light_census_written;
static unsigned long g_light_frame;

/* Sector flags: bit 0x02 is the candle-tinted mark, which the engine CLEARS on
 * a room whose light switch has finished raising it and SETS on one it has
 * finished lowering; bit 0x40 marks an emissive face's sector. */
#define HWR_LIGHT_FLAG_TINT 0x02u
#define HWR_LIGHT_FLAG_GLOW 0x40u

static uint32_t hwr_light_mix(uint32_t hash, uint32_t value)
{
    hash ^= value;
    return hash * 16777619u;
}

void hwr_objects_dump_lights_reset(void)
{
    free(g_light_state);
    g_light_state = NULL;
    g_light_state_count = 0;
    g_light_unshaded_hash = 0;
    g_light_unshaded_count = 0;
    g_light_census_written = 0;
    g_light_frame = 0;
}

void hwr_objects_dump_lights(const struct hwr_objects *objects,
                             const struct hwr_world *world, const char *path)
{
    uint32_t unshaded_hash = 2166136261u;
    size_t unshaded_count = 0;
    size_t moved = 0;
    size_t i;
    FILE *file;

    if (objects == NULL || world == NULL || !world->valid || path == NULL ||
        path[0] == 0)
        return;
    g_light_frame++;
    /* A new map's sector 12 is not the old map's sector 12, and a census is the
     * only honest thing to write when the table underneath has been replaced. */
    if (g_light_state_count != world->sector_count) {
        free(g_light_state);
        g_light_state = (uint16_t *)calloc(world->sector_count == 0 ? 1u
                                                                   : world->sector_count,
                                           sizeof *g_light_state);
        if (g_light_state == NULL) {
            g_light_state_count = 0;
            return;
        }
        g_light_state_count = world->sector_count;
        g_light_census_written = 0;
    }
    for (i = 0; i < objects->count; i++) {
        const struct hwr_object *o = &objects->items[i];
        uint8_t sector_light = 0;

        if (o->sector_index != (uint16_t)HWR_OBJECT_NO_SECTOR &&
            o->sector_index < world->sector_count)
            sector_light = world->sectors[o->sector_index].light;
        if (hwr_object_composed_light(o->seed, sector_light) != 0)
            continue;
        unshaded_count++;
        unshaded_hash = hwr_light_mix(unshaded_hash, o->graphic_id);
        unshaded_hash = hwr_light_mix(unshaded_hash, o->seed);
        unshaded_hash = hwr_light_mix(unshaded_hash, o->sector_index);
    }
    if (g_light_census_written) {
        for (i = 0; i < world->sector_count; i++) {
            const struct hwr_sector *s = &world->sectors[i];
            uint16_t now = (uint16_t)(((uint16_t)s->flags << 8) | s->light);

            if (now != g_light_state[i])
                moved++;
        }
        if (moved == 0 && unshaded_hash == g_light_unshaded_hash &&
            unshaded_count == g_light_unshaded_count)
            return;
    }
    file = fopen(path, "a");
    if (file == NULL)
        return;
    if (!g_light_census_written) {
        fprintf(file, "census frame=%lu sectors=%zu objects=%zu unshaded=%zu\n",
                g_light_frame, world->sector_count, objects->count,
                unshaded_count);
        for (i = 0; i < world->sector_count; i++) {
            const struct hwr_sector *s = &world->sectors[i];

            fprintf(file, "sector %zu flags=%#04x tint=%d glow=%d light=%u\n", i,
                    s->flags, (s->flags & HWR_LIGHT_FLAG_TINT) != 0 ? 1 : 0,
                    (s->flags & HWR_LIGHT_FLAG_GLOW) != 0 ? 1 : 0, s->light);
        }
        for (i = 0; i < objects->count; i++) {
            const struct hwr_object *o = &objects->items[i];
            uint8_t sector_light = 0;

            if (o->sector_index != (uint16_t)HWR_OBJECT_NO_SECTOR &&
                o->sector_index < world->sector_count)
                sector_light = world->sectors[o->sector_index].light;
            fprintf(file,
                    "object %s graphic=%#06x seed=%u composed=%u sector=%u\n",
                    hwr_objects_source_name(o->source), o->graphic_id, o->seed,
                    hwr_object_composed_light(o->seed, sector_light),
                    o->sector_index);
        }
    } else {
        fprintf(file, "frame %lu moved=%zu unshaded=%zu\n", g_light_frame, moved,
                unshaded_count);
        for (i = 0; i < world->sector_count; i++) {
            const struct hwr_sector *s = &world->sectors[i];
            uint16_t was = g_light_state[i];
            uint16_t now = (uint16_t)(((uint16_t)s->flags << 8) | s->light);

            if (now == was)
                continue;
            fprintf(file,
                    "sector %zu flags=%#04x->%#04x tint=%d->%d glow=%d->%d "
                    "light=%u->%u\n", i, (unsigned)(was >> 8), s->flags,
                    ((was >> 8) & HWR_LIGHT_FLAG_TINT) != 0 ? 1 : 0,
                    (s->flags & HWR_LIGHT_FLAG_TINT) != 0 ? 1 : 0,
                    ((was >> 8) & HWR_LIGHT_FLAG_GLOW) != 0 ? 1 : 0,
                    (s->flags & HWR_LIGHT_FLAG_GLOW) != 0 ? 1 : 0,
                    (unsigned)(was & 0xffu), s->light);
        }
        if (unshaded_hash != g_light_unshaded_hash ||
            unshaded_count != g_light_unshaded_count) {
            for (i = 0; i < objects->count; i++) {
                const struct hwr_object *o = &objects->items[i];
                uint8_t sector_light = 0;

                if (o->sector_index != (uint16_t)HWR_OBJECT_NO_SECTOR &&
                    o->sector_index < world->sector_count)
                    sector_light = world->sectors[o->sector_index].light;
                if (hwr_object_composed_light(o->seed, sector_light) != 0)
                    continue;
                fprintf(file,
                        "unshaded %s graphic=%#06x seed=%u sector=%u\n",
                        hwr_objects_source_name(o->source), o->graphic_id,
                        o->seed, o->sector_index);
            }
        }
    }
    fclose(file);
    for (i = 0; i < world->sector_count; i++) {
        const struct hwr_sector *s = &world->sectors[i];

        g_light_state[i] = (uint16_t)(((uint16_t)s->flags << 8) | s->light);
    }
    g_light_unshaded_hash = unshaded_hash;
    g_light_unshaded_count = unshaded_count;
    g_light_census_written = 1;
}
