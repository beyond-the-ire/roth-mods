#include "hwr_world.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VA_G_PALETTE_RGB_PTR       0x85488u
#define VA_G_BLEND_PTR             0x85d08u
#define VA_G_GLOW_PTR              0x86d18u
#define VA_G_TINT_PTR              0x86d1cu
#define VA_G_COLORMAP_PTR          0x86d28u
#define VA_G_DAMAGE_FLASH_LEVEL    0x89f3bu
#define VA_G_WORLD_SPAN_COLORKEY   0x90a2cu
#define VA_G_SKY_FAT_INDEX         0x89f34u
#define VA_G_MAP_GEOMETRY_BUFFER   0x90aa8u
#define VA_G_VERTEX_GEOMETRY_BASE  0x90aacu
#define VA_G_SHADE_LEVEL           0x90c16u
#define VA_G_TINT_SHADE_LEVEL      0x90c18u
#define VA_G_DAS_FILL_INDEX        0x89f10u
#define VA_G_CANDLE_GLOW           0x89f08u
#define VA_G_DOOR_COUNT            0x8b3f4u
#define VA_G_DOOR_POOL             0x8b3f8u

#define HWR_SECTION_EXTENT 0x10000u
/* The largest candle word this reads as one. Every map in the game holds eight,
 * twelve, fifteen, sixteen or thirty-two, so a byte's worth is room for eight
 * times the brightest map anyone shipped; past that the word is not a candle
 * word, and a light that reads it as an authoring hint is better off with its
 * own settings than with a number that means something else. */
#define HWR_CANDLE_GLOW_MAX 255u
#define HWR_NO_INDEX UINT16_MAX
#define HWR_DOOR_STRIDE 0x1f6u
#define HWR_DOOR_CORNER_BASE 0xb6u
#define HWR_DOOR_CORNER_STRIDE 0x10u
#define HWR_DOOR_SURFACE_BASE 0xf6u
#define HWR_DOOR_SURFACE_STRIDE 0x40u

static uint16_t hwr_u16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
}

static int16_t hwr_i16(const uint8_t *bytes)
{
    return (int16_t)hwr_u16(bytes);
}

static int hwr_world_fail(struct hwr_world *world, const char *format, ...)
{
    va_list args;
    world->valid = 0;
    va_start(args, format);
    vsnprintf(world->error, sizeof world->error, format, args);
    va_end(args);
    return -1;
}

static int hwr_section_fits(uint16_t offset, uint32_t size, uint32_t extent)
{
    return (uint32_t)offset <= extent && size <= extent - (uint32_t)offset;
}

/* The engine's map sections and colour tables are heap allocations that sit
 * outside the plugin accessor window, so the bounded accessors cannot reach
 * them. The engine stores their real addresses in pointer globals (which ARE
 * inside the window); the plugin shares the process address space and reads
 * the published regions directly, exactly like the framebuffer pointer. */
static const uint8_t *hwr_stored_pointer(const struct roth_api_v1 *api,
                                         uint32_t pointer_global)
{
    uintptr_t stored = (uintptr_t)api->game_ram->u32(pointer_global);
    return stored >= 0x1000u ? (const uint8_t *)stored : NULL;
}

static int hwr_section_read(const uint8_t *section, uint32_t extent,
                            uint16_t offset, void *destination, uint32_t size)
{
    if (section == NULL || !hwr_section_fits(offset, size, extent))
        return -1;
    memcpy(destination, section + offset, size);
    return 0;
}

static void hwr_parse_sector(struct hwr_sector *sector)
{
    sector->ceiling_z = hwr_i16(sector->raw);
    sector->floor_z = hwr_i16(sector->raw + 2);
    sector->ceiling_flat_id = hwr_i16(sector->raw + 6);
    sector->floor_flat_id = hwr_i16(sector->raw + 8);
    sector->flags = sector->raw[0x0a];
    sector->light = sector->raw[0x0b];
    sector->texture_map_override = (int8_t)sector->raw[0x0c];
    sector->wall_count = sector->raw[0x0d];
    sector->wall_offset = hwr_u16(sector->raw + 0x0e);
    sector->ceiling_pan = hwr_u16(sector->raw + 0x10);
    sector->floor_pan = hwr_u16(sector->raw + 0x12);
    sector->sentinel = hwr_i16(sector->raw + 0x14);
    sector->texture_flip_flags = hwr_u16(sector->raw + 0x16);
    sector->slab_offset = hwr_u16(sector->raw + 0x18);
}

static void hwr_parse_wall(struct hwr_wall *wall)
{
    wall->vertex_a_offset = hwr_u16(wall->raw);
    wall->vertex_b_offset = hwr_u16(wall->raw + 2);
    wall->mapping_offset = hwr_u16(wall->raw + 4);
    wall->portal_link_offset = hwr_u16(wall->raw + 8);
    wall->extent = hwr_i16(wall->mapping_raw);
    wall->middle_id = hwr_u16(wall->mapping_raw + 2);
    wall->upper_id = hwr_u16(wall->mapping_raw + 4);
    wall->lower_id = hwr_u16(wall->mapping_raw + 6);
    wall->mapping_flags = wall->mapping_raw[8];
    wall->scale = wall->mapping_raw[9];
    wall->pan_u = wall->mapping_raw[0x0a];
    wall->pan_v = wall->mapping_raw[0x0b];
}

static void hwr_parse_vertex(struct hwr_vertex *vertex)
{
    vertex->world_x = hwr_i16(vertex->raw + 8);
    vertex->world_y = hwr_i16(vertex->raw + 0x0a);
}

static void hwr_parse_slab(struct hwr_slab *slab)
{
    slab->lower_surface_id = hwr_u16(slab->raw);
    slab->lower_z = hwr_i16(slab->raw + 2);
    slab->lower_aux = hwr_u16(slab->raw + 4);
    slab->upper_surface_id = hwr_u16(slab->raw + 6);
    slab->upper_z = hwr_i16(slab->raw + 8);
    slab->upper_aux = hwr_u16(slab->raw + 0x0a);
    slab->flags = slab->raw[0x0c];
}

static int hwr_world_mark_texture(struct hwr_world *world, int16_t id)
{
    uint16_t logical;
    uint8_t mask;
    if (id < 0)
        return 0;
    logical = (uint16_t)id;
    if (logical >= 0x1600u || logical == world->world_span_colorkey)
        return 0;
    mask = (uint8_t)(1u << (logical & 7u));
    if ((world->texture_refs[logical >> 3] & mask) != 0)
        return 0;
    world->texture_refs[logical >> 3] |= mask;
    return 1;
}

static void hwr_world_collect_texture_references(struct hwr_world *world)
{
    size_t i;
    for (i = 0; i < world->sector_count; i++) {
        hwr_world_mark_texture(world, world->sectors[i].ceiling_flat_id);
        hwr_world_mark_texture(world, world->sectors[i].floor_flat_id);
    }
    for (i = 0; i < world->slab_count; i++) {
        hwr_world_mark_texture(world, (int16_t)world->slabs[i].lower_surface_id);
        hwr_world_mark_texture(world, (int16_t)world->slabs[i].upper_surface_id);
    }
    for (i = 0; i < world->wall_count; i++) {
        hwr_world_mark_texture(world, (int16_t)world->walls[i].middle_id);
        hwr_world_mark_texture(world, (int16_t)world->walls[i].upper_id);
        hwr_world_mark_texture(world, (int16_t)world->walls[i].lower_id);
    }
}

void hwr_world_clear(struct hwr_world *world)
{
    if (world == NULL)
        return;
    free(world->sectors);
    free(world->walls);
    free(world->vertices);
    free(world->slabs);
    free(world->sector_offset_to_index);
    free(world->vertex_offset_to_index);
    free(world->vertex_wall_offsets);
    free(world->vertex_wall_indices);
    memset(world, 0, sizeof *world);
}

/* Builds the vertex -> incident-wall CSR index from the parsed wall list. Runs
 * once per snapshot; the mesh builder then finds the walls sharing an endpoint
 * with O(1) lookups instead of a full per-emit scan.  Bucket fill proceeds in
 * ascending wall index, so each vertex lists its walls in the same order the
 * former linear scan visited them. */
static int hwr_world_build_adjacency(struct hwr_world *world)
{
    size_t *offsets;
    uint32_t *indices;
    size_t *cursor;
    size_t total;
    size_t i;
    size_t v;
    if (world->vertex_count == 0)
        return 0;
    offsets = (size_t *)calloc(world->vertex_count + 1u, sizeof *offsets);
    if (offsets == NULL)
        return -1;
    for (i = 0; i < world->wall_count; i++) {
        offsets[world->walls[i].vertex_a_index + 1u]++;
        offsets[world->walls[i].vertex_b_index + 1u]++;
    }
    for (v = 0; v < world->vertex_count; v++)
        offsets[v + 1u] += offsets[v];
    total = offsets[world->vertex_count];
    indices = total != 0 ? (uint32_t *)malloc(total * sizeof *indices) : NULL;
    cursor = (size_t *)malloc(world->vertex_count * sizeof *cursor);
    if ((total != 0 && indices == NULL) || cursor == NULL) {
        free(offsets);
        free(indices);
        free(cursor);
        return -1;
    }
    for (v = 0; v < world->vertex_count; v++)
        cursor[v] = offsets[v];
    for (i = 0; i < world->wall_count; i++) {
        indices[cursor[world->walls[i].vertex_a_index]++] = (uint32_t)i;
        indices[cursor[world->walls[i].vertex_b_index]++] = (uint32_t)i;
    }
    free(cursor);
    world->vertex_wall_offsets = offsets;
    world->vertex_wall_indices = indices;
    return 0;
}

int hwr_world_snapshot(struct hwr_world *world, const struct roth_api_v1 *api)
{
    struct hwr_world next;
    uint32_t sector_extent = HWR_SECTION_EXTENT;
    uint32_t vertex_extent = HWR_SECTION_EXTENT;
    uint16_t first_sector;
    uint16_t first_vertex;
    uint16_t count16;
    uint8_t header_word[2];
    size_t wall_total = 0;
    size_t slab_total = 0;
    size_t i;
    size_t wall_index;
    size_t slab_index;

    if (world == NULL || api == NULL || api->game_ram == NULL)
        return -1;
    hwr_world_clear(world);
    memset(&next, 0, sizeof next);
    next.sector_section = hwr_stored_pointer(api, VA_G_MAP_GEOMETRY_BUFFER);
    next.vertex_section = hwr_stored_pointer(api, VA_G_VERTEX_GEOMETRY_BASE);
    if (next.sector_section == NULL || next.vertex_section == NULL)
        return hwr_world_fail(world, "geometry sections are not staged");

    if (hwr_section_read(next.sector_section, sector_extent, 4,
                         header_word, sizeof header_word) != 0) {
        hwr_world_clear(&next);
        return hwr_world_fail(world, "cannot read sector array offset");
    }
    first_sector = hwr_u16(header_word);
    if (first_sector < 2 ||
        hwr_section_read(next.sector_section, sector_extent,
                         (uint16_t)(first_sector - 2u), &count16, 2) != 0) {
        hwr_world_clear(&next);
        return hwr_world_fail(world, "sector array header is invalid");
    }
    count16 = hwr_u16((const uint8_t *)&count16);
    if (count16 == 0 || !hwr_section_fits(first_sector, (uint32_t)count16 * 0x1au,
                                          sector_extent)) {
        hwr_world_clear(&next);
        return hwr_world_fail(world, "sector array exceeds its section");
    }
    next.sector_count = count16;
    next.sectors = (struct hwr_sector *)calloc(next.sector_count, sizeof *next.sectors);
    next.sector_offset_to_index = (uint16_t *)malloc(HWR_SECTION_EXTENT * sizeof(uint16_t));
    if (next.sectors == NULL || next.sector_offset_to_index == NULL) {
        hwr_world_clear(&next);
        return hwr_world_fail(world, "out of memory allocating sectors");
    }
    memset(next.sector_offset_to_index, 0xff, HWR_SECTION_EXTENT * sizeof(uint16_t));
    for (i = 0; i < next.sector_count; i++) {
        struct hwr_sector *sector = &next.sectors[i];
        uint32_t offset32 = (uint32_t)first_sector + (uint32_t)i * 0x1au;
        sector->section_offset = (uint16_t)offset32;
        sector->slab_index = SIZE_MAX;
        if (hwr_section_read(next.sector_section, sector_extent,
                             sector->section_offset, sector->raw, sizeof sector->raw) != 0) {
            hwr_world_clear(&next);
            return hwr_world_fail(world, "sector %u cannot be read", (unsigned)i);
        }
        hwr_parse_sector(sector);
        next.sector_offset_to_index[sector->section_offset] = (uint16_t)i;
        if (!hwr_section_fits(sector->wall_offset,
                              (uint32_t)sector->wall_count * 0x0cu, sector_extent) ||
            wall_total > HWR_SECTION_EXTENT / 0x0cu - sector->wall_count) {
            hwr_world_clear(&next);
            return hwr_world_fail(world, "sector %u has an invalid wall array", (unsigned)i);
        }
        wall_total += sector->wall_count;
        if (sector->slab_offset != 0) {
            if (!hwr_section_fits(sector->slab_offset, 0x0du, sector_extent)) {
                hwr_world_clear(&next);
                return hwr_world_fail(world, "sector %u has an invalid slab", (unsigned)i);
            }
            slab_total++;
        }
    }

    if (hwr_section_read(next.vertex_section, vertex_extent, 2,
                         header_word, 2) != 0) {
        hwr_world_clear(&next);
        return hwr_world_fail(world, "cannot read vertex array offset");
    }
    first_vertex = hwr_u16(header_word);
    if (first_vertex < 2 ||
        hwr_section_read(next.vertex_section, vertex_extent,
                         (uint16_t)(first_vertex - 2u), &count16, 2) != 0) {
        hwr_world_clear(&next);
        return hwr_world_fail(world, "vertex array header is invalid");
    }
    count16 = hwr_u16((const uint8_t *)&count16);
    if (count16 == 0 || !hwr_section_fits(first_vertex, (uint32_t)count16 * 0x0cu,
                                          vertex_extent)) {
        hwr_world_clear(&next);
        return hwr_world_fail(world, "vertex array exceeds its section");
    }
    next.first_vertex_offset = first_vertex;
    next.vertex_count = count16;
    next.wall_count = wall_total;
    next.slab_count = slab_total;
    next.walls = (struct hwr_wall *)calloc(wall_total, sizeof *next.walls);
    next.vertices = (struct hwr_vertex *)calloc(next.vertex_count, sizeof *next.vertices);
    next.slabs = slab_total != 0
               ? (struct hwr_slab *)calloc(slab_total, sizeof *next.slabs) : NULL;
    next.vertex_offset_to_index = (uint16_t *)malloc(HWR_SECTION_EXTENT * sizeof(uint16_t));
    if ((wall_total != 0 && next.walls == NULL) || next.vertices == NULL ||
        (slab_total != 0 && next.slabs == NULL) || next.vertex_offset_to_index == NULL) {
        hwr_world_clear(&next);
        return hwr_world_fail(world, "out of memory allocating geometry");
    }
    memset(next.vertex_offset_to_index, 0xff, HWR_SECTION_EXTENT * sizeof(uint16_t));
    for (i = 0; i < next.vertex_count; i++) {
        struct hwr_vertex *vertex = &next.vertices[i];
        vertex->section_offset = (uint16_t)((uint32_t)first_vertex + (uint32_t)i * 0x0cu);
        if (hwr_section_read(next.vertex_section, vertex_extent,
                             vertex->section_offset, vertex->raw, sizeof vertex->raw) != 0) {
            hwr_world_clear(&next);
            return hwr_world_fail(world, "vertex %u cannot be read", (unsigned)i);
        }
        hwr_parse_vertex(vertex);
        next.vertex_offset_to_index[vertex->section_offset] = (uint16_t)i;
    }

    wall_index = 0;
    slab_index = 0;
    for (i = 0; i < next.sector_count; i++) {
        struct hwr_sector *sector = &next.sectors[i];
        size_t j;
        sector->first_wall = wall_index;
        if (sector->slab_offset != 0) {
            struct hwr_slab *slab = &next.slabs[slab_index];
            slab->section_offset = sector->slab_offset;
            if (hwr_section_read(next.sector_section, sector_extent,
                                 slab->section_offset, slab->raw, sizeof slab->raw) != 0) {
                hwr_world_clear(&next);
                return hwr_world_fail(world, "sector %u slab cannot be read", (unsigned)i);
            }
            hwr_parse_slab(slab);
            sector->slab_index = slab_index++;
        }
        for (j = 0; j < sector->wall_count; j++, wall_index++) {
            struct hwr_wall *wall = &next.walls[wall_index];
            uint16_t neighbor;
            uint16_t va;
            uint16_t vb;
            wall->section_offset = (uint16_t)((uint32_t)sector->wall_offset +
                                              (uint32_t)j * 0x0cu);
            wall->sector_index = i;
            wall->neighbor_sector_index = SIZE_MAX;
            if (hwr_section_read(next.sector_section, sector_extent,
                                 wall->section_offset, wall->raw, sizeof wall->raw) != 0) {
                hwr_world_clear(&next);
                return hwr_world_fail(world, "wall %u cannot be read", (unsigned)wall_index);
            }
            wall->mapping_offset = hwr_u16(wall->raw + 4);
            if (hwr_section_read(next.sector_section, sector_extent,
                                 wall->mapping_offset, wall->mapping_raw,
                                 sizeof wall->mapping_raw) != 0) {
                hwr_world_clear(&next);
                return hwr_world_fail(world, "wall %u mapping cannot be read", (unsigned)wall_index);
            }
            hwr_parse_wall(wall);
            va = wall->vertex_a_offset;
            vb = wall->vertex_b_offset;
            if (va < first_vertex || vb < first_vertex ||
                ((uint16_t)(va - first_vertex) % 0x0cu) != 0 ||
                ((uint16_t)(vb - first_vertex) % 0x0cu) != 0 ||
                next.vertex_offset_to_index[va] == HWR_NO_INDEX ||
                next.vertex_offset_to_index[vb] == HWR_NO_INDEX) {
                hwr_world_clear(&next);
                return hwr_world_fail(world, "wall %u has an invalid vertex offset", (unsigned)wall_index);
            }
            wall->vertex_a_index = next.vertex_offset_to_index[va];
            wall->vertex_b_index = next.vertex_offset_to_index[vb];
            if (wall->portal_link_offset != UINT16_MAX) {
                if (!hwr_section_fits(wall->portal_link_offset, 8, sector_extent)) {
                    hwr_world_clear(&next);
                    return hwr_world_fail(world, "wall %u has an invalid portal link", (unsigned)wall_index);
                }
                neighbor = hwr_u16(next.sector_section +
                                   wall->portal_link_offset + 6u);
                wall->neighbor_sector_offset = neighbor;
                if (next.sector_offset_to_index[neighbor] == HWR_NO_INDEX) {
                    hwr_world_clear(&next);
                    return hwr_world_fail(world, "wall %u links to an unknown sector", (unsigned)wall_index);
                }
                wall->neighbor_sector_index = next.sector_offset_to_index[neighbor];
                next.portal_count++;
            } else {
                wall->neighbor_sector_offset = UINT16_MAX;
            }
        }
    }
    next.world_span_colorkey = api->game_ram->u16(VA_G_WORLD_SPAN_COLORKEY);
    next.sky_fat_index = api->game_ram->u16(VA_G_SKY_FAT_INDEX);
    hwr_world_collect_texture_references(&next);
    if (hwr_world_build_adjacency(&next) != 0) {
        hwr_world_clear(&next);
        return hwr_world_fail(world, "out of memory building wall adjacency index");
    }
    next.valid = 1;
    /* Development aid: HWR_WORLD_DUMP=<path> writes the parsed geometry as
     * text (sectors, then walls with endpoint coordinates) for offline
     * inspection. */
    {
        const char *dump = getenv("HWR_WORLD_DUMP");
        if (dump != NULL && dump[0] != 0) {
            FILE *f = fopen(dump, "w");
            if (f != NULL) {
                size_t n;
                fprintf(f, "colorkey %u\n", next.world_span_colorkey);
                for (n = 0; n < next.sector_count; n++) {
                    const struct hwr_sector *s = &next.sectors[n];
                    fprintf(f, "sector %zu off=%#x floor=%d ceil=%d flags=%#x light=%u map_override=%d walls=%zu+%u fid=%d cid=%d fpan=%#x cpan=%#x",
                            n, s->section_offset, s->floor_z, s->ceiling_z,
                            s->flags, s->light, (int)s->texture_map_override,
                            s->first_wall, s->wall_count,
                            s->floor_flat_id, s->ceiling_flat_id,
                            s->floor_pan, s->ceiling_pan);
                    if (s->slab_index != SIZE_MAX) {
                        const struct hwr_slab *sl = &next.slabs[s->slab_index];
                        fprintf(f, " slab=%zu lo=%d(%u) hi=%d(%u)",
                                s->slab_index, sl->lower_z, sl->lower_surface_id,
                                sl->upper_z, sl->upper_surface_id);
                    }
                    fputc('\n', f);
                }
                for (n = 0; n < next.wall_count; n++) {
                    const struct hwr_wall *w = &next.walls[n];
                    const struct hwr_vertex *a = &next.vertices[w->vertex_a_index];
                    const struct hwr_vertex *b = &next.vertices[w->vertex_b_index];
                    fprintf(f, "wall %zu sec=%zu nbr=%ld va=%zu(%d,%d) vb=%zu(%d,%d) mid=%u up=%u low=%u ext=%d mf=%#x scale=%#x\n",
                            n, w->sector_index,
                            w->neighbor_sector_offset == UINT16_MAX
                                ? -1L : (long)w->neighbor_sector_index,
                            w->vertex_a_index, a->world_x, a->world_y,
                            w->vertex_b_index, b->world_x, b->world_y,
                            w->middle_id, w->upper_id, w->lower_id,
                            w->extent, w->mapping_flags, w->scale);
                }
                fclose(f);
            }
        }
    }
    hwr_world_clear(world);
    *world = next;
    return 0;
}

/* Texture panning is not something the renderer computes. Each pannable
 * surface carries a pan byte pair in the map-geometry buffer, and the map's
 * effect system advances that pair in place on the game tick; the engine
 * simply re-reads it every time it emits the surface. Flowing water and
 * drifting platform textures therefore only move if the pan bytes are re-read
 * along with the other mutable geometry. HWR_SCROLL=0 leaves every surface at
 * the pan it carried when the map was loaded. */
static int hwr_scroll_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *setting = getenv("HWR_SCROLL");
        enabled = setting != NULL && setting[0] == '0' ? 0 : 1;
    }
    return enabled;
}

int hwr_world_refresh(struct hwr_world *world, const struct roth_api_v1 *api)
{
    size_t i;
    int scroll;
    int new_texture_seen = 0;
    if (world == NULL || !world->valid || api == NULL || api->game_ram == NULL)
        return -1;
    scroll = hwr_scroll_enabled();
    /* A map change replaces the sections; a stale snapshot must fail closed
     * instead of reading through a dangling pointer. */
    if (hwr_stored_pointer(api, VA_G_MAP_GEOMETRY_BUFFER) != world->sector_section ||
        hwr_stored_pointer(api, VA_G_VERTEX_GEOMETRY_BASE) != world->vertex_section)
        return hwr_world_fail(world, "geometry sections moved since the snapshot");
    for (i = 0; i < world->sector_count; i++) {
        struct hwr_sector *sector = &world->sectors[i];
        const uint8_t *record = world->sector_section + sector->section_offset;
        memcpy(sector->raw, record, 4);
        /* Texture-state commands update correlated fields as one tuple: ids,
         * scale/mapping flags and flip/tiling bits must settle together. */
        memcpy(sector->raw + 6, record + 6, 7);
        memcpy(sector->raw + 0x16, record + 0x16, 2);
        if (scroll) {
            /* Ceiling pan pair at +0x10, floor pan pair at +0x12. */
            memcpy(sector->raw + 0x10, record + 0x10, 4);
        }
        hwr_parse_sector(sector);
        new_texture_seen |= hwr_world_mark_texture(world, sector->ceiling_flat_id);
        new_texture_seen |= hwr_world_mark_texture(world, sector->floor_flat_id);
    }
    for (i = 0; i < world->slab_count; i++) {
        struct hwr_slab *slab = &world->slabs[i];
        const uint8_t *record = world->sector_section + slab->section_offset;
        memcpy(slab->raw, record, 4);
        memcpy(slab->raw + 6, record + 6, 4);
        slab->raw[0x0c] = record[0x0c];
        if (scroll) {
            /* Lower-pair pan at +0x04, upper-pair pan at +0x0a. */
            memcpy(slab->raw + 4, record + 4, 2);
            memcpy(slab->raw + 0x0a, record + 0x0a, 2);
        }
        hwr_parse_slab(slab);
        new_texture_seen |= hwr_world_mark_texture(
            world, (int16_t)slab->lower_surface_id);
        new_texture_seen |= hwr_world_mark_texture(
            world, (int16_t)slab->upper_surface_id);
    }
    for (i = 0; i < world->wall_count; i++) {
        struct hwr_wall *wall = &world->walls[i];
        const uint8_t *record = world->sector_section + wall->mapping_offset;
        memcpy(wall->mapping_raw, record, 0x0a);
        if (scroll) {
            /* A face's pan pair sits at +0x0a of its mapping record. Several
             * walls may share one mapping record, which simply re-reads the
             * same pair. */
            memcpy(wall->mapping_raw + 0x0a, record + 0x0a, 2);
        }
        hwr_parse_wall(wall);
        new_texture_seen |= hwr_world_mark_texture(
            world, (int16_t)wall->middle_id);
        new_texture_seen |= hwr_world_mark_texture(
            world, (int16_t)wall->upper_id);
        new_texture_seen |= hwr_world_mark_texture(
            world, (int16_t)wall->lower_id);
    }
    for (i = 0; i < world->vertex_count; i++) {
        struct hwr_vertex *vertex = &world->vertices[i];
        const uint8_t *record = world->vertex_section + vertex->section_offset;
        memcpy(vertex->raw + 8, record + 8, 4);
        vertex->world_x = hwr_i16(vertex->raw + 8);
        vertex->world_y = hwr_i16(vertex->raw + 0x0a);
    }
    {
        size_t count = api->game_ram->u8(VA_G_DOOR_COUNT);
        if (count > HWR_DOOR_MAX)
            count = HWR_DOOR_MAX;
        world->door_count = 0;
        for (i = 0; i < count; i++) {
            uint8_t record[HWR_DOOR_STRIDE];
            struct hwr_door *door = &world->doors[world->door_count];
            uint32_t record_va =
                VA_G_DOOR_POOL + (uint32_t)i * HWR_DOOR_STRIDE;
            uint16_t owner;
            uint16_t neighbor;
            size_t k;
            if (api->game_ram->read_block(
                    record_va, record, sizeof record) != 0)
                return hwr_world_fail(world,
                                      "primary door %u cannot be read",
                                      (unsigned)i);
            owner = hwr_u16(record);
            neighbor = hwr_u16(record + 0x10);
            if (owner == 0 ||
                world->sector_offset_to_index[owner] == HWR_NO_INDEX)
                continue;
            memset(door, 0, sizeof *door);
            door->record_ptr =
                (uint32_t)(uintptr_t)api->game_ram->to_ptr(record_va);
            door->owner_sector_offset = owner;
            door->neighbor_sector_offset = neighbor;
            door->owner_sector_index =
                world->sector_offset_to_index[owner];
            door->neighbor_sector_index = SIZE_MAX;
            if (neighbor != 0 &&
                world->sector_offset_to_index[neighbor] != HWR_NO_INDEX)
                door->neighbor_sector_index =
                    world->sector_offset_to_index[neighbor];
            door->state_flags = record[2];
            door->angle = (int8_t)record[3];
            for (k = 0; k < 4u; k++) {
                const uint8_t *corner =
                    record + HWR_DOOR_CORNER_BASE +
                    k * HWR_DOOR_CORNER_STRIDE;
                door->corner_x[k] = hwr_i16(corner);
                door->corner_y[k] = hwr_i16(corner + 4);
            }
            for (k = 0; k < HWR_DOOR_SURFACE_COUNT; k++) {
                const uint8_t *surface =
                    record + HWR_DOOR_SURFACE_BASE +
                    k * HWR_DOOR_SURFACE_STRIDE;
                door->surfaces[k].texture_id =
                    (int16_t)hwr_u16(surface + 0x0c);
                door->surfaces[k].extent =
                    hwr_u16(surface + 0x26) & 0x0fffu;
                door->surfaces[k].vertical_extent =
                    hwr_u16(surface + 0x24);
                door->surfaces[k].draw_flags =
                    hwr_u16(surface + 0x16);
                door->surfaces[k].shade_mode = surface[0x0e];
                door->surfaces[k].scale = surface[0x0f];
                new_texture_seen |= hwr_world_mark_texture(
                    world, door->surfaces[k].texture_id);
            }
            world->door_count++;
        }
    }
    return new_texture_seen ? HWR_WORLD_REFRESH_TEXTURES : 0;
}

static int hwr_copy_pointer_region(const struct roth_api_v1 *api,
                                   uint32_t pointer_global, void *destination,
                                   uint32_t size)
{
    const uint8_t *region = hwr_stored_pointer(api, pointer_global);
    if (region == NULL)
        return -1;
    memcpy(destination, region, size);
    return 0;
}

int hwr_world_snapshot_luts(struct hwr_world *world, const struct roth_api_v1 *api)
{
    if (world == NULL || api == NULL || api->game_ram == NULL)
        return -1;
    world->luts.valid = 0;
    if (hwr_copy_pointer_region(api, VA_G_PALETTE_RGB_PTR, world->luts.palette,
                                sizeof world->luts.palette) != 0 ||
        hwr_copy_pointer_region(api, VA_G_COLORMAP_PTR, world->luts.colormap,
                                sizeof world->luts.colormap) != 0 ||
        hwr_copy_pointer_region(api, VA_G_TINT_PTR, world->luts.tint,
                                sizeof world->luts.tint) != 0 ||
        hwr_copy_pointer_region(api, VA_G_GLOW_PTR, world->luts.glow,
                                sizeof world->luts.glow) != 0 ||
        hwr_copy_pointer_region(api, VA_G_BLEND_PTR, world->luts.blend,
                                sizeof world->luts.blend) != 0)
        return hwr_world_fail(world, "palette or color table pointer is outside game memory");
    if (hwr_world_refresh_lut_state(world, api) != 0)
        return -1;
    world->luts.valid = 1;
    return 0;
}

int hwr_world_refresh_lut_state(struct hwr_world *world,
                                const struct roth_api_v1 *api)
{
    if (world == NULL || api == NULL || api->game_ram == NULL)
        return -1;
    world->luts.damage_flash_level =
        (int32_t)api->game_ram->u32(VA_G_DAMAGE_FLASH_LEVEL);
    world->luts.shade_level = api->game_ram->u16(VA_G_SHADE_LEVEL);
    world->luts.tint_shade_level = api->game_ram->u16(VA_G_TINT_SHADE_LEVEL);
    world->luts.fill_index = api->game_ram->u16(VA_G_DAS_FILL_INDEX);
    /* The map's own candle word, re-read every frame for the same reason the
     * two shade levels beside it are: it is rewritten on every map load and a
     * value taken at one load is not this map's. A map that never wrote one
     * leaves nothing there, and a word larger than any candle word can be is
     * not one either; both come back as no word at all, and the light that
     * reads it falls back on its own settings. Saying so once is enough --
     * this runs every frame. */
    {
        static int reported;
        uint16_t glow = api->game_ram->u16(VA_G_CANDLE_GLOW);

        if (glow > HWR_CANDLE_GLOW_MAX) {
            if (!reported) {
                reported = 1;
                fprintf(stderr, "[renderer_overhaul] the map's candle word "
                                "reads %u, which is not one; the light you "
                                "carry keeps its own strength and reach\n",
                        (unsigned)glow);
            }
            glow = 0u;
        }
        world->luts.candle_glow = glow;
    }
    /* The palette buffer is populated late on some load paths (a quickload
     * can stage frames before it lands) and screen effects rewrite it, so it
     * is re-read every frame rather than trusted from the load snapshot. */
    hwr_copy_pointer_region(api, VA_G_PALETTE_RGB_PTR, world->luts.palette,
                            sizeof world->luts.palette);
    return 0;
}

/* The engine repoints the color-table globals late in its map-load sequence
 * (the tint table in particular may become an alias of the shading table), so
 * a snapshot taken at the load event can capture superseded tables. Re-reads
 * the tables and reports whether anything differed from the snapshot.
 * Returns 1 when the stored tables changed, 0 when identical, -1 on failure. */
int hwr_world_resnapshot_luts(struct hwr_world *world,
                              const struct roth_api_v1 *api)
{
    static struct hwr_luts fresh;
    if (world == NULL || !world->valid || !world->luts.valid ||
        api == NULL || api->game_ram == NULL)
        return -1;
    if (hwr_copy_pointer_region(api, VA_G_PALETTE_RGB_PTR, fresh.palette,
                                sizeof fresh.palette) != 0 ||
        hwr_copy_pointer_region(api, VA_G_COLORMAP_PTR, fresh.colormap,
                                sizeof fresh.colormap) != 0 ||
        hwr_copy_pointer_region(api, VA_G_TINT_PTR, fresh.tint,
                                sizeof fresh.tint) != 0 ||
        hwr_copy_pointer_region(api, VA_G_GLOW_PTR, fresh.glow,
                                sizeof fresh.glow) != 0 ||
        hwr_copy_pointer_region(api, VA_G_BLEND_PTR, fresh.blend,
                                sizeof fresh.blend) != 0)
        return -1;
    if (memcmp(fresh.palette, world->luts.palette, sizeof fresh.palette) == 0 &&
        memcmp(fresh.colormap, world->luts.colormap, sizeof fresh.colormap) == 0 &&
        memcmp(fresh.tint, world->luts.tint, sizeof fresh.tint) == 0 &&
        memcmp(fresh.glow, world->luts.glow, sizeof fresh.glow) == 0 &&
        memcmp(fresh.blend, world->luts.blend, sizeof fresh.blend) == 0)
        return 0;
    memcpy(world->luts.palette, fresh.palette, sizeof fresh.palette);
    memcpy(world->luts.colormap, fresh.colormap, sizeof fresh.colormap);
    memcpy(world->luts.tint, fresh.tint, sizeof fresh.tint);
    memcpy(world->luts.glow, fresh.glow, sizeof fresh.glow);
    memcpy(world->luts.blend, fresh.blend, sizeof fresh.blend);
    return 1;
}

static void hwr_mark_texture(uint8_t bits[0x2000], uint16_t id, size_t *count)
{
    uint8_t mask = (uint8_t)(1u << (id & 7u));
    uint8_t *byte = &bits[id >> 3];
    if ((*byte & mask) == 0) {
        *byte |= mask;
        (*count)++;
    }
}

void hwr_world_summary(const struct hwr_world *world, FILE *file)
{
    uint8_t wall_ids[0x2000];
    uint8_t flat_ids[0x2000];
    size_t distinct_wall = 0;
    size_t distinct_flat = 0;
    size_t negative_flats = 0;
    size_t negative_extents = 0;
    size_t sky_mappings = 0;
    size_t colorkey_parts = 0;
    int min_x = 0;
    int max_x = 0;
    int min_y = 0;
    int max_y = 0;
    size_t i;
    if (world == NULL || !world->valid) {
        fprintf(file, "  world: unavailable\n");
        return;
    }
    memset(wall_ids, 0, sizeof wall_ids);
    memset(flat_ids, 0, sizeof flat_ids);
    for (i = 0; i < world->sector_count; i++) {
        const struct hwr_sector *sector = &world->sectors[i];
        hwr_mark_texture(flat_ids, (uint16_t)sector->ceiling_flat_id, &distinct_flat);
        hwr_mark_texture(flat_ids, (uint16_t)sector->floor_flat_id, &distinct_flat);
        negative_flats += sector->ceiling_flat_id < 0;
        negative_flats += sector->floor_flat_id < 0;
    }
    for (i = 0; i < world->slab_count; i++) {
        hwr_mark_texture(flat_ids, world->slabs[i].lower_surface_id, &distinct_flat);
        hwr_mark_texture(flat_ids, world->slabs[i].upper_surface_id, &distinct_flat);
    }
    for (i = 0; i < world->wall_count; i++) {
        const struct hwr_wall *wall = &world->walls[i];
        uint16_t ids[3] = { wall->middle_id, wall->upper_id, wall->lower_id };
        size_t part;
        for (part = 0; part < 3; part++) {
            hwr_mark_texture(wall_ids, ids[part], &distinct_wall);
            colorkey_parts += ids[part] == world->world_span_colorkey;
        }
        negative_extents += wall->extent < 0;
        sky_mappings += (wall->mapping_flags & 0x40u) != 0;
    }
    if (world->vertex_count != 0) {
        min_x = max_x = world->vertices[0].world_x;
        min_y = max_y = world->vertices[0].world_y;
        for (i = 1; i < world->vertex_count; i++) {
            int x = world->vertices[i].world_x;
            int y = world->vertices[i].world_y;
            if (x < min_x) min_x = x;
            if (x > max_x) max_x = x;
            if (y < min_y) min_y = y;
            if (y > max_y) max_y = y;
        }
    }
    fprintf(file, "  world: sectors=%u walls=%u vertices=%u slabs=%u portals=%u\n",
            (unsigned)world->sector_count, (unsigned)world->wall_count,
            (unsigned)world->vertex_count, (unsigned)world->slab_count,
            (unsigned)world->portal_count);
    fprintf(file, "  textures: walls=%u flats=%u colorkey=0x%04x colorkey_parts=%u\n",
            (unsigned)distinct_wall, (unsigned)distinct_flat,
            world->world_span_colorkey, (unsigned)colorkey_parts);
    fprintf(file, "  bounds: x=%d..%d y=%d..%d\n", min_x, max_x, min_y, max_y);
    fprintf(file, "  oddities: negative_flats=%u negative_extents=%u sky_mappings=%u\n",
            (unsigned)negative_flats, (unsigned)negative_extents,
            (unsigned)sky_mappings);
}

const char *hwr_world_error(const struct hwr_world *world)
{
    return world != NULL && world->error[0] != 0 ? world->error : "unknown world error";
}

size_t hwr_world_sector_count(const struct hwr_world *world)
{
    return world != NULL && world->valid ? world->sector_count : 0;
}

size_t hwr_world_wall_count(const struct hwr_world *world)
{
    return world != NULL && world->valid ? world->wall_count : 0;
}

size_t hwr_world_vertex_count(const struct hwr_world *world)
{
    return world != NULL && world->valid ? world->vertex_count : 0;
}

size_t hwr_world_slab_count(const struct hwr_world *world)
{
    return world != NULL && world->valid ? world->slab_count : 0;
}

const struct hwr_sector *hwr_world_sector(const struct hwr_world *world, size_t index)
{
    return world != NULL && world->valid && index < world->sector_count
         ? &world->sectors[index] : NULL;
}

const struct hwr_wall *hwr_world_wall(const struct hwr_world *world, size_t index)
{
    return world != NULL && world->valid && index < world->wall_count
         ? &world->walls[index] : NULL;
}

const struct hwr_vertex *hwr_world_vertex(const struct hwr_world *world, size_t index)
{
    return world != NULL && world->valid && index < world->vertex_count
         ? &world->vertices[index] : NULL;
}

const struct hwr_slab *hwr_world_slab(const struct hwr_world *world, size_t index)
{
    return world != NULL && world->valid && index < world->slab_count
         ? &world->slabs[index] : NULL;
}
