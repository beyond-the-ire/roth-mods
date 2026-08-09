#include "hwr_scene.h"

#include "hwr_composite.h"
#include "hwr_perf.h"

#include "hwr_projection.h"
#include "hwr_settings.h"

#include "hwr_gl.h"
#include "hwr_lights.h"
#include "hwr_sprite_span.h"
#include "hwr_sun.h"

#include "hwr_util.h"
#include "hwr_visibility.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* THE TEXTURE STORE'S THREE BLOCKS.
 *
 * Every texel the world renderer samples lives in one linear GPU buffer, and
 * every directory entry that addresses it is a byte offset into that buffer.
 * What changes when the player walks through a door is only a small part of it:
 *
 *   S  the shared DAS's sprite set. One shared DAS serves the whole game (the
 *      engine names it once, from the configuration, and never swaps it), so
 *      these bytes are settled for the life of the process.
 *   M  the object meshes of both DAS files, the map DAS's object-graphic tail,
 *      and the ground shadow. Every map that names the same map DAS shares
 *      them -- which is most
 *      warps, because the engine itself skips its DAS swap when the basename is
 *      unchanged (map_load.c:1058-1080). The meshes sit here rather than in S
 *      because a mesh face may name a texture in EITHER DAS: ids below the
 *      shared bias resolve against the map DAS, so a shared mesh can carry
 *      map-DAS texels and cannot outlive a map-DAS change.
 *   V  this map's own world textures and its sky. The only genuinely per-map
 *      bytes, and the store's suffix, so they are written into the resident
 *      buffer rather than replacing it.
 *
 * The identity each block is kept on is the DAS file object it was packed from
 * (hwr_das_archive::open_serial), not a name: the serial changes if and only if
 * that DAS was re-opened, which is the only way its bytes can have become a
 * different file. */
static uint32_t g_store_shared_serial;
static uint32_t g_store_map_serial;
static int g_store_closure;
static size_t g_store_shared_size;   /* |S| */
static size_t g_store_prefix_size;   /* |S| + |M| */
static int g_store_resident;

/* Headroom the store buffer is created with, so a later map's V block is
 * written into it instead of forcing a new one. The largest V any retail map
 * produces is under 7 MB; a store that would overflow this falls back to a full
 * upload rather than growing in place. */
#define HWR_STORE_SLACK (16u * 1024u * 1024u)
/* Initial room for the V block in the CPU run being packed. */
#define HWR_STORE_RUN_HINT (8u * 1024u * 1024u)

void hwr_scene_store_forget(void)
{
    g_store_shared_serial = 0;
    g_store_map_serial = 0;
    g_store_closure = 0;
    g_store_shared_size = 0;
    g_store_prefix_size = 0;
    g_store_resident = 0;
}

/* Ordinary world-wall winner tag, shared with hwr_billboards.c and the geometry
 * shader. The low bits identify the owning sector; the high class bit keeps it
 * disjoint from the existing owner-floor height tags. */
#define HWR_WORLD_WALL_TAG 0x80000000u

#define HWR_WALL_U_REVERSED 0
#define HWR_GROUP_PART_TABLE_VA 0x909b4u
#define HWR_SKY_PROJECTION_TABLE_VA 0x8c484u
#define HWR_SKY_PROJECTION_TABLE_SIZE 0x288u

#define HWR_TEX_TRANSPARENT 0x01u
#define HWR_TEX_IMAGE_FIT 0x04u
#define HWR_TEX_TRANSPARENT_FIXED_SIZE 0x08u
#define HWR_TEX_HALF_PIXEL 0x20u
#define HWR_SECTOR_FLOOR_FLIP_X (1u << 8)
#define HWR_SECTOR_FLOOR_FLIP_Y (1u << 9)
#define HWR_SECTOR_CEILING_FLIP_X (1u << 10)
#define HWR_SECTOR_CEILING_FLIP_Y (1u << 11)

struct hwr_scene_texture_stats {
    size_t plain;
    size_t grouped;
    size_t animated;
    size_t fill;
    size_t fallbacks;
};

static uint16_t hwr_scene_read_u16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
}

static int hwr_mesh_fail(struct hwr_scene_mesh *mesh, const char *format, ...)
{
    va_list args;
    mesh->valid = 0;
    va_start(args, format);
    vsnprintf(mesh->error, sizeof mesh->error, format, args);
    va_end(args);
    return -1;
}

static int hwr_scene_fail(struct hwr_scene *scene, const char *format, ...)
{
    va_list args;
    scene->valid = 0;
    va_start(args, format);
    vsnprintf(scene->error, sizeof scene->error, format, args);
    va_end(args);
    return -1;
}

void hwr_scene_mesh_clear(struct hwr_scene_mesh *mesh)
{
    if (mesh == NULL)
        return;
    free(mesh->vertices);
    free(mesh->surfaces);
    free(mesh->loop_points);
    free(mesh->cut_scratch);
    memset(mesh, 0, sizeof *mesh);
}

/* Sizes the reusable wall-cut scratch to the current map. Each side (a/b) can
 * accrue at most 8 cuts per incident wall (floor/ceiling of the incident sector
 * plus its neighbour, each optionally carrying a slab), and every wall can be
 * incident to an endpoint, so wall_count*8 per side bounds either list -- the
 * same bound the former per-emit allocation used. */
static int hwr_reserve_cut_scratch(struct hwr_scene_mesh *mesh, size_t wall_count)
{
    int16_t *scratch;
    size_t needed;
    if (wall_count == 0)
        return 0;
    if (wall_count > SIZE_MAX / (16u * sizeof *scratch))
        return hwr_mesh_fail(mesh, "wall cut scratch overflow");
    needed = wall_count * 16u;
    if (needed <= mesh->cut_scratch_capacity)
        return 0;
    scratch = (int16_t *)realloc(mesh->cut_scratch, needed * sizeof *scratch);
    if (scratch == NULL)
        return hwr_mesh_fail(mesh, "out of memory reserving wall cut scratch");
    mesh->cut_scratch = scratch;
    mesh->cut_scratch_capacity = needed;
    return 0;
}

static int hwr_reserve_vertices(struct hwr_scene_mesh *mesh, size_t extra)
{
    struct hwr_mesh_vertex *vertices;
    size_t capacity;
    if (extra > SIZE_MAX - mesh->vertex_count)
        return hwr_mesh_fail(mesh, "mesh vertex count overflow");
    if (mesh->vertex_count + extra <= mesh->vertex_capacity)
        return 0;
    capacity = mesh->vertex_capacity != 0 ? mesh->vertex_capacity : 4096u;
    while (capacity < mesh->vertex_count + extra) {
        if (capacity > SIZE_MAX / 2u)
            return hwr_mesh_fail(mesh, "mesh vertex capacity overflow");
        capacity *= 2u;
    }
    vertices = (struct hwr_mesh_vertex *)realloc(
        mesh->vertices, capacity * sizeof *vertices);
    if (vertices == NULL)
        return hwr_mesh_fail(mesh, "out of memory growing mesh vertices");
    mesh->vertices = vertices;
    mesh->vertex_capacity = capacity;
    return 0;
}

/* Appends one flat's sector loop to the mesh-wide loop store and records the
 * run on the surface. The store grows exactly like the vertex store. */
static int hwr_store_surface_loop(struct hwr_scene_mesh *mesh,
                                  struct hwr_mesh_surface *surface,
                                  const struct hwr_point *points, size_t count)
{
    size_t capacity;
    if (count == 0)
        return 0;
    if (count > SIZE_MAX - mesh->loop_point_count)
        return hwr_mesh_fail(mesh, "mesh loop point count overflow");
    if (mesh->loop_point_count + count > mesh->loop_point_capacity) {
        struct hwr_point *grown;
        capacity = mesh->loop_point_capacity != 0
            ? mesh->loop_point_capacity : 2048u;
        while (capacity < mesh->loop_point_count + count) {
            if (capacity > SIZE_MAX / 2u)
                return hwr_mesh_fail(mesh, "mesh loop point capacity overflow");
            capacity *= 2u;
        }
        grown = (struct hwr_point *)realloc(mesh->loop_points,
                                            capacity * sizeof *grown);
        if (grown == NULL)
            return hwr_mesh_fail(mesh, "out of memory growing sector loops");
        mesh->loop_points = grown;
        mesh->loop_point_capacity = capacity;
    }
    memcpy(mesh->loop_points + mesh->loop_point_count, points,
           count * sizeof *points);
    surface->first_loop_point = mesh->loop_point_count;
    surface->loop_point_count = count;
    mesh->loop_point_count += count;
    return 0;
}

static struct hwr_mesh_surface *hwr_add_surface(struct hwr_scene_mesh *mesh,
                                                enum hwr_surface_kind kind,
                                                size_t vertex_count)
{
    struct hwr_mesh_surface *surfaces;
    struct hwr_mesh_surface *surface;
    size_t capacity;
    if (mesh->surface_count == mesh->surface_capacity) {
        capacity = mesh->surface_capacity != 0 ? mesh->surface_capacity * 2u : 1024u;
        surfaces = (struct hwr_mesh_surface *)realloc(
            mesh->surfaces, capacity * sizeof *surfaces);
        if (surfaces == NULL) {
            hwr_mesh_fail(mesh, "out of memory growing mesh surfaces");
            return NULL;
        }
        mesh->surfaces = surfaces;
        mesh->surface_capacity = capacity;
    }
    if (hwr_reserve_vertices(mesh, vertex_count) != 0)
        return NULL;
    surface = &mesh->surfaces[mesh->surface_count++];
    memset(surface, 0, sizeof *surface);
    surface->first_vertex = mesh->vertex_count;
    surface->vertex_count = vertex_count;
    surface->neighbor_sector_index = SIZE_MAX;
    surface->wall_offset = UINT16_MAX;
    surface->door_index = UINT8_MAX;
    surface->kind = kind;
    mesh->kind_counts[kind]++;
    return surface;
}

static void hwr_mark_texture(struct hwr_scene_mesh *mesh, int16_t id)
{
    uint16_t logical;
    uint8_t mask;
    if (id < 0)
        return;
    logical = (uint16_t)id;
    if (logical >= 0x1600u)
        return;
    mask = (uint8_t)(1u << (logical & 7u));
    if ((mesh->texture_refs[logical >> 3] & mask) == 0) {
        mesh->texture_refs[logical >> 3] |= mask;
        mesh->texture_ref_count++;
    }
}

static void hwr_collect_texture_references(struct hwr_scene_mesh *mesh,
                                           const struct hwr_world *world)
{
    size_t i;
    for (i = 0; i < 0x1600u; i++) {
        if ((world->texture_refs[i >> 3] &
             (uint8_t)(1u << (i & 7u))) != 0)
            hwr_mark_texture(mesh, (int16_t)i);
    }
    for (i = 0; i < world->sector_count; i++) {
        const struct hwr_sector *sector = &world->sectors[i];
        int16_t ids[2] = { sector->ceiling_flat_id, sector->floor_flat_id };
        size_t k;
        for (k = 0; k < 2; k++) {
            if (ids[k] < 0 || (uint16_t)ids[k] != world->world_span_colorkey)
                hwr_mark_texture(mesh, ids[k]);
        }
    }
    for (i = 0; i < world->slab_count; i++) {
        int16_t ids[2] = { (int16_t)world->slabs[i].lower_surface_id,
                           (int16_t)world->slabs[i].upper_surface_id };
        size_t k;
        for (k = 0; k < 2; k++) {
            if (ids[k] < 0 || (uint16_t)ids[k] != world->world_span_colorkey)
                hwr_mark_texture(mesh, ids[k]);
        }
    }
    for (i = 0; i < world->wall_count; i++) {
        int16_t ids[3] = { (int16_t)world->walls[i].middle_id,
                           (int16_t)world->walls[i].upper_id,
                           (int16_t)world->walls[i].lower_id };
        size_t k;
        for (k = 0; k < 3; k++) {
            if (ids[k] < 0 || (uint16_t)ids[k] != world->world_span_colorkey)
                hwr_mark_texture(mesh, ids[k]);
        }
    }
}

int hwr_scene_mesh_references(const struct hwr_scene_mesh *mesh, uint16_t id)
{
    return mesh != NULL && id < 0x1600u &&
           (mesh->texture_refs[id >> 3] & (uint8_t)(1u << (id & 7u))) != 0;
}

static void hwr_finish_material(struct hwr_scene_mesh *mesh,
                                struct hwr_mesh_surface *surface,
                                const struct hwr_sector *sector, int16_t id,
                                uint8_t mapping_flags, int masked)
{
    surface->texture_id = id;
    surface->sector_flags = sector->flags;
    surface->light = sector->light;
    surface->mapping_flags = mapping_flags;
    if (masked)
        surface->flags |= HWR_SURFACE_MASKED;
    if (id < 0)
        surface->flags |= HWR_SURFACE_DIRECT;
    else
        hwr_mark_texture(mesh, id);
    if ((mapping_flags & 0x40u) != 0)
        surface->flags |= HWR_SURFACE_SKY;
}

static void hwr_set_vertex(struct hwr_mesh_vertex *vertex, int16_t x, int16_t y,
                           int16_t z, float wall_s, float wall_top)
{
    vertex->x = x;
    vertex->y = y;
    vertex->z = z;
    vertex->reserved = 0;
    vertex->wall_s = wall_s;
    vertex->wall_top = wall_top;
}

static int hwr_compare_heights(const void *left, const void *right)
{
    int16_t a = *(const int16_t *)left;
    int16_t b = *(const int16_t *)right;
    return a < b ? -1 : a > b;
}

static void hwr_add_wall_cut(int16_t *cuts, size_t *count, int16_t z,
                             int16_t bottom, int16_t top)
{
    size_t i;
    if (z <= bottom || z >= top)
        return;
    for (i = 0; i < *count; i++) {
        if (cuts[i] == z)
            return;
    }
    cuts[(*count)++] = z;
}

static void hwr_add_sector_cuts(const struct hwr_world *world,
                                const struct hwr_sector *sector,
                                int16_t *cuts, size_t *count,
                                int16_t bottom, int16_t top)
{
    hwr_add_wall_cut(cuts, count, sector->floor_z, bottom, top);
    hwr_add_wall_cut(cuts, count, sector->ceiling_z, bottom, top);
    if (sector->slab_index < world->slab_count) {
        const struct hwr_slab *slab = &world->slabs[sector->slab_index];
        hwr_add_wall_cut(cuts, count, slab->lower_z, bottom, top);
        hwr_add_wall_cut(cuts, count, slab->upper_z, bottom, top);
    }
}

/* Collects the wall-edge cuts contributed by every wall incident to one
 * endpoint vertex, using the world's static adjacency index. This replaces the
 * per-emit linear scan over all walls: the set of incident walls is fixed map
 * topology, so it is precomputed once at snapshot time. Cuts are de-duplicated
 * and later sorted, so the visitation order does not affect the result. */
static void hwr_collect_endpoint_cuts(const struct hwr_world *world, size_t vertex,
                                      int16_t *cuts, size_t *count,
                                      int16_t bottom, int16_t top)
{
    size_t start = world->vertex_wall_offsets[vertex];
    size_t end = world->vertex_wall_offsets[vertex + 1u];
    size_t k;
    for (k = start; k < end; k++) {
        const struct hwr_wall *incident =
            &world->walls[world->vertex_wall_indices[k]];
        const struct hwr_sector *incident_sector;
        if (incident->sector_index >= world->sector_count)
            continue;
        incident_sector = &world->sectors[incident->sector_index];
        hwr_add_sector_cuts(world, incident_sector, cuts, count, bottom, top);
        if (incident->neighbor_sector_index != SIZE_MAX)
            hwr_add_sector_cuts(world,
                                &world->sectors[incident->neighbor_sector_index],
                                cuts, count, bottom, top);
    }
}

static uint16_t hwr_wall_length(int16_t ax, int16_t ay,
                                int16_t bx, int16_t by)
{
    int32_t dx = (int32_t)bx - ax;
    int32_t dy = (int32_t)by - ay;
    uint64_t squared = (uint64_t)((int64_t)dx * dx) +
                       (uint64_t)((int64_t)dy * dy);
    uint32_t low = 1u;
    uint32_t high = UINT16_MAX;

    if (squared >= (uint64_t)UINT16_MAX * UINT16_MAX)
        return UINT16_MAX;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if ((uint64_t)middle * middle < squared)
            low = middle + 1u;
        else
            high = middle;
    }
    return (uint16_t)low;
}

/* Emits the sky "cap" for a wall whose top edge borders the sky: a vertical
 * quad standing on the wall's top edge (endpoints a/b at top_a/top_b) and
 * rising to just under the top of the coordinate space, so it projects above
 * the top of the view from any camera.  The cap shares the wall's world x/y, so
 * its projection covers exactly the screen column band above the wall's top
 * edge; drawn in the depth-off sky background pass, the SKY-material fragment
 * branch fills that band with the screen-space skybox, and later opaque
 * geometry paints over it.  Nothing else about the wall changes. */
static int hwr_emit_sky_cap(struct hwr_scene_mesh *mesh,
                            const struct hwr_vertex *a,
                            const struct hwr_vertex *b,
                            int16_t top_a, int16_t top_b, int occluder,
                            int full_height, size_t sector_index)
{
    struct hwr_mesh_surface *surface;
    struct hwr_mesh_vertex *v;
    int raised_a = (int)top_a + 30000;
    int raised_b = (int)top_b + 30000;
    int16_t cap_za = (int16_t)(raised_a > INT16_MAX ? INT16_MAX : raised_a);
    int16_t cap_zb = (int16_t)(raised_b > INT16_MAX ? INT16_MAX : raised_b);
    if (!full_height && cap_za <= top_a && cap_zb <= top_b)
        return 0;   /* the wall top already sits at the coordinate ceiling */
    surface = hwr_add_surface(mesh, HWR_SURFACE_SOLID_MIDDLE, 6u);
    if (surface == NULL)
        return -1;
    surface->texture_id = -1;
    /* A cap belongs to the sector whose wall bears the sky flag, so the
     * visibility gate drops it with that sector -- the engine only fills sky
     * from the faces it actually visits. */
    surface->sector_index = sector_index;
    surface->flags = (uint8_t)(HWR_SURFACE_SKY_CAP |
                               (occluder ? HWR_SURFACE_SKY_OCCLUDER : 0u));
    surface->aux = full_height ? 1u : 0u;
    v = mesh->vertices + mesh->vertex_count;
    hwr_set_vertex(&v[0], a->world_x, a->world_y,
                   full_height ? INT16_MIN : top_a, 0.0f, 0.0f);
    hwr_set_vertex(&v[1], b->world_x, b->world_y,
                   full_height ? INT16_MIN : top_b, 0.0f, 0.0f);
    hwr_set_vertex(&v[2], b->world_x, b->world_y,
                   full_height ? INT16_MAX : cap_zb, 0.0f, 0.0f);
    hwr_set_vertex(&v[3], a->world_x, a->world_y,
                   full_height ? INT16_MIN : top_a, 0.0f, 0.0f);
    hwr_set_vertex(&v[4], b->world_x, b->world_y,
                   full_height ? INT16_MAX : cap_zb, 0.0f, 0.0f);
    hwr_set_vertex(&v[5], a->world_x, a->world_y,
                   full_height ? INT16_MAX : cap_za, 0.0f, 0.0f);
    mesh->vertex_count += 6u;
    return 0;
}

static int hwr_emit_wall(struct hwr_scene_mesh *mesh, const struct hwr_world *world,
                         const struct hwr_wall *wall, const struct hwr_sector *sector,
                         enum hwr_surface_kind kind, int16_t id,
                         int16_t top_a, int16_t top_b, int16_t bottom_a,
                         int16_t bottom_b, int masked, uint8_t mapping_flags)
{
    const struct hwr_vertex *a;
    const struct hwr_vertex *b;
    struct hwr_mesh_surface *surface;
    struct hwr_mesh_vertex *v;
    int16_t *cuts_a = NULL;
    int16_t *cuts_b = NULL;
    size_t cut_count_a = 0;
    size_t cut_count_b = 0;
    size_t triangle_count;
    uint16_t extent;
    float sa;
    float sb;
    if (id >= 0 && (uint16_t)id == world->world_span_colorkey)
        return 0;
    if (top_a <= bottom_a && top_b <= bottom_b)
        return 0;
    a = &world->vertices[wall->vertex_a_index];
    b = &world->vertices[wall->vertex_b_index];
    extent = (uint16_t)wall->extent & 0x7fffu;
    if (extent == 0)
        extent = hwr_wall_length(a->world_x, a->world_y,
                                 b->world_x, b->world_y);
    sa = HWR_WALL_U_REVERSED ? (float)extent : 0.0f;
    sb = HWR_WALL_U_REVERSED ? 0.0f : (float)extent;
    if (world->wall_count != 0) {
        /* The two endpoint lists are backed by the halves of the mesh's reusable
         * cut scratch (reserved once per build).  The walls incident to each
         * endpoint come from the world's precomputed adjacency index, so no
         * per-emit allocation or full-wall scan is needed. */
        size_t half = world->wall_count * 8u;
        cuts_a = mesh->cut_scratch;
        cuts_b = mesh->cut_scratch + half;
        hwr_collect_endpoint_cuts(world, wall->vertex_a_index, cuts_a,
                                  &cut_count_a, bottom_a, top_a);
        hwr_collect_endpoint_cuts(world, wall->vertex_b_index, cuts_b,
                                  &cut_count_b, bottom_b, top_b);
        qsort(cuts_a, cut_count_a, sizeof *cuts_a, hwr_compare_heights);
        qsort(cuts_b, cut_count_b, sizeof *cuts_b, hwr_compare_heights);
    }
    if (cut_count_a > SIZE_MAX - cut_count_b - 2u)
        return hwr_mesh_fail(mesh, "wall triangle count overflow");
    triangle_count = cut_count_a + cut_count_b + 2u;
    if (triangle_count > SIZE_MAX / 3u)
        return hwr_mesh_fail(mesh, "wall vertex count overflow");
    surface = hwr_add_surface(mesh, kind, triangle_count * 3u);
    if (surface == NULL)
        return -1;
    surface->sector_index = wall->sector_index;
    surface->wall_offset = wall->section_offset;
    surface->extent = extent;
    surface->scale = wall->scale;
    surface->pan_u = wall->extent < 0 ? wall->pan_u : 0;
    surface->pan_v = wall->extent < 0 ? wall->pan_v : 0;
    hwr_finish_material(mesh, surface, sector, id, mapping_flags, masked);
    v = mesh->vertices + mesh->vertex_count;
    {
        size_t ia = 0;
        size_t ib = 0;
        size_t triangle = 0;
        int16_t current_a = bottom_a;
        int16_t current_b = bottom_b;
        while (ia <= cut_count_a || ib <= cut_count_b) {
            int32_t next_a = ia < cut_count_a ? cuts_a[ia]
                           : ia == cut_count_a ? top_a : INT32_MAX;
            int32_t next_b = ib < cut_count_b ? cuts_b[ib]
                           : ib == cut_count_b ? top_b : INT32_MAX;
            struct hwr_mesh_vertex *q = v + triangle * 3u;
            if (next_a <= next_b) {
                hwr_set_vertex(&q[0], a->world_x, a->world_y,
                               current_a, sa, (float)top_a);
                hwr_set_vertex(&q[1], a->world_x, a->world_y,
                               (int16_t)next_a, sa, (float)top_a);
                hwr_set_vertex(&q[2], b->world_x, b->world_y,
                               current_b, sb, (float)top_b);
                q[0].reserved = current_a == bottom_a
                              ? HWR_WALL_CORNER_BOTTOM : HWR_WALL_CORNER_NONE;
                q[1].reserved = HWR_WALL_CORNER_NONE;
                q[2].reserved = HWR_WALL_CORNER_END_B |
                    (current_b == bottom_b ? HWR_WALL_CORNER_BOTTOM : 0);
                current_a = (int16_t)next_a;
                ia++;
            } else {
                hwr_set_vertex(&q[0], a->world_x, a->world_y,
                               current_a, sa, (float)top_a);
                hwr_set_vertex(&q[1], b->world_x, b->world_y,
                               (int16_t)next_b, sb, (float)top_b);
                hwr_set_vertex(&q[2], b->world_x, b->world_y,
                               current_b, sb, (float)top_b);
                q[0].reserved = current_a == bottom_a
                              ? HWR_WALL_CORNER_BOTTOM : HWR_WALL_CORNER_NONE;
                q[1].reserved = HWR_WALL_CORNER_END_B;
                q[2].reserved = HWR_WALL_CORNER_END_B |
                    (current_b == bottom_b ? HWR_WALL_CORNER_BOTTOM : 0);
                current_b = (int16_t)next_b;
                ib++;
            }
            triangle++;
        }
    }
    mesh->vertex_count += triangle_count * 3u;
    /* Sky walls (EDGE_MAP, bit 0x40) get a screen-space skybox cap standing on
     * their top edge.  Every EDGE_MAP face is capped; the caps are drawn in a
     * depth-off background pass before opaque geometry, which then overwrites
     * the sky wherever a nearer surface projects, so the engine's rule "write
     * sky everywhere a sky-wall projects it, then let geometry paint over it"
     * holds without any single face over-covering the view. */
    if ((mapping_flags & 0x40u) != 0) {
        /* A cap may OCCLUDE only when nothing of this wall is drawn above it,
         * i.e. it stands on the wall's true sky boundary.  Parts reaching our
         * ceiling always qualify.  A lower step qualifies only when no upper is
         * emitted (neighbour ceiling at or above ours), because then the
         * opening runs to the ceiling and the step's cap is the sky boundary;
         * if a facade sits above the step instead, occluding with that cap
         * would hide both the facade and whatever is seen through the opening. */
        int cap_occluder = (top_a == sector->ceiling_z &&
                            top_b == sector->ceiling_z);
        if (!cap_occluder && kind == HWR_SURFACE_PORTAL_LOWER &&
            wall->neighbor_sector_index != SIZE_MAX) {
            const struct hwr_sector *nb =
                &world->sectors[wall->neighbor_sector_index];
            cap_occluder = nb->ceiling_z >= sector->ceiling_z;
        }
        if (hwr_emit_sky_cap(mesh, a, b, top_a, top_b, cap_occluder, 0,
                             wall->sector_index) != 0)
            return -1;
    }
    return 0;
}

static int hwr_emit_doors(struct hwr_scene_mesh *mesh,
                          const struct hwr_world *world)
{
    static const uint8_t endpoint_a[HWR_DOOR_SURFACE_COUNT] =
        { 3u, 1u, 0u, 2u };
    static const uint8_t endpoint_b[HWR_DOOR_SURFACE_COUNT] =
        { 2u, 0u, 3u, 1u };
    size_t di;
    for (di = 0; di < world->door_count; di++) {
        const struct hwr_door *door = &world->doors[di];
        const struct hwr_sector *sector;
        const struct hwr_sector *shade_sector;
        size_t fi;
        int64_t leaf_area = 0;
        uint8_t leaf_reversed;
        if (door->owner_sector_index >= world->sector_count)
            continue;
        /* The signed area of the cycle the four faces traverse, taken from the
         * corner the leaf starts at so a leaf authored across the coordinate
         * wrap is still a small quad. A negative area is the handedness a
         * sector loop has, where the perpendicular of a directed edge points
         * INTO the loop -- and for a box, into the leaf. */
        for (fi = 0; fi < HWR_DOOR_SURFACE_COUNT; fi++) {
            uint8_t ea = endpoint_a[fi];
            uint8_t eb = endpoint_b[fi];
            int64_t ax = (int16_t)((uint16_t)door->corner_x[ea] -
                                   (uint16_t)door->corner_x[0]);
            int64_t ay = (int16_t)((uint16_t)door->corner_y[ea] -
                                   (uint16_t)door->corner_y[0]);
            int64_t bx = (int16_t)((uint16_t)door->corner_x[eb] -
                                   (uint16_t)door->corner_x[0]);
            int64_t by = (int16_t)((uint16_t)door->corner_y[eb] -
                                   (uint16_t)door->corner_y[0]);
            leaf_area += ax * by - bx * ay;
        }
        leaf_reversed = leaf_area < 0 ? 1u : 0u;
        sector = &world->sectors[door->owner_sector_index];
        /* The neighbor worklist always admits an active primary door; the
         * owner is only an additional admission while state bit 1 is clear.
         * Use that mandatory pass's sector shade state, falling back to owner
         * only for a malformed/missing neighbor. */
        shade_sector = door->neighbor_sector_index < world->sector_count
                     ? &world->sectors[door->neighbor_sector_index] : sector;
        for (fi = 0; fi < HWR_DOOR_SURFACE_COUNT; fi++) {
            const struct hwr_door_surface *face = &door->surfaces[fi];
            struct hwr_mesh_surface *surface;
            struct hwr_mesh_vertex *v;
            uint8_t a = endpoint_a[fi];
            uint8_t b = endpoint_b[fi];
            uint16_t extent = face->extent;
            int32_t leaf_height = (int16_t)face->vertical_extent;
            int16_t top_z;
            float row[4];
            float column[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
            float row_scale = 1.0f;
            float column_scale = 1.0f;
            if (face->texture_id >= 0 &&
                (uint16_t)face->texture_id == world->world_span_colorkey)
                continue;
            /* A door leaf is not the opening it fills. The engine authors one
             * height word per door at spawn -- the owner sector's ceiling minus
             * floor, or -(texture_map_override * 4) when the reference corner
             * carries TRANSPARENT together with TRANSPARENT_FIXED_SIZE
             * (setup_door_swing_geometry, doors.c:534-545) -- and stamps it
             * into all four of the leaf's quad points (doors.c:566-575). The
             * leaf then draws through the object-mesh pipeline, whose vertex
             * height is that point word plus the base height the caller passes
             * (project_sprite_to_render_queue, render_world.c:645-652); for a
             * door that base is the owner sector's FLOOR taken relative to the
             * eye (renderer.c:6440-6446). The mirrored lower points keep the
             * pool-init zero (doors.c:140-176), so the leaf stands ON the floor
             * and reaches floor + height -- which is exactly the ceiling for
             * every door that took the default. The same word is also the
             * leaf's vertical mapping extent (doors.c:278 read back at
             * renderer.c:13117), so a shortened leaf keeps a texture that spans
             * it end to end and the fit below needs no rescale. A leaf with no
             * positive height has nothing to draw. */
            if (leaf_height <= 0)
                continue;
            top_z = (int16_t)((uint16_t)sector->floor_z +
                              (uint16_t)leaf_height);
            if (extent == 0)
                extent = hwr_wall_length(
                    door->corner_x[a], door->corner_y[a],
                    door->corner_x[b], door->corner_y[b]);
            /* The direct DAS sampler's base row axis matches the accepted
             * object-mesh path. Face bit 1 travels through mapping_flags below
             * so it XORs with a grouped child's flip instead of being baked
             * into these coordinates twice. */
            row[0] = 0.0f; row[1] = 1.0f;
            row[2] = 1.0f; row[3] = 0.0f;
            if ((face->draw_flags & 0x20u) != 0) {
                row_scale += (float)(face->scale & 0x0fu);
                column_scale += (float)(face->scale >> 4);
                row[0] *= row_scale; row[1] *= row_scale;
                row[2] *= row_scale; row[3] *= row_scale;
                column[0] *= column_scale; column[1] *= column_scale;
                column[2] *= column_scale; column[3] *= column_scale;
            }
            surface = hwr_add_surface(mesh, HWR_SURFACE_DOOR, 6u);
            if (surface == NULL)
                return -1;
            surface->sector_index = door->owner_sector_index;
            surface->neighbor_sector_index = door->neighbor_sector_index;
            surface->door_index = (uint8_t)di;
            surface->door_state_flags = door->state_flags;
            surface->normal_reversed = leaf_reversed;
            surface->texture_id = face->texture_id;
            surface->extent = extent;
            surface->aux = face->vertical_extent;
            surface->scale = face->scale;
            surface->mapping_flags = (uint8_t)(face->draw_flags & 2u);
            surface->sector_flags = shade_sector->flags;
            /* +0x0e is a shade seed (normally 0x80), not a Boolean. Stock
             * combines it with the admitting sector ambient; when that
             * ambient is zero the 0x80 seed deliberately survives. */
            surface->light = face->shade_mode;
            if (face->shade_mode != 0 && shade_sector->light != 0) {
                surface->light = (uint8_t)(
                    (uint8_t)(face->shade_mode - 0x80u) +
                    shade_sector->light);
            }
            if (face->texture_id < 0)
                surface->flags |= HWR_SURFACE_DIRECT;
            else
                hwr_mark_texture(mesh, face->texture_id);
            if ((face->draw_flags & 1u) != 0)
                surface->flags |= HWR_SURFACE_MASKED;
            v = mesh->vertices + mesh->vertex_count;
            hwr_set_vertex(&v[0], door->corner_x[a], door->corner_y[a],
                           top_z, row[0], column[0]);
            hwr_set_vertex(&v[1], door->corner_x[b], door->corner_y[b],
                           top_z, row[1], column[1]);
            hwr_set_vertex(&v[2], door->corner_x[b], door->corner_y[b],
                           sector->floor_z, row[2], column[2]);
            hwr_set_vertex(&v[3], door->corner_x[a], door->corner_y[a],
                           top_z, row[0], column[0]);
            hwr_set_vertex(&v[4], door->corner_x[b], door->corner_y[b],
                           sector->floor_z, row[2], column[2]);
            hwr_set_vertex(&v[5], door->corner_x[a], door->corner_y[a],
                           sector->floor_z, row[3], column[3]);
            v[1].reserved = HWR_WALL_CORNER_END_B;
            v[2].reserved =
                HWR_WALL_CORNER_END_B | HWR_WALL_CORNER_BOTTOM;
            v[4].reserved =
                HWR_WALL_CORNER_END_B | HWR_WALL_CORNER_BOTTOM;
            v[5].reserved = HWR_WALL_CORNER_BOTTOM;
            mesh->vertex_count += 6u;
        }
    }
    return 0;
}

static int64_t hwr_cross(struct hwr_point a, struct hwr_point b,
                         struct hwr_point c)
{
    return (int64_t)(b.x - a.x) * (c.y - a.y) -
           (int64_t)(b.y - a.y) * (c.x - a.x);
}

static int hwr_between(int64_t a, int64_t b, int64_t value)
{
    return value >= (a < b ? a : b) && value <= (a > b ? a : b);
}

static int hwr_segments_cross(struct hwr_point a, struct hwr_point b,
                              struct hwr_point c, struct hwr_point d)
{
    int64_t ab_c = hwr_cross(a, b, c);
    int64_t ab_d = hwr_cross(a, b, d);
    int64_t cd_a = hwr_cross(c, d, a);
    int64_t cd_b = hwr_cross(c, d, b);
    if (((ab_c > 0 && ab_d < 0) || (ab_c < 0 && ab_d > 0)) &&
        ((cd_a > 0 && cd_b < 0) || (cd_a < 0 && cd_b > 0)))
        return 1;
    if (ab_c == 0 && hwr_between(a.x, b.x, c.x) && hwr_between(a.y, b.y, c.y))
        return 1;
    if (ab_d == 0 && hwr_between(a.x, b.x, d.x) && hwr_between(a.y, b.y, d.y))
        return 1;
    if (cd_a == 0 && hwr_between(c.x, d.x, a.x) && hwr_between(c.y, d.y, a.y))
        return 1;
    return cd_b == 0 && hwr_between(c.x, d.x, b.x) && hwr_between(c.y, d.y, b.y);
}

static int hwr_polygon_valid(const struct hwr_point *points, size_t count)
{
    size_t i;
    size_t j;
    for (i = 0; i < count; i++) {
        size_t in = (i + 1u) % count;
        if (points[i].x == points[in].x && points[i].y == points[in].y)
            return 0;
        for (j = i + 1u; j < count; j++) {
            size_t jn = (j + 1u) % count;
            if (i == j || in == j || jn == i)
                continue;
            if (hwr_segments_cross(points[i], points[in], points[j], points[jn]))
                return 0;
        }
    }
    return 1;
}

static int hwr_point_in_triangle(struct hwr_point p, struct hwr_point a,
                                 struct hwr_point b, struct hwr_point c)
{
    int64_t c0 = hwr_cross(a, b, p);
    int64_t c1 = hwr_cross(b, c, p);
    int64_t c2 = hwr_cross(c, a, p);
    return c0 >= 0 && c1 >= 0 && c2 >= 0;
}

static int hwr_triangulate(const struct hwr_point *points, size_t count,
                           uint16_t *triangles)
{
    uint16_t *indices;
    int64_t area = 0;
    size_t remaining = count;
    size_t output = 0;
    size_t i;
    size_t guard = 0;
    if (count < 3 || count > UINT16_MAX || !hwr_polygon_valid(points, count))
        return -1;
    for (i = 0; i < count; i++) {
        size_t next = (i + 1u) % count;
        area += (int64_t)points[i].x * points[next].y -
                (int64_t)points[next].x * points[i].y;
    }
    if (area == 0)
        return -1;
    indices = (uint16_t *)malloc(count * sizeof *indices);
    if (indices == NULL)
        return -1;
    for (i = 0; i < count; i++)
        indices[i] = (uint16_t)(area > 0 ? i : count - 1u - i);
    while (remaining > 3 && guard++ < count * count) {
        int clipped = 0;
        for (i = 0; i < remaining; i++) {
            size_t prev = (i + remaining - 1u) % remaining;
            size_t next = (i + 1u) % remaining;
            uint16_t ia = indices[prev];
            uint16_t ib = indices[i];
            uint16_t ic = indices[next];
            size_t k;
            int contains = 0;
            if (hwr_cross(points[ia], points[ib], points[ic]) <= 0)
                continue;
            for (k = 0; k < remaining; k++) {
                uint16_t ip = indices[k];
                if (k == prev || k == i || k == next)
                    continue;
                if (hwr_point_in_triangle(points[ip], points[ia], points[ib], points[ic])) {
                    contains = 1;
                    break;
                }
            }
            if (contains)
                continue;
            triangles[output++] = ia;
            triangles[output++] = ib;
            triangles[output++] = ic;
            memmove(indices + i, indices + i + 1u,
                    (remaining - i - 1u) * sizeof *indices);
            remaining--;
            clipped = 1;
            break;
        }
        if (!clipped) {
            free(indices);
            return -1;
        }
    }
    if (remaining == 3) {
        triangles[output++] = indices[0];
        triangles[output++] = indices[1];
        triangles[output++] = indices[2];
    }
    free(indices);
    return output == (count - 2u) * 3u ? 0 : -1;
}

/* A sector-scoped owner identity, disjoint from the height-derived one the
 * shader publishes for every other flat: the bit pair that marks the flat
 * family is kept, one more bit separates a floor from a ceiling, and the rest
 * is the sector. Scoping it by sector is what stops a mid-platform's redraw
 * from spilling onto a NEIGHBOUR's flat that happens to sit at the same height
 * -- the overhang the engine covers up by painting the nearer sector after the
 * farther one, which a depth-off redraw cannot reproduce. */
#define HWR_FLAT_OWNER_TAG       0x60000000u
#define HWR_FLAT_OWNER_CEILING   0x10000000u
#define HWR_FLAT_OWNER_INDEX_MAX 0x0fffffffu

/* HWR_SLAB_DEPTH_EXCEPTION=0 restores the plain depth-tested treatment of
 * mid-platform surfaces: one that sits at or through the flat of its own sector
 * loses to that flat again. Follows the owner-surface exception switch, since
 * this reuses that pass and its identity buffer. */
/* HWR_FLAT_VERTEX_LAW=0 restores the previous tinted-flat treatment: the whole
 * unclipped sector loop is emitted, a triangle with a corner outside the drawn
 * area is marked so the shader evaluates the candle radial per fragment, no flat
 * takes the whole-polygon solid fill, the bright end of the corner word floors
 * at row 0, and the fill index stays unset. It is the A/B for the corner-set
 * law and touches nothing else. */
static int hwr_flat_vertex_law_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *setting = getenv("HWR_FLAT_VERTEX_LAW");
        enabled = setting != NULL && setting[0] == '0' ? 0 : 1;
    }
    return enabled;
}

static int hwr_slab_exception_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *setting = getenv("HWR_SLAB_DEPTH_EXCEPTION");
        enabled = setting != NULL && setting[0] == '0' ? 0 : 1;
    }
    return enabled && hwr_billboards_floor_exception_enabled();
}

/* The engine paints a sector's mid-platform LAST and has no depth buffer, so a
 * platform surface always covers the sector's own ceiling and floor, however
 * the two are ordered in height (render_world_face_list_subpass, renderer.c:
 * ceiling, then floor, then walls, then secondary surfaces, then the platform).
 * Almost every authored platform stands above its floor and below its ceiling,
 * where the depth test reproduces that for free. These two cases -- a platform
 * top at or below its own floor, a platform underside at or above its own
 * ceiling -- are the ones where depth and paint order disagree, and they are
 * the ones routed through the depth exception. */
static void hwr_flat_identities(const struct hwr_world *world,
                                const struct hwr_sector *sector,
                                size_t sector_index,
                                enum hwr_surface_kind kind,
                                uint32_t *owner, uint32_t *exception)
{
    const struct hwr_slab *slab;
    uint32_t floor_id;
    uint32_t ceiling_id;

    *owner = 0;
    *exception = 0;
    if (!hwr_slab_exception_enabled() ||
        sector_index > HWR_FLAT_OWNER_INDEX_MAX ||
        sector->slab_index >= world->slab_count)
        return;
    slab = &world->slabs[sector->slab_index];
    floor_id = HWR_FLAT_OWNER_TAG | (uint32_t)sector_index;
    ceiling_id = floor_id | HWR_FLAT_OWNER_CEILING;
    if (slab->upper_z <= sector->floor_z) {
        if (kind == HWR_SURFACE_FLOOR)
            *owner = floor_id;
        else if (kind == HWR_SURFACE_SLAB_UPPER)
            *exception = floor_id;
    }
    if (slab->lower_z >= sector->ceiling_z) {
        if (kind == HWR_SURFACE_CEILING)
            *owner = ceiling_id;
        else if (kind == HWR_SURFACE_SLAB_LOWER)
            *exception = ceiling_id;
    }
}

static int hwr_emit_flat(struct hwr_scene_mesh *mesh, const struct hwr_world *world,
                         const struct hwr_sector *sector, size_t sector_index,
                         const struct hwr_point *points, const uint16_t *triangles,
                         size_t triangle_count, enum hwr_surface_kind kind,
                         int16_t z, int16_t id, uint16_t aux, uint8_t scale,
                         int face_down)
{
    struct hwr_mesh_surface *surface;
    struct hwr_mesh_vertex *vertices;
    uint8_t mapping_flags = 0;
    size_t count = triangle_count * 3u;
    size_t i;
    if (id >= 0 && (uint16_t)id == world->world_span_colorkey)
        return 0;
    surface = hwr_add_surface(mesh, kind, count);
    if (surface == NULL)
        return -1;
    hwr_flat_identities(world, sector, sector_index, kind,
                        &surface->owner_identity, &surface->exception_identity);
    /* The loop, not just its triangulation: the per-frame emit clips the
     * polygon the engine shades the corners of. */
    if (hwr_store_surface_loop(mesh, surface, points, triangle_count + 2u) != 0)
        return -1;
    surface->sector_index = sector_index;
    surface->aux = aux;
    surface->scale = scale;
    surface->flags = HWR_SURFACE_FLAT | (face_down ? HWR_SURFACE_FACE_DOWN : 0);
    if (face_down) {
        mapping_flags = (uint8_t)(
            ((sector->texture_flip_flags & HWR_SECTOR_CEILING_FLIP_X) != 0
                 ? 2u : 0u) |
            ((sector->texture_flip_flags & HWR_SECTOR_CEILING_FLIP_Y) != 0
                 ? 4u : 0u));
    } else {
        mapping_flags = (uint8_t)(
            ((sector->texture_flip_flags & HWR_SECTOR_FLOOR_FLIP_X) != 0
                 ? 2u : 0u) |
            ((sector->texture_flip_flags & HWR_SECTOR_FLOOR_FLIP_Y) != 0
                 ? 4u : 0u));
    }
    hwr_finish_material(mesh, surface, sector, id, mapping_flags, 0);
    vertices = mesh->vertices + mesh->vertex_count;
    for (i = 0; i < triangle_count; i++) {
        uint16_t order[3];
        size_t k;
        order[0] = triangles[i * 3u];
        order[1] = triangles[i * 3u + (face_down ? 2u : 1u)];
        order[2] = triangles[i * 3u + (face_down ? 1u : 2u)];
        for (k = 0; k < 3; k++) {
            const struct hwr_point *point = &points[order[k]];
            hwr_set_vertex(&vertices[i * 3u + k],
                           (int16_t)(uint16_t)point->x,
                           (int16_t)(uint16_t)point->y, z, 0.0f, 0.0f);
        }
    }
    mesh->vertex_count += count;
    return 0;
}

/* Signed distance between two authored world coordinates. The world->view
 * transform is a 16-bit add (renderer.c:6767-6770), so a difference that
 * overflows the word is the short way round, not a map-sized span. */
static int32_t hwr_wrap16(int32_t value)
{
    return (int32_t)(int16_t)(uint16_t)value;
}

static int hwr_build_sector_flats(struct hwr_scene_mesh *mesh,
                                  const struct hwr_world *world,
                                  const struct hwr_sector *sector,
                                  size_t sector_index, int debug)
{
    struct hwr_point *points;
    uint16_t *triangles;
    size_t wall_count = sector->wall_count;
    size_t count;
    size_t i;
    int result = 0;
    if (wall_count < 3)
        goto degenerate;
    points = (struct hwr_point *)malloc(wall_count * sizeof *points);
    triangles = (uint16_t *)malloc((wall_count - 2u) * 3u * sizeof *triangles);
    if (points == NULL || triangles == NULL) {
        free(points);
        free(triangles);
        return hwr_mesh_fail(mesh, "out of memory triangulating sector %u",
                             (unsigned)sector_index);
    }
    /* Walk the loop by wrapped edge deltas so a loop authored across the
     * coordinate wrap becomes a simple polygon in linear space. The emitted
     * vertices are cast back to the authored words, so only the triangulation
     * sees the unwrapped run. */
    for (i = 0; i < wall_count; i++) {
        const struct hwr_wall *wall = &world->walls[sector->first_wall + i];
        const struct hwr_wall *next = &world->walls[
            sector->first_wall + (i + 1u) % wall_count];
        const struct hwr_vertex *vertex = &world->vertices[wall->vertex_a_index];
        if (wall->vertex_b_index != next->vertex_a_index) {
            free(points);
            free(triangles);
            goto degenerate;
        }
        if (i == 0) {
            points[0].x = vertex->world_x;
            points[0].y = vertex->world_y;
        } else {
            const struct hwr_vertex *previous = &world->vertices[
                world->walls[sector->first_wall + i - 1u].vertex_a_index];
            points[i].x = points[i - 1u].x +
                hwr_wrap16((int32_t)vertex->world_x -
                           (int32_t)previous->world_x);
            points[i].y = points[i - 1u].y +
                hwr_wrap16((int32_t)vertex->world_y -
                           (int32_t)previous->world_y);
        }
    }
    /* A moving sector parks its shared vertices exactly on a neighbour's own
     * vertices when it reaches a travel endpoint, which leaves consecutive
     * loop points coincident for as long as it waits there. The engine has no
     * polygon-validity test: a coincident pair is simply a zero-length edge in
     * its span walk and the sector keeps drawing. Compact those points away
     * rather than rejecting the loop; only a loop left with fewer than three
     * distinct points has genuinely no area. */
    count = 0;
    for (i = 0; i < wall_count; i++) {
        if (count != 0 && points[i].x == points[count - 1u].x &&
            points[i].y == points[count - 1u].y)
            continue;
        points[count++] = points[i];
    }
    while (count > 1u && points[0].x == points[count - 1u].x &&
           points[0].y == points[count - 1u].y)
        count--;
    if (count < 3u || hwr_triangulate(points, count, triangles) != 0) {
        free(points);
        free(triangles);
        goto degenerate;
    }
    result |= hwr_emit_flat(mesh, world, sector, sector_index, points, triangles,
                            count - 2u, HWR_SURFACE_CEILING, sector->ceiling_z,
                            sector->ceiling_flat_id,
                            sector->sentinel == -3 ? 0 : sector->ceiling_pan,
                            (uint8_t)((sector->flags >> 2) & 3u), 1);
    /* A slab is a full-footprint block sharing the sector's wall loop, so its
     * top surface covers exactly the same triangles as the base floor.  When
     * that top sits FLUSH with the floor, it fully hides a sentinel base floor
     * (a negative flat id, drawn as a solid colour); the engine treats such a
     * floor as absent and paints the slab top as the walkable surface.  Emitting
     * the base floor anyway leaves two coplanar flats that z-fight, and the solid
     * colour wins across one triangulation triangle -- a stray wedge below a
     * doorway.  Skip it and let the slab top stand in for the floor.  Only the
     * exactly-flush case qualifies: a slab hovering above the floor is a
     * suspended platform, and the base floor beneath it still shows. */
    if (sector->floor_flat_id >= 0 || sector->slab_index >= world->slab_count ||
        world->slabs[sector->slab_index].upper_z != sector->floor_z)
        result |= hwr_emit_flat(mesh, world, sector, sector_index, points,
                                triangles, count - 2u, HWR_SURFACE_FLOOR,
                                sector->floor_z, sector->floor_flat_id,
                                sector->floor_pan,
                                (uint8_t)((sector->flags >> 4) & 3u), 0);
    if (sector->slab_index < world->slab_count) {
        const struct hwr_slab *slab = &world->slabs[sector->slab_index];
        result |= hwr_emit_flat(mesh, world, sector, sector_index, points, triangles,
                                count - 2u, HWR_SURFACE_SLAB_LOWER, slab->lower_z,
                                (int16_t)slab->lower_surface_id, slab->lower_aux,
                                (uint8_t)((slab->flags >> 2) & 3u), 1);
        result |= hwr_emit_flat(mesh, world, sector, sector_index, points, triangles,
                                count - 2u, HWR_SURFACE_SLAB_UPPER, slab->upper_z,
                                (int16_t)slab->upper_surface_id, slab->upper_aux,
                                (uint8_t)((slab->flags >> 4) & 3u), 0);
    }
    free(points);
    free(triangles);
    return result != 0 ? -1 : 0;

degenerate:
    mesh->skipped_flats++;
    if (debug)
        fprintf(stderr, "[renderer_overhaul] sector %u has a degenerate wall loop; flat surfaces skipped\n",
                (unsigned)sector_index);
    return 0;
}

static int hwr_emit_portal_slab(struct hwr_scene_mesh *mesh,
                                const struct hwr_world *world,
                                const struct hwr_wall *wall,
                                const struct hwr_sector *sector,
                                const struct hwr_sector *neighbor)
{
    const struct hwr_slab *neighbor_slab;
    int16_t bottom;
    int16_t top;
    if (neighbor->slab_index >= world->slab_count)
        return 0;
    neighbor_slab = &world->slabs[neighbor->slab_index];
    bottom = neighbor_slab->lower_z;
    top = neighbor_slab->upper_z;
    if (bottom >= sector->ceiling_z || top <= sector->floor_z)
        return 0;
    if (bottom < sector->floor_z)
        bottom = sector->floor_z;
    if (top > sector->ceiling_z)
        top = sector->ceiling_z;
    if (sector->slab_index < world->slab_count) {
        const struct hwr_slab *source_slab = &world->slabs[sector->slab_index];
        if (bottom < source_slab->upper_z && top > source_slab->lower_z) {
            if (bottom >= source_slab->lower_z)
                bottom = source_slab->upper_z;
            if (top <= source_slab->upper_z)
                top = source_slab->lower_z;
        }
    }
    if (bottom >= top)
        return 0;
    return hwr_emit_wall(mesh, world, wall, sector, HWR_SURFACE_PORTAL_SLAB,
                         (int16_t)wall->middle_id, top, top, bottom, bottom, 0,
                         wall->mapping_flags);
}

static int hwr_primary_door_owns_sector(const struct hwr_world *world,
                                        uint16_t sector_offset)
{
    size_t i;
    for (i = 0; i < world->door_count; i++) {
        if (world->doors[i].owner_sector_offset == sector_offset)
            return 1;
    }
    return 0;
}

/* Stock's final portal-overlay gate drops only the doorway middle while the
 * private hinged-door record owns it. Upper/lower bands and sector geometry
 * remain ordinary world surfaces. */
static int hwr_portal_middle_suppressed(
    const struct hwr_world *world, const struct hwr_sector *sector,
    const struct hwr_sector *neighbor)
{
    uint16_t sentinel = (uint16_t)sector->sentinel;
    uint16_t neighbor_sentinel = (uint16_t)neighbor->sentinel;
    if (sentinel >= 0xfffeu) {
        if (hwr_primary_door_owns_sector(world, sector->section_offset))
            return 1;
        if (sentinel == 0xfffeu)
            return 1;
    }
    return neighbor_sentinel >= 0xfffeu &&
           hwr_primary_door_owns_sector(world, neighbor->section_offset);
}

int hwr_scene_build_mesh(struct hwr_scene_mesh *mesh,
                         const struct hwr_world *world, int debug)
{
    size_t i;
    if (mesh == NULL || world == NULL || !world->valid)
        return -1;
    hwr_scene_mesh_clear(mesh);
    hwr_collect_texture_references(mesh, world);
    if (hwr_reserve_cut_scratch(mesh, world->wall_count) != 0)
        return -1;
    for (i = 0; i < world->sector_count; i++) {
        const struct hwr_sector *sector = &world->sectors[i];
        size_t j;
        if (hwr_build_sector_flats(mesh, world, sector, i, debug) != 0)
            goto failed;
        for (j = 0; j < sector->wall_count; j++) {
            const struct hwr_wall *wall = &world->walls[sector->first_wall + j];
            if (wall->neighbor_sector_index == SIZE_MAX) {
                if (hwr_emit_wall(mesh, world, wall, sector,
                                  HWR_SURFACE_SOLID_MIDDLE,
                                  (int16_t)wall->middle_id,
                                  sector->ceiling_z, sector->ceiling_z,
                                  sector->floor_z, sector->floor_z,
                                  (wall->mapping_flags & 1u) != 0,
                                  wall->mapping_flags) != 0)
                    goto failed;
            } else {
                const struct hwr_sector *neighbor =
                    &world->sectors[wall->neighbor_sector_index];
                if (neighbor->ceiling_z < sector->ceiling_z &&
                    hwr_emit_wall(mesh, world, wall, sector,
                                  HWR_SURFACE_PORTAL_UPPER,
                                  (int16_t)wall->upper_id,
                                  sector->ceiling_z, sector->ceiling_z,
                                  neighbor->ceiling_z > sector->floor_z
                                      ? neighbor->ceiling_z : sector->floor_z,
                                  neighbor->ceiling_z > sector->floor_z
                                      ? neighbor->ceiling_z : sector->floor_z,
                                  0, wall->mapping_flags) != 0)
                    goto failed;
                if (neighbor->floor_z > sector->floor_z &&
                    hwr_emit_wall(mesh, world, wall, sector,
                                  HWR_SURFACE_PORTAL_LOWER,
                                  (int16_t)wall->lower_id,
                                  neighbor->floor_z < sector->ceiling_z
                                      ? neighbor->floor_z : sector->ceiling_z,
                                  neighbor->floor_z < sector->ceiling_z
                                      ? neighbor->floor_z : sector->ceiling_z,
                                  sector->floor_z, sector->floor_z,
                                  0, wall->mapping_flags) != 0)
                    goto failed;
                if (hwr_portal_middle_suppressed(
                        world, sector, neighbor)) {
                    /* The active private door supplies this opening's middle. */
                } else if ((wall->mapping_flags & 1u) != 0) {
                    int16_t top = sector->ceiling_z < neighbor->ceiling_z
                                ? sector->ceiling_z : neighbor->ceiling_z;
                    int16_t bottom = sector->floor_z > neighbor->floor_z
                                   ? sector->floor_z : neighbor->floor_z;
                    if ((wall->mapping_flags &
                         (HWR_TEX_TRANSPARENT |
                          HWR_TEX_TRANSPARENT_FIXED_SIZE)) ==
                        (HWR_TEX_TRANSPARENT |
                         HWR_TEX_TRANSPARENT_FIXED_SIZE) &&
                        sector->texture_map_override != 0) {
                        int16_t height = (int16_t)(
                            (sector->texture_map_override > 0
                                 ? sector->texture_map_override
                                 : -sector->texture_map_override) * 4);
                        /* The engine pins positive overrides to the opening
                         * ceiling and negative overrides to its floor. */
                        if (sector->texture_map_override > 0)
                            bottom = (int16_t)(top - height);
                        else
                            top = (int16_t)(bottom + height);
                    }
                    /* Span staging derives tiled vertical scale from this
                     * emitted height; image-fit stretches over the same band. */
                    if (hwr_emit_wall(mesh, world, wall, sector,
                                      HWR_SURFACE_PORTAL_MIDDLE,
                                      (int16_t)wall->middle_id,
                                      top, top, bottom, bottom, 1,
                                      wall->mapping_flags) != 0)
                        goto failed;
                } else if (hwr_emit_portal_slab(mesh, world, wall, sector, neighbor) != 0) {
                    goto failed;
                }
            }
            /* A sky opening: an EDGE_MAP face whose middle texture is the span
             * colorkey.  The engine never draws such a face's body (the face
             * dispatcher rejects the colorkey id), so the face renders only its
             * edge spans, and the EDGE_MAP fill runs from the view top down to
             * the face's floor boundary -- the whole face is skybox and
             * everything beyond it is hidden.  When the face sits entirely
             * below the view the engine's dedicated path fills the full
             * viewport instead; a cap standing on the floor boundary projects
             * to the same region in both cases.  The cap occludes so geometry
             * beyond the face is dropped, and the wall's own bands survive it
             * through the occlusion pass's polygon offset.  The colorkey
             * middle is the load-bearing condition: an EDGE_MAP face with a
             * real middle texture draws normally, and skying it over would
             * blanket walls the engine shows.
             *
             * A solid direct-colour upper band is a lintel, and it disqualifies
             * the opening: the face frames a see-through doorway (e.g. ANUBDOOR)
             * whose top is capped by opaque geometry, not an aperture onto the
             * sky.  The engine draws such a face's parallax sky only above its
             * own top edge (the per-band cap above already emits that) and
             * leaves the colorkey middle see-through, so the door behind shows;
             * it never occludes with sky.  Emitting the full-height occluding
             * cap here would blanket that doorway with sky (the ANUBIS door
             * vanishing behind parallax sky), so a framed opening skips it.
             * Genuine sky openings leave the upper band empty (ceilings level)
             * or colorkey, so the sentinel/colorkey upper id keeps them skied. */
            int framed_opening = (wall->upper_id & 0x8000u) != 0 &&
                                 wall->upper_id != 0xFFFFu;
            if ((wall->mapping_flags & 0x40u) != 0 &&
                (uint16_t)wall->middle_id == world->world_span_colorkey &&
                !framed_opening) {
                const struct hwr_vertex *a = &world->vertices[wall->vertex_a_index];
                const struct hwr_vertex *b = &world->vertices[wall->vertex_b_index];
                int16_t base = sector->floor_z;
                if (wall->neighbor_sector_index != SIZE_MAX) {
                    const struct hwr_sector *nb =
                        &world->sectors[wall->neighbor_sector_index];
                    if (nb->floor_z > base)
                        base = nb->floor_z;
                }
                if (hwr_emit_sky_cap(mesh, a, b, base, base, 1, 0,
                                     wall->sector_index) != 0)
                    goto failed;
            }
        }
    }
    if (hwr_emit_doors(mesh, world) != 0)
        goto failed;
    mesh->valid = 1;
    return 0;

failed:
    if (mesh->error[0] == 0)
        snprintf(mesh->error, sizeof mesh->error, "mesh construction failed");
    return -1;
}

static void hwr_scene_animations_clear(struct hwr_scene *scene)
{
    size_t i;
    for (i = 0; i < scene->animation_count; i++)
        free(scene->animations[i].frames);
    free(scene->animations);
    scene->animations = NULL;
    scene->animation_count = 0;
}

/* Tears the scene down. `keep_store` retains everything the resident texture
 * store describes -- the GL objects built around it and the mesh/sprite
 * directories whose offsets address it -- because the bytes they point at are
 * still in the buffer. Everything derived from THIS map (the world mesh, the
 * per-map texture table, animations, streams, visibility) always goes. */
static void hwr_scene_clear_keeping(struct hwr_scene *scene, int keep_store)
{
    struct hwr_meshes kept_meshes;
    struct hwr_billboards kept_billboards;
    size_t i;
    if (scene == NULL)
        return;
    if (keep_store) {
        kept_meshes = scene->object_meshes;
        kept_billboards = scene->billboards;
    } else {
        memset(&kept_meshes, 0, sizeof kept_meshes);
        memset(&kept_billboards, 0, sizeof kept_billboards);
        hwr_gl_scene_clear();
        hwr_scene_store_forget();
        hwr_meshes_clear(&scene->object_meshes);
        hwr_billboards_clear(&scene->billboards);
    }
    hwr_scene_mesh_clear(&scene->mesh);
    hwr_scene_animations_clear(scene);
    free(scene->stream_vertices);
    free(scene->translucent_runs);
    free(scene->translucent_run_flags);
    free(scene->visible);
    free(scene->oracle_visible);
    free(scene->spans);
    free(scene->reflection_spans);
    free(scene->wide_spans);
    for (i = 0; i < HWR_REFLECTION_MAX; i++) {
        free(scene->reflections[i].vertices);
        free(scene->reflections[i].runs);
        free(scene->reflections[i].run_flags);
    }
    memset(scene, 0, sizeof *scene);
    if (keep_store) {
        scene->object_meshes = kept_meshes;
        scene->billboards = kept_billboards;
    }
}

void hwr_scene_clear(struct hwr_scene *scene)
{
    hwr_scene_clear_keeping(scene, 0);
}

const char *hwr_scene_error(const struct hwr_scene *scene)
{
    return scene != NULL && scene->error[0] != 0 ? scene->error : "unknown scene error";
}

static int hwr_append_bytes(uint8_t **bytes, size_t *size, size_t *capacity,
                            const uint8_t *source, size_t count)
{
    uint8_t *next;
    size_t wanted;
    size_t next_capacity;
    if (count > SIZE_MAX - *size)
        return -1;
    wanted = *size + count;
    if (wanted > *capacity) {
        next_capacity = *capacity != 0 ? *capacity : 4096u;
        while (next_capacity < wanted) {
            if (next_capacity > SIZE_MAX / 2u)
                return -1;
            next_capacity *= 2u;
        }
        next = (uint8_t *)realloc(*bytes, next_capacity);
        if (next == NULL)
            return -1;
        *bytes = next;
        *capacity = next_capacity;
    }
    memcpy(*bytes + *size, source, count);
    *size = wanted;
    return 0;
}

/* An unresolvable id whose map slot is empty may still name a shared-archive
 * placeholder at the same index: those mark surfaces another renderer owns
 * (the door records live this way), so the faces are dropped, not filled. */
static int hwr_texture_is_door_placeholder(struct hwr_das_archive *map_archive,
                                           struct hwr_das_archive *shared_archive,
                                           uint16_t logical,
                                           const struct hwr_das_entry *entry)
{
    const struct hwr_das_entry *shared_entry;
    if (entry == NULL || entry->file_offset != 0 || entry->payload_size != 0)
        return 0;
    if (map_archive == NULL || logical >= map_archive->entry_count ||
        entry != &map_archive->entries[logical])
        return 0;
    if (shared_archive == NULL || logical >= shared_archive->entry_count)
        return 0;
    shared_entry = &shared_archive->entries[logical];
    if (hwr_das_decode(shared_archive, logical) != 0)
        return 0;
    return shared_entry->resource_class == HWR_DAS_CLASS_PLACEHOLDER_SPACE ||
           shared_entry->resource_class == HWR_DAS_CLASS_PLACEHOLDER_DOLLAR;
}

static struct hwr_das_entry *hwr_resolve_entry(struct hwr_das_archive *map_archive,
                                                struct hwr_das_archive *shared_archive,
                                                uint16_t logical,
                                                struct hwr_das_archive **owner,
                                                size_t *entry_id)
{
    if (logical < 0x1200u) {
        *owner = map_archive;
        *entry_id = logical;
    } else if (logical < 0x1600u) {
        *owner = shared_archive;
        *entry_id = logical - 0x1200u;
    } else {
        return NULL;
    }
    if (*owner == NULL || *entry_id >= (*owner)->entry_count)
        return NULL;
    return &(*owner)->entries[*entry_id];
}

static int hwr_resolve_grouped_texture(const struct hwr_das_entry *entry,
                                       const struct roth_api_v1 *api,
                                       uint16_t logical, int debug,
                                       struct hwr_das_texture_view *view,
                                       int *flip_toggle)
{
    uint16_t selector;
    uint16_t child_word;
    size_t child_word_offset;
    size_t child_offset;
    if (entry == NULL || api == NULL || api->game_ram == NULL ||
        entry->raw_payload == NULL || entry->raw_payload_size < 8u)
        return -1;
    selector = hwr_scene_read_u16(entry->raw_payload + 6u);
    if ((selector & 0x8000u) != 0) {
        child_word_offset = 8u;
        if (debug)
            fprintf(stderr, "[renderer_overhaul] texture %u grouped directional selector uses static k=0\n",
                    logical);
    } else if ((selector & 0x4000u) != 0) {
        child_word_offset = 8u;
        if (debug)
            fprintf(stderr, "[renderer_overhaul] texture %u grouped per-face selector uses static first variant\n",
                    logical);
    } else {
        uint16_t part_offset = api->game_ram->u16(
            HWR_GROUP_PART_TABLE_VA + (uint32_t)selector);
        child_word_offset = 8u + part_offset;
    }
    if (child_word_offset > entry->raw_payload_size ||
        entry->raw_payload_size - child_word_offset < 2u)
        return -1;
    child_word = hwr_scene_read_u16(entry->raw_payload + child_word_offset);
    if (hwr_das_group_child_offset(entry, child_word, &child_offset,
                                   flip_toggle) != 0)
        return -1;
    return hwr_das_group_child_view(entry, child_offset, view);
}

static size_t hwr_scene_quad_count(const struct hwr_scene_mesh *mesh)
{
    size_t quads = 0;
    size_t i;
    for (i = 0; i < mesh->surface_count; i++) {
        if ((mesh->surfaces[i].flags & HWR_SURFACE_FLAT) == 0)
            quads++;
    }
    return quads;
}

/* Fields of an animated block that the playback clocks read. Offsets are into
 * the payload as the archive stores it; the engine addresses the same fields
 * ten bytes further along, past the runtime header it prefixes in memory. */
#define HWR_ANIM_FIELD_SUBIMAGE_DIVISOR 0x0au
#define HWR_ANIM_FIELD_DELTA_COUNT      0x0cu
#define HWR_ANIM_FIELD_DELTA_RATE       0x11u
#define HWR_ANIM_SUBIMAGE_SENTINEL      0xfffeu
#define HWR_ANIM_HEADER_SIZE            0x1au

/* Records an animated texture so its pixels can be swapped as it plays. The
 * frames are copied out because the archive releases its decoded copy as soon
 * as the texture store is packed. Animations whose frames do not all share one
 * size are left on their first frame: the store gives each texture a fixed
 * slot, and a differently sized frame would not fit it. */
static int hwr_scene_register_animation(struct hwr_scene *scene,
                                        const struct hwr_das_entry *entry,
                                        uint32_t offset)
{
    struct hwr_scene_animation *animations;
    struct hwr_scene_animation *animation;
    size_t frame_bytes;
    size_t i;
    if (entry->frames == NULL || entry->frame_count < 2u ||
        entry->frame_count > UINT16_MAX || entry->raw_payload == NULL ||
        entry->raw_payload_size < HWR_ANIM_HEADER_SIZE)
        return 0;
    frame_bytes = (size_t)entry->frames[0].width * entry->frames[0].height;
    if (frame_bytes == 0)
        return 0;
    for (i = 0; i < entry->frame_count; i++) {
        if (entry->frames[i].pixels == NULL ||
            entry->frames[i].width != entry->frames[0].width ||
            entry->frames[i].height != entry->frames[0].height)
            return 0;
    }
    if (frame_bytes > SIZE_MAX / entry->frame_count)
        return -1;
    animations = (struct hwr_scene_animation *)realloc(
        scene->animations,
        (scene->animation_count + 1u) * sizeof *animations);
    if (animations == NULL)
        return -1;
    scene->animations = animations;
    animation = &animations[scene->animation_count];
    memset(animation, 0, sizeof *animation);
    animation->frames = (uint8_t *)malloc(frame_bytes * entry->frame_count);
    if (animation->frames == NULL)
        return -1;
    for (i = 0; i < entry->frame_count; i++)
        memcpy(animation->frames + i * frame_bytes, entry->frames[i].pixels,
               frame_bytes);
    animation->frame_bytes = (uint32_t)frame_bytes;
    animation->offset = offset;
    animation->frame_count = (uint16_t)entry->frame_count;
    if (hwr_scene_read_u16(entry->raw_payload + HWR_ANIM_FIELD_DELTA_COUNT) ==
        HWR_ANIM_SUBIMAGE_SENTINEL) {
        animation->kind = HWR_ANIMATION_SUBIMAGE;
        animation->divisor = hwr_scene_read_u16(entry->raw_payload +
                                                HWR_ANIM_FIELD_SUBIMAGE_DIVISOR);
    } else {
        animation->kind = HWR_ANIMATION_DELTA;
        animation->rate = entry->raw_payload[HWR_ANIM_FIELD_DELTA_RATE];
    }
    scene->animation_count++;
    return 0;
}

static size_t hwr_scene_animation_bytes(const struct hwr_scene *scene)
{
    size_t bytes = 0;
    size_t i;
    for (i = 0; i < scene->animation_count; i++)
        bytes += (size_t)scene->animations[i].frame_bytes *
                 scene->animations[i].frame_count;
    return bytes;
}

/* HWR_VERIFY_STORE hashes the whole store at upload and reads it back to
 * compare. A store whose tail is rewritten in place has no such hash, so while
 * that self-test is on, every rebuild packs and uploads the whole store and the
 * check keeps its exact meaning. */
static int hwr_scene_store_verify_forces_full(void)
{
    static int state = -1;
    if (state < 0) {
        const char *setting = getenv("HWR_VERIFY_STORE");
        state = setting != NULL && setting[0] != '0' && setting[0] != 0 ? 1 : 0;
    }
    return state;
}

/* The shared block may be kept when it is resident, persistence is on, and the
 * shared DAS still open is the very file object it was packed from. */
static int hwr_scene_keep_shared_block(const struct hwr_das_archive *shared)
{
    return g_store_resident && hwr_das_persist_enabled() &&
           !hwr_scene_store_verify_forces_full() &&
           hwr_gl_scene_store_reserve() != 0 &&
           shared != NULL && shared->entries != NULL &&
           shared->open_serial != 0 &&
           shared->open_serial == g_store_shared_serial;
}

/* The map block additionally needs the sprite-closure policy to be the one it
 * was packed under: a closure scopes the map sprite tail to one map's reachable
 * set, which is not a property of the DAS file. */
static int hwr_scene_keep_map_block(const struct hwr_das_archive *map)
{
    return map != NULL && map->entries != NULL && map->open_serial != 0 &&
           map->open_serial == g_store_map_serial &&
           hwr_billboards_closure_enabled() == g_store_closure;
}

/* Starts a pack run whose first byte occupies store offset `base`. The bytes in
 * front of `base` are the resident prefix: they are not held here and never
 * written, so the run is addressed by absolute store offset and every directory
 * entry a packer records is a store offset, exactly as it is when the whole
 * store is packed at once. */
static int hwr_store_run_begin(uint8_t **bytes, size_t *size, size_t *capacity,
                               size_t base)
{
    uint8_t *buffer;
    if (base > SIZE_MAX - HWR_STORE_RUN_HINT)
        return -1;
    buffer = (uint8_t *)malloc(base + HWR_STORE_RUN_HINT);
    if (buffer == NULL)
        return -1;
    *bytes = buffer;
    *size = base;
    *capacity = base + HWR_STORE_RUN_HINT;
    return 0;
}

static int hwr_pack_and_upload_store(struct hwr_scene *scene,
                                     const struct hwr_world *world,
                                     struct hwr_das_archive *map_archive,
                                     struct hwr_das_archive *shared_archive,
                                     const struct hwr_objects *objects,
                                     const struct roth_api_v1 *api,
                                     int debug, int keep_shared, int keep_map,
                                     int *out_retry)
{
    uint8_t *bytes = NULL;
    size_t size = 0;
    size_t capacity = 0;
    size_t base;
    size_t shared_size;
    size_t prefix_size;
    uint8_t zero = 0;
    uint16_t logical;
    int has_sky = 0;
    size_t surface_index;
    struct hwr_scene_texture_stats stats;
    memset(&stats, 0, sizeof stats);
    *out_retry = 0;
    hwr_scene_animations_clear(scene);
    base = keep_map ? g_store_prefix_size
                    : (keep_shared ? g_store_shared_size : 0);
    if (hwr_store_run_begin(&bytes, &size, &capacity, base) != 0)
        return hwr_scene_fail(scene, "out of memory creating texture buffer");
    if (!keep_shared) {
        /* Offset zero is a known zero texel, so every "nothing here" reference
         * lands on a defined byte. */
        if (hwr_append_bytes(&bytes, &size, &capacity, &zero, 1) != 0) {
            free(bytes);
            return hwr_scene_fail(scene, "out of memory creating texture buffer");
        }
        if (hwr_billboards_pack_shared(&scene->billboards, shared_archive,
                                       &bytes, &size, &capacity) != 0) {
            free(bytes);
            return hwr_scene_fail(scene, "%s", scene->billboards.error);
        }
    }
    /* Every block reports its own phase whether or not it had to be produced,
     * so one timing line reads the same as the next and a block that cost
     * nothing says so with a zero instead of an absence. */
    hwr_perf_phase("bb-shared");
    shared_size = keep_shared ? g_store_shared_size : size;
    if (!keep_map) {
        /* Copy mesh geometry and append its face dependencies before world
         * texture packing can release any overlapping decoded DAS entry. */
        if (hwr_meshes_pack(&scene->object_meshes, map_archive, shared_archive,
                            &bytes, &size, &capacity) != 0) {
            free(bytes);
            return hwr_scene_fail(scene, "%s",
                                  hwr_meshes_error(&scene->object_meshes));
        }
        hwr_perf_phase("obj-meshes");
        if (hwr_billboards_pack_map(&scene->billboards, map_archive, objects,
                                    api, &bytes, &size, &capacity) != 0) {
            free(bytes);
            return hwr_scene_fail(scene, "%s", scene->billboards.error);
        }
    } else {
        hwr_perf_phase("obj-meshes");
    }
    hwr_perf_phase("bb-map");
    prefix_size = keep_map ? g_store_prefix_size : size;
    memset(scene->textures, 0, sizeof scene->textures);
    scene->sky_valid = 0;
    for (surface_index = 0; surface_index < scene->mesh.surface_count;
         surface_index++) {
        if ((scene->mesh.surfaces[surface_index].flags & HWR_SURFACE_SKY) != 0) {
            has_sky = 1;
            break;
        }
    }
    for (logical = 0; logical < 0x1600u; logical++) {
        struct hwr_das_archive *owner = NULL;
        struct hwr_das_entry *entry;
        struct hwr_das_texture_view grouped_view;
        size_t entry_id = 0;
        const uint8_t *pixels = NULL;
        size_t pixel_count = 0;
        uint16_t texture_flags = 0;
        uint16_t texture_width = 0;
        uint16_t texture_height = 0;
        int grouped_flip = 0;
        enum hwr_das_class resolved_class = HWR_DAS_CLASS_UNREAD;
        if (!hwr_scene_mesh_references(&scene->mesh, logical))
            continue;
        entry = hwr_resolve_entry(map_archive, shared_archive, logical, &owner, &entry_id);
        if (entry != NULL && hwr_das_decode(owner, entry_id) == 0) {
            if (entry->resource_class == HWR_DAS_CLASS_PLAIN &&
                entry->raw_payload != NULL && entry->raw_payload_size >= 6u) {
                pixels = entry->raw_payload + 6u;
                pixel_count = (size_t)entry->width * entry->height;
                texture_flags = entry->payload_flags;
                texture_width = entry->width;
                texture_height = entry->height;
                resolved_class = HWR_DAS_CLASS_PLAIN;
            } else if (entry->resource_class == HWR_DAS_CLASS_GROUPED &&
                       hwr_resolve_grouped_texture(entry, api, logical, debug,
                                                  &grouped_view,
                                                  &grouped_flip) == 0) {
                pixels = grouped_view.pixels;
                pixel_count = grouped_view.pixel_count;
                texture_flags = grouped_view.flags;
                texture_width = grouped_view.width;
                texture_height = grouped_view.height;
                resolved_class = HWR_DAS_CLASS_GROUPED;
            } else if (entry->resource_class == HWR_DAS_CLASS_ANIMATED &&
                       entry->frame_count != 0 && entry->frames[0].pixels != NULL) {
                pixels = entry->frames[0].pixels;
                pixel_count = (size_t)entry->frames[0].width * entry->frames[0].height;
                texture_flags = entry->payload_flags;
                texture_width = entry->frames[0].width;
                texture_height = entry->frames[0].height;
                resolved_class = HWR_DAS_CLASS_ANIMATED;
            }
        }
        if (pixels != NULL && pixel_count != 0 && size <= UINT32_MAX &&
            pixel_count <= UINT32_MAX - size) {
            scene->textures[logical].offset = (uint32_t)size;
            scene->textures[logical].width = texture_width;
            scene->textures[logical].height = texture_height;
            scene->textures[logical].qflags = texture_flags;
            scene->textures[logical].flip_toggle = (uint8_t)grouped_flip;
            if (hwr_append_bytes(&bytes, &size, &capacity, pixels, pixel_count) != 0) {
                free(bytes);
                return hwr_scene_fail(scene, "out of memory packing texture %u", logical);
            }
            if (resolved_class == HWR_DAS_CLASS_PLAIN)
                stats.plain++;
            else if (resolved_class == HWR_DAS_CLASS_GROUPED)
                stats.grouped++;
            else if (resolved_class == HWR_DAS_CLASS_ANIMATED) {
                stats.animated++;
                if (hwr_scene_register_animation(scene, entry,
                                                 scene->textures[logical].offset) != 0) {
                    hwr_das_release_decoded(owner, entry_id);
                    free(bytes);
                    return hwr_scene_fail(scene, "out of memory recording animation %u",
                                          logical);
                }
            }
        } else if (hwr_texture_is_door_placeholder(map_archive, shared_archive,
                                                   logical, entry)) {
            scene->textures[logical].skip = 1;
            if (debug)
                fprintf(stderr, "[renderer_overhaul] texture %u is a placeholder resource; surfaces skipped\n",
                        logical);
        } else {
            scene->textures[logical].direct = 1;
            scene->textures[logical].direct_index = (uint8_t)logical;
            scene->textures[logical].width = 1;
            scene->textures[logical].height = 1;
            stats.fill++;
            stats.fallbacks++;
            if (debug)
                fprintf(stderr, "[renderer_overhaul] texture %u could not be resolved; using palette fill %u\n",
                        logical, (unsigned)(uint8_t)logical);
        }
        if (entry != NULL)
            hwr_das_release_decoded(owner, entry_id);
    }
    {
        struct hwr_das_archive *owner = NULL;
        struct hwr_das_entry *entry;
        size_t entry_id = 0;
        const uint8_t *pixels = NULL;
        size_t pixel_count = 0;
        uint16_t sky_width = 0;
        uint16_t sky_height = 0;
        uint16_t sky_source_offset = 0;
        uint8_t projection[HWR_SKY_PROJECTION_TABLE_SIZE];
        size_t packed_pixel_count = 0;
        size_t i;

        entry = has_sky ? hwr_resolve_entry(map_archive, shared_archive,
                                            world->sky_fat_index, &owner,
                                            &entry_id) : NULL;
        if (entry != NULL && hwr_das_decode(owner, entry_id) == 0) {
            if (entry->resource_class == HWR_DAS_CLASS_PLAIN &&
                entry->raw_payload != NULL && entry->raw_payload_size >= 6u) {
                pixels = entry->raw_payload + 6u;
                sky_width = entry->width;
                sky_height = entry->height;
                pixel_count = (size_t)sky_width * sky_height;
                packed_pixel_count = pixel_count;
                /* DAS selectors normally address the pixels at block+0x10.
                 * Flag 0x100 adds the block's word at 0x14 to that base. */
                if ((entry->payload_flags & 0x100u) != 0 &&
                    entry->raw_payload_size >= 0x0cu) {
                    size_t available = entry->raw_payload_size - 6u;
                    sky_source_offset = hwr_scene_read_u16(
                        entry->raw_payload + 0x0au);
                    if (pixel_count > available ||
                        sky_source_offset > available - pixel_count) {
                        pixels = NULL;
                    } else {
                        packed_pixel_count += sky_source_offset;
                    }
                }
            } else if (entry->resource_class == HWR_DAS_CLASS_ANIMATED &&
                       entry->frame_count != 0 && entry->frames[0].pixels != NULL) {
                pixels = entry->frames[0].pixels;
                sky_width = entry->frames[0].width;
                sky_height = entry->frames[0].height;
                pixel_count = (size_t)sky_width * sky_height;
                packed_pixel_count = pixel_count;
            }
        }
        if (pixels != NULL && sky_width == 256u && sky_height <= 256u &&
            size <= UINT32_MAX && packed_pixel_count <= UINT32_MAX - size) {
            scene->sky_texture_offset = (uint32_t)size;
            scene->sky_source_offset = sky_source_offset;
            scene->sky_texture_height = sky_height;
            scene->sky_row_bias = entry != NULL && (entry->fat_flags & 2u) != 0
                                ? entry->captured_first_byte : 0;
            if (hwr_append_bytes(&bytes, &size, &capacity,
                                 pixels, packed_pixel_count) != 0) {
                if (entry != NULL)
                    hwr_das_release_decoded(owner, entry_id);
                free(bytes);
                return hwr_scene_fail(scene, "out of memory packing sky texture %u",
                                      world->sky_fat_index);
            }
            if (size > UINT32_MAX - sizeof projection) {
                if (entry != NULL)
                    hwr_das_release_decoded(owner, entry_id);
                free(bytes);
                return hwr_scene_fail(scene, "sky projection table exceeds texture store");
            }
            for (i = 0; i < sizeof projection; i++)
                projection[i] = api->game_ram->u8(
                    HWR_SKY_PROJECTION_TABLE_VA + (uint32_t)i);
            scene->sky_projection_offset = (uint32_t)size;
            if (hwr_append_bytes(&bytes, &size, &capacity,
                                 projection, sizeof projection) != 0) {
                if (entry != NULL)
                    hwr_das_release_decoded(owner, entry_id);
                free(bytes);
                return hwr_scene_fail(scene, "out of memory packing sky projection table");
            }
            scene->sky_valid = 1;
            /* The skybox can itself be an animated block, and the game plays it
             * like any other. It reads its pixels from the same store, so
             * recording it here lets the ordinary frame swap animate it. */
            if (entry != NULL &&
                entry->resource_class == HWR_DAS_CLASS_ANIMATED &&
                hwr_scene_register_animation(scene, entry,
                                             scene->sky_texture_offset) != 0) {
                hwr_das_release_decoded(owner, entry_id);
                free(bytes);
                return hwr_scene_fail(scene, "out of memory recording sky animation %u",
                                      world->sky_fat_index);
            }
            if (debug)
                fprintf(stderr, "[renderer_overhaul] sky texture %u: 256x%u, row bias %u, source offset %u\n",
                        world->sky_fat_index, sky_height, scene->sky_row_bias,
                        scene->sky_source_offset);
        } else if (has_sky && debug) {
            fprintf(stderr, "[renderer_overhaul] sky texture %u could not be resolved\n",
                    world->sky_fat_index);
        }
        if (entry != NULL)
            hwr_das_release_decoded(owner, entry_id);
    }
    if (debug) {
        /* Offset-to-texture map, so a captured surface-identity frame can be
         * read back to the texture that produced each pixel. */
        size_t t;
        for (t = 0; t < 0x1600u; t++) {
            if (scene->textures[t].width == 0 && scene->textures[t].height == 0)
                continue;
            fprintf(stderr, "[renderer_overhaul] texmap id=%u offset=%u %ux%u qflags=0x%04x%s\n",
                    (unsigned)t, (unsigned)scene->textures[t].offset,
                    (unsigned)scene->textures[t].width,
                    (unsigned)scene->textures[t].height,
                    (unsigned)scene->textures[t].qflags,
                    scene->textures[t].direct ? " direct" : "");
        }
    }
    if (debug)
        fprintf(stderr, "[renderer_overhaul] scene stats: surfaces=%u quads=%u textures plain=%u grouped=%u animated=%u (playing %u) fill=%u fallbacks=%u\n",
                (unsigned)scene->mesh.surface_count,
                (unsigned)hwr_scene_quad_count(&scene->mesh),
                (unsigned)stats.plain, (unsigned)stats.grouped,
                (unsigned)stats.animated, (unsigned)scene->animation_count,
                (unsigned)stats.fill, (unsigned)stats.fallbacks);
    if (debug)
        fprintf(stderr, "[renderer_overhaul] animation frames retained: %u bytes across %u textures\n",
                (unsigned)hwr_scene_animation_bytes(scene),
                (unsigned)scene->animation_count);
    if (debug)
        fprintf(stderr,
                "[renderer_overhaul] mesh atlas: resources=%u faces=%u materials=%u invalid=%u store=%u bytes\n",
                (unsigned)scene->object_meshes.stat_resources,
                (unsigned)scene->object_meshes.face_count,
                (unsigned)scene->object_meshes.material_count,
                (unsigned)scene->object_meshes.stat_invalid, (unsigned)size);
    hwr_perf_phase("world-atlas");
    if (debug)
        fprintf(stderr, "[renderer_overhaul] billboard atlas: map slots=%u shared slots=%u store=%u bytes\n",
                (unsigned)scene->billboards.map_dir_count,
                (unsigned)scene->billboards.shared_dir_count, (unsigned)size);
    if (base != 0) {
        /* A map that packs no texels of its own would leave nothing to write;
         * one pad byte keeps the write well formed and the store's length
         * honest. */
        if (size == base &&
            hwr_append_bytes(&bytes, &size, &capacity, &zero, 1) != 0) {
            free(bytes);
            return hwr_scene_fail(scene, "out of memory closing the texture store");
        }
        /* Only this map's block is new; the rest of the store is already where
         * the directories say it is. */
        if (!hwr_gl_scene_update_store(base, bytes + base, size - base,
                                       world->luts.colormap, world->luts.tint,
                                       world->luts.glow, world->luts.blend)) {
            /* This map's block outgrew the store's headroom (or the store went
             * away): ask the caller to pack the whole thing instead. */
            free(bytes);
            hwr_scene_store_forget();
            *out_retry = 1;
            return -1;
        }
    } else if (!hwr_gl_scene_upload(bytes, size, size + HWR_STORE_SLACK,
                                    prefix_size, world->luts.colormap,
                                    world->luts.tint, world->luts.glow,
                                    world->luts.blend)) {
        free(bytes);
        hwr_scene_store_forget();
        return hwr_scene_fail(scene, "%s", hwr_gl_last_error());
    }
    hwr_perf_phase("upload");
    free(bytes);
    g_store_shared_serial = shared_archive != NULL ? shared_archive->open_serial : 0;
    g_store_map_serial = map_archive != NULL ? map_archive->open_serial : 0;
    g_store_closure = hwr_billboards_closure_enabled();
    g_store_shared_size = shared_size;
    g_store_prefix_size = prefix_size;
    g_store_resident = 1;
    scene->uploaded = 1;
    return 0;
}

/* Packs this map's texels into the store and gets them onto the GPU, keeping
 * whatever the resident store already holds that this map can still use. The
 * one way that can fail without the store being broken is a per-map block too
 * large for the headroom, and the answer to that is to build the whole store
 * once more. */
static int hwr_upload_referenced_textures(struct hwr_scene *scene,
                                          const struct hwr_world *world,
                                          struct hwr_das_archive *map_archive,
                                          struct hwr_das_archive *shared_archive,
                                          const struct hwr_objects *objects,
                                          const struct roth_api_v1 *api,
                                          int debug, int keep_shared,
                                          int keep_map)
{
    int retry = 0;
    int result = hwr_pack_and_upload_store(scene, world, map_archive,
                                           shared_archive, objects, api, debug,
                                           keep_shared, keep_map, &retry);
    if (result == 0 || !retry)
        return result;
    fprintf(stderr, "[renderer_overhaul] this map's texels do not fit the resident store; rebuilding all of it\n");
    return hwr_pack_and_upload_store(scene, world, map_archive, shared_archive,
                                     objects, api, debug, 0, 0, &retry);
}

int hwr_scene_rebuild(struct hwr_scene *scene, const struct hwr_world *world,
                      struct hwr_das_archive *map_archive,
                      struct hwr_das_archive *shared_archive,
                      const struct hwr_objects *objects,
                      const struct roth_api_v1 *api, int debug)
{
    int keep_shared;
    int keep_map;
    if (scene == NULL)
        return -1;
    /* Close out the interval between the reload and this rebuild (whatever ran
     * on the build dispatch before it) so it is not folded into the mesh
     * number. A no-op outside a timing session. */
    hwr_perf_phase("dispatch-gap");
    /* Decided before the teardown, because it decides how much of the teardown
     * there is: what the resident store still holds is what does not have to be
     * produced again. */
    keep_shared = hwr_scene_keep_shared_block(shared_archive);
    keep_map = keep_shared && hwr_scene_keep_map_block(map_archive);
    hwr_scene_clear_keeping(scene, keep_shared);
    if (!keep_shared)
        hwr_billboards_init(&scene->billboards);
    /* Directories carried across a transition still describe the same texels,
     * but they have to name the DAS objects that are open now. */
    hwr_billboards_bind(&scene->billboards, map_archive, shared_archive);
    hwr_meshes_bind(&scene->object_meshes, map_archive, shared_archive);
    scene->map_archive = map_archive;
    scene->shared_archive = shared_archive;
    if (hwr_scene_build_mesh(&scene->mesh, world, debug) != 0)
        return hwr_scene_fail(scene, "%s", scene->mesh.error);
    hwr_perf_phase("mesh");
    if (hwr_upload_referenced_textures(scene, world, map_archive,
                                       shared_archive, objects, api, debug,
                                       keep_shared, keep_map) != 0)
        return -1;
    hwr_perf_report();
    scene->valid = 1;
    return 0;
}

#define VA_G_VIEW_OFFSET_X       0x90a04u
#define VA_G_VIEW_OFFSET_Y       0x90a06u
#define VA_G_EYE_Z               0x852fau
#define VA_G_CAMERA_SIN          0x85310u
#define VA_G_CAMERA_COS          0x85312u
#define VA_G_FOCAL_X             0x9099cu
#define VA_G_FOCAL_Y             0x90998u
#define VA_G_CENTER_X            0x909a0u
#define VA_G_HORIZON             0x909a4u
#define VA_G_SHADE_ORIGIN_X      0x90990u
#define VA_G_SHADE_ORIGIN_Y      0x90992u
#define VA_G_GLOBAL_LIGHT_BIAS   0x853f6u
#define VA_G_GLOW_PHASE          0x8a355u
#define VA_G_VIEW_PARAMS_A       0x85270u
#define VA_G_PLAYER_LIGHT_BIAS   0x8c1d8u
#define VA_G_SKY_START_ROW       0x909f6u
#define VA_G_SKY_YAW             0x909f8u
#define VA_G_DOUBLE_SCANLINE     0x8a310u
/* The two animation clocks. The render tick counts rendered frames; the frame
 * time scale is how many timer ticks the last frame took, clamped; the
 * animation clock is the raw timer tick copied once per frame. All three stop
 * advancing whenever the game's own time does, so animations pause with it. */
#define VA_G_RENDER_TICK         0x90c0au
#define VA_G_FRAME_TIME_SCALE    0x85324u
#define VA_G_ANIM_CLOCK          0x8532au
/* The view centre row before the pitch shift. The engine stages its horizon
 * as pitch term + this word (render_world.c, the frame-context commit), but
 * anchors the candle origin's jitter at this word alone
 * (renderer.c:13411-13426) -- the pool is screen-fixed while the world
 * pitches under it. The modern cone's fixed centre reads it for the same
 * behaviour. */
#define VA_G_UNPITCHED_CENTER_Y  0x8532cu

static void hwr_read_camera(struct hwr_gl_camera *camera,
                            const struct hwr_world *world,
                            const struct roth_api_v1 *api)
{
    memset(camera, 0, sizeof *camera);
    camera->view_offset_x = (int16_t)api->game_ram->u16(VA_G_VIEW_OFFSET_X);
    camera->view_offset_y = (int16_t)api->game_ram->u16(VA_G_VIEW_OFFSET_Y);
    camera->eye_z = (int16_t)api->game_ram->u16(VA_G_EYE_Z);
    camera->yaw_sin = (int16_t)api->game_ram->u16(VA_G_CAMERA_SIN);
    camera->yaw_cos = (int16_t)api->game_ram->u16(VA_G_CAMERA_COS);
    camera->yaw = api->game_ram->u16(VA_G_SKY_YAW);
    camera->focal_x = api->game_ram->u16(VA_G_FOCAL_X);
    camera->focal_y = api->game_ram->u16(VA_G_FOCAL_Y);
    camera->center_x = (int32_t)api->game_ram->u32(VA_G_CENTER_X);
    camera->horizon = (int32_t)api->game_ram->u32(VA_G_HORIZON);
    camera->cone_center_y =
        (int16_t)api->game_ram->u16(VA_G_UNPITCHED_CENTER_Y);
    camera->shade_origin_x = (int16_t)api->game_ram->u16(VA_G_SHADE_ORIGIN_X);
    camera->shade_origin_y = (int16_t)api->game_ram->u16(VA_G_SHADE_ORIGIN_Y);
    camera->shade_level = (uint8_t)world->luts.shade_level;
    camera->tint_shade_level = (uint8_t)world->luts.tint_shade_level;
    /* The colour a saturated block is filled with is the map's own, loaded from
     * the DAS file right after the palette; the engine's fast path takes its low
     * byte. Left unset this was zero, so every solid fill came out as palette
     * index 0 rather than the map's far-darkness colour. */
    if (hwr_flat_vertex_law_enabled())
        camera->solid_fill_index = (uint8_t)world->luts.fill_index;
    camera->global_light_bias = api->game_ram->u8(VA_G_GLOBAL_LIGHT_BIAS);
    camera->glow_phase = api->game_ram->u8(VA_G_GLOW_PHASE);
    {
        uintptr_t params = (uintptr_t)api->game_ram->u32(VA_G_VIEW_PARAMS_A);
        uint16_t view_bias = params >= 0x1000u
            ? hwr_scene_read_u16((const uint8_t *)params + 0x1c) : 0;
        camera->flat_edge_bias = (uint16_t)(view_bias +
            api->game_ram->u16(VA_G_PLAYER_LIGHT_BIAS));
        /* The engine stages ONE word into both of its edge projectors: the
         * view record's own candle word plus the player-state term (zero in
         * the candle-lit class, minus thirteen outside it). The wall model
         * reads it signed; the flat path keeps its historical unsigned form,
         * whose 16-bit wrap lands the same rows. */
        camera->wall_edge_bias = (int16_t)camera->flat_edge_bias;
        camera->candle_word = (int16_t)view_bias;
    }
    camera->shade_depth_scale = hwr_settings_get()->shade_depth_scale;
}

/* Every wall surface is a DIRECTED face: it is emitted from one sector, with
 * its endpoints in that sector's wall-ring order.  The engine keeps a directed
 * face only when its endpoints project across the screen from A to B, and drops
 * it otherwise, so a wall seen from behind contributes nothing.  The mesh holds
 * both sectors' faces for a shared wall, and it holds a facade (a portal upper
 * or lower) that only the emitting sector should see, so without this test the
 * renderer paints surfaces the engine never draws -- most visibly a facade
 * painted over the open sky that should show through a portal.
 *
 * Projected A-left-of-B is, for endpoints in front of the eye, the sign of the
 * view-space cross product, and the cross product is unchanged by rotation, so
 * only the eye TRANSLATION is needed here.  Clipping an endpoint to the near
 * plane slides it along the same wall line, which scales the cross product by a
 * non-negative factor, so the untruncated endpoints answer for the clipped ones
 * too.  Faces whose endpoints are both behind the eye are dropped by the
 * frustum and need no test.
 *
 * Note the sign: the accepted face is the one with a NEGATIVE cross product.
 * Were it inverted, every wall of the room the camera stands in would vanish
 * and only the back sides of distant geometry would remain.
 */
static int hwr_wall_faces_camera(const struct hwr_scene_mesh *mesh,
                                 const struct hwr_mesh_surface *surface,
                                 const struct hwr_gl_camera *camera)
{
    const struct hwr_mesh_vertex *vertices = &mesh->vertices[surface->first_vertex];
    const struct hwr_mesh_vertex *end_a = NULL;
    const struct hwr_mesh_vertex *end_b = NULL;
    int32_t ax, ay, bx, by;
    size_t i;
    for (i = 0; i < surface->vertex_count; i++) {
        if ((vertices[i].reserved & HWR_WALL_CORNER_END_B) != 0) {
            if (end_b == NULL)
                end_b = &vertices[i];
        } else if (end_a == NULL) {
            end_a = &vertices[i];
        }
    }
    if (end_a == NULL || end_b == NULL)
        return 0;
    ax = (int16_t)((uint16_t)end_a->x + (uint16_t)camera->view_offset_x);
    ay = (int16_t)((uint16_t)end_a->y + (uint16_t)camera->view_offset_y);
    bx = (int16_t)((uint16_t)end_b->x + (uint16_t)camera->view_offset_x);
    by = (int16_t)((uint16_t)end_b->y + (uint16_t)camera->view_offset_y);
    return (int64_t)ax * by - (int64_t)ay * bx < 0;
}

/* HWR_FACE_CULL=0 narrows the directed-face test back to portal middles, the
 * one class that is emitted from both sides of the same wall.  It is an A/B
 * switch for the test, not a correctness option. */
static int hwr_face_cull_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *setting = getenv("HWR_FACE_CULL");
        enabled = setting != NULL && setting[0] == '0' ? 0 : 1;
    }
    return enabled;
}

enum hwr_mirror_surface_mode {
    HWR_MIRROR_SURFACE_NORMAL = 0,
    HWR_MIRROR_SURFACE_OMIT,
    HWR_MIRROR_SURFACE_SOLID
};

static enum hwr_mirror_surface_mode hwr_mirror_surface_mode(
    const struct hwr_scene *scene, const struct hwr_mesh_surface *surface,
    const struct hwr_gl_camera *camera)
{
    uint16_t logical;
    size_t i;
    int scheduled = 0;

    if (surface->wall_offset == UINT16_MAX || surface->texture_id < 0)
        return HWR_MIRROR_SURFACE_NORMAL;
    logical = (uint16_t)surface->texture_id;
    if (logical >= 0x1600u ||
        (scene->textures[logical].qflags & 0x800u) == 0)
        return HWR_MIRROR_SURFACE_NORMAL;

    if (!camera->reflected) {
        for (i = 0; i < scene->reflection_count; i++) {
            if (scene->reflections[i].wall_offset == surface->wall_offset) {
                scheduled = 1;
                break;
            }
        }
    }
    if (!scheduled)
        return HWR_MIRROR_SURFACE_SOLID;
    /* A mirror carrying the transparency bit paints its authored overlay after
     * registering the reflected view. A plain mirror paints nothing in the
     * primary view, exposing the reflected pixels already underneath. */
    return (scene->textures[logical].qflags & 0x400u) != 0
         ? HWR_MIRROR_SURFACE_NORMAL : HWR_MIRROR_SURFACE_OMIT;
}

static int hwr_mirror_depth_surface(
    const struct hwr_scene *scene, const struct hwr_mesh_surface *surface,
    const struct hwr_gl_camera *camera)
{
    enum hwr_mirror_surface_mode mode;
    uint16_t logical;

    if (camera->reflected || surface->wall_offset == UINT16_MAX ||
        surface->texture_id < 0)
        return 0;
    logical = (uint16_t)surface->texture_id;
    if (logical >= 0x1600u ||
        (scene->textures[logical].qflags & 0x800u) == 0)
        return 0;
    mode = hwr_mirror_surface_mode(scene, surface, camera);
    /* A scheduled plain mirror is omitted from colour; a scheduled transparent
     * mirror keeps its authored overlay. Both own the same primary-view depth
     * plane, while unscheduled/nested mirrors remain ordinary solid geometry. */
    return mode == HWR_MIRROR_SURFACE_OMIT ||
           (mode == HWR_MIRROR_SURFACE_NORMAL &&
            (scene->textures[logical].qflags & 0xc00u) == 0xc00u);
}

static int hwr_surface_geometry_visible(
    const struct hwr_scene *scene, const struct hwr_mesh_surface *surface,
    const struct hwr_gl_camera *camera)
{
    const struct hwr_scene_mesh *mesh = &scene->mesh;
    int16_t z;
    /* Per-frame visibility gate. When the renderer's own drawn-sector set was
     * read successfully, a surface whose sector that set did not draw is
     * dropped -- including its sky caps -- so a sector stacked over or under the
     * camera's own, but not on the sight path, no longer bleeds through. Inert
     * while pvs_valid is 0. */
    if (!camera->reflected && surface->kind == HWR_SURFACE_DOOR &&
        scene->door_portal_clip_valid &&
        surface->door_index < HWR_DOOR_MAX &&
        scene->door_spans[surface->door_index].count == 0)
        return 0;
    if (scene->pvs_valid) {
        int owner_visible =
            surface->sector_index < scene->visible_count &&
            scene->visible[surface->sector_index];
        int neighbor_visible =
            surface->kind == HWR_SURFACE_DOOR &&
            surface->neighbor_sector_index < scene->visible_count &&
            scene->visible[surface->neighbor_sector_index];
        if (surface->kind == HWR_SURFACE_DOOR &&
            (surface->door_state_flags & 2u) != 0)
            owner_visible = 0;
        if (!owner_visible && !neighbor_visible)
            return 0;
    }
    if (surface->texture_id >= 0 &&
        (uint16_t)surface->texture_id < 0x1600u &&
        scene->textures[(uint16_t)surface->texture_id].skip)
        return 0;
    if ((surface->flags & HWR_SURFACE_FLAT) == 0) {
        /* A sky cap carries no endpoint roles and already rejects the engine's
         * back side inside the sky pass, so it is not tested here. */
        if ((surface->flags & HWR_SURFACE_SKY_CAP) != 0)
            return 1;
        if (surface->kind == HWR_SURFACE_PORTAL_MIDDLE || hwr_face_cull_enabled())
            return hwr_wall_faces_camera(mesh, surface, camera);
        return 1;
    }
    z = mesh->vertices[surface->first_vertex].z;
    return (surface->flags & HWR_SURFACE_FACE_DOWN) != 0
        ? camera->eye_z < z : camera->eye_z > z;
}

static int hwr_surface_visible(const struct hwr_scene *scene,
                               const struct hwr_mesh_surface *surface,
                               const struct hwr_gl_camera *camera)
{
    if (hwr_mirror_surface_mode(scene, surface, camera) ==
        HWR_MIRROR_SURFACE_OMIT)
        return 0;
    return hwr_surface_geometry_visible(scene, surface, camera);
}

static int hwr_flat_transparent(const struct hwr_scene *scene,
                                const struct hwr_mesh_surface *surface);

/* A transparent flat blends through the same table as a transparent wall, so
 * it belongs in the same pass; its transparency is carried by the texture
 * rather than by a surface flag, hence the separate test.  HWR_FLAT_BLEND=0
 * keeps such flats in the opaque pass, where they punch holes but do not
 * blend. */
static int hwr_flat_blend_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *setting = getenv("HWR_FLAT_BLEND");
        enabled = setting != NULL && setting[0] == '0' ? 0 : 1;
    }
    return enabled;
}

/* A scheduled mirror may carry the ordinary block-transparency bit as an
 * authored pane over the reflected image.  Unlike a normal wall, that pane's
 * masking/blending is selected by the texture itself even when the map part is
 * not marked as a masked middle. */
static int hwr_mirror_overlay(const struct hwr_scene *scene,
                              const struct hwr_mesh_surface *surface,
                              const struct hwr_gl_camera *camera)
{
    uint16_t logical = (uint16_t)surface->texture_id;
    return logical < 0x1600u &&
           hwr_mirror_surface_mode(scene, surface, camera) ==
               HWR_MIRROR_SURFACE_NORMAL &&
           (scene->textures[logical].qflags & 0xc00u) == 0xc00u;
}

/* HWR_WALL_STALE=0 restores the previous fully-opaque rendering of one-sided
 * walls whose texture carries the block transparency bit. */
static int hwr_wall_stale_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *setting = getenv("HWR_WALL_STALE");
        enabled = setting != NULL && setting[0] == '0' ? 0 : 1;
    }
    return enabled;
}

/* HWR_PORTAL_BLEND=0 restores the previous fully-opaque rendering of two-sided
 * portal parts whose texture carries the block transparency bit. */
static int hwr_portal_blend_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *setting = getenv("HWR_PORTAL_BLEND");
        enabled = setting != NULL && setting[0] == '0' ? 0 : 1;
    }
    return enabled;
}

/* HWR_SOLID_BLEND=0 restores the previous fully-opaque rendering of
 * palette-colour wall parts authored with the blend flag pair. */
static int hwr_solid_blend_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *setting = getenv("HWR_SOLID_BLEND");
        enabled = setting != NULL && setting[0] == '0' ? 0 : 1;
    }
    return enabled;
}

/* Texture-driven translucency on a part the map itself marks plain.  The span
 * driver takes its blend switch straight off the resolved block's attribute
 * word as it binds the texture, with no reference to the face's own flags, so a
 * portal slab side, step, or opening whose texture carries the transparency bit
 * blends exactly like a marked middle would.  Both blend writers also skip
 * index-0 texels, so such a part is masked as well -- hence one helper feeding
 * both the run classification and the material bits.
 *
 * Scoped to the parts that genuinely have geometry behind them.  A one-sided
 * middle is owned by the stale-mask path (nothing is ever drawn behind it, so
 * its holes and blends read the previous frame instead); flats have their own
 * texture-only test; and a block that resolves to a solid colour rather than an
 * image forces the blend switch off, so it never qualifies. */
static int hwr_texture_blend_part(const struct hwr_scene *scene,
                                  const struct hwr_mesh_surface *surface)
{
    uint16_t logical;
    if (!hwr_portal_blend_enabled())
        return 0;
    if ((surface->flags & (HWR_SURFACE_FLAT | HWR_SURFACE_DIRECT |
                           HWR_SURFACE_MASKED)) != 0)
        return 0;
    switch (surface->kind) {
    case HWR_SURFACE_PORTAL_UPPER:
    case HWR_SURFACE_PORTAL_LOWER:
    case HWR_SURFACE_PORTAL_MIDDLE:
    case HWR_SURFACE_PORTAL_SLAB:
        break;
    default:
        return 0;
    }
    logical = (uint16_t)surface->texture_id;
    if (logical >= 0x1600u || scene->textures[logical].direct)
        return 0;
    return (scene->textures[logical].qflags & 0x400u) != 0;
}

/* A wall part that selects a palette colour instead of a texture.  The span
 * driver's untextured branch reads the blend switch from the low two bits of
 * the face's texture-mapping flags and blends only when BOTH are set; the
 * second bit means horizontal flip on a textured face and is meaningless on a
 * flat colour, so it was reused here as the blend modifier.
 *
 * Two exclusions, both from the driver rather than from taste.  Flats have no
 * untextured blend writer at all -- their driver's untextured fills have no
 * blend variant -- so a solid floor or ceiling never blends.  And a door leaf
 * reaches the driver through a different entry whose mode word forces the low
 * bit set, so its blend condition is not this one; no retail door can satisfy
 * this test anyway, since a leaf's mapping flags carry only the flip bit. */
static int hwr_solid_blend_surface(const struct hwr_mesh_surface *surface)
{
    if (!hwr_solid_blend_enabled())
        return 0;
    if ((surface->flags & HWR_SURFACE_DIRECT) == 0 ||
        (surface->flags & HWR_SURFACE_FLAT) != 0 ||
        surface->kind == HWR_SURFACE_DOOR)
        return 0;
    return (surface->mapping_flags & 3u) == 3u;
}

static int hwr_surface_translucent(struct hwr_scene *scene,
                                   struct hwr_mesh_surface *surface,
                                   const struct hwr_gl_camera *camera)
{
    uint16_t logical;
    uint16_t qflags;
    if (hwr_mirror_surface_mode(scene, surface, camera) ==
        HWR_MIRROR_SURFACE_SOLID)
        return 0;
    if (hwr_flat_blend_enabled() && hwr_flat_transparent(scene, surface))
        return 1;
    if (hwr_mirror_overlay(scene, surface, camera))
        return 1;
    if ((surface->flags & HWR_SURFACE_DIRECT) != 0)
        return hwr_solid_blend_surface(surface);
    if ((surface->flags & HWR_SURFACE_MASKED) == 0)
        return hwr_texture_blend_part(scene, surface);
    logical = (uint16_t)surface->texture_id;
    qflags = logical < 0x1600u ? scene->textures[logical].qflags : 0;
    /* The transparency bit alone decides this.  The block's other high bit is
     * only ever consulted when this one is clear, so a surface carrying both
     * still blends. */
    return (qflags & 0x400u) != 0;
}

/* A textured surface's DAS block carries a transparency bit that the span
 * driver loads into its texture-mode selector as it resolves the block.  For
 * flats that one bit does two jobs: it selects the masking span writer, so
 * palette index 0 punches a hole, and it suppresses the density boost an
 * opaque square-256 flat gets.  Both consequences are derived from this. */
static int hwr_flat_transparent(const struct hwr_scene *scene,
                                const struct hwr_mesh_surface *surface)
{
    uint16_t logical;
    if ((surface->flags & HWR_SURFACE_FLAT) == 0 ||
        (surface->flags & HWR_SURFACE_DIRECT) != 0)
        return 0;
    logical = (uint16_t)surface->texture_id;
    if (logical >= 0x1600u)
        return 0;
    return (scene->textures[logical].qflags & 0x400u) != 0;
}

static float hwr_surface_depth(const struct hwr_mesh_surface *surface,
                               const struct hwr_scene_mesh *mesh,
                               const struct hwr_gl_camera *camera)
{
    const struct hwr_mesh_vertex *vertices = mesh->vertices + surface->first_vertex;
    float x = 0.0f;
    float y = 0.0f;
    float s = (float)camera->yaw_sin / 16384.0f;
    float c = (float)camera->yaw_cos / 16384.0f;
    size_t i;
    for (i = 0; i < surface->vertex_count; i++) {
        int dx = (int16_t)((uint16_t)vertices[i].x +
                           (uint16_t)camera->view_offset_x);
        int dy = (int16_t)((uint16_t)vertices[i].y +
                           (uint16_t)camera->view_offset_y);
        x += (float)dx;
        y += (float)dy;
    }
    x /= (float)surface->vertex_count;
    y /= (float)surface->vertex_count;
    return s * y - c * x;
}

/* Shared view-space depth for the candle shade law.  Returns the vertex's view
 * depth (before the near clamp) and, via view_x_out, its view-space X.  The wall
 * endpoint word and the per-flat-vertex shade row both derive their screen
 * column and 16-bit depth word from these two dot products. */
static double hwr_vertex_view_depth(const struct hwr_mesh_vertex *vertex,
                                    const struct hwr_gl_camera *camera,
                                    double *view_x_out)
{
    int dx = (int16_t)((uint16_t)vertex->x + (uint16_t)camera->view_offset_x);
    int dy = (int16_t)((uint16_t)vertex->y + (uint16_t)camera->view_offset_y);
    double sine = (double)camera->yaw_sin / 16384.0;
    double cosine = (double)camera->yaw_cos / 16384.0;
    if (view_x_out != NULL)
        *view_x_out = sine * (double)dx + cosine * (double)dy;
    return sine * (double)dy - cosine * (double)dx;
}

/* The candle law is stated once, in the engine's own screen window: the radial
 * scales are 0x40000/focal, the origin is a pixel position in that window, and
 * the corner the law is sampled at is where the drawn polygon meets that
 * window's edge.  Rendering the same view into a taller framebuffer rescales
 * all three together and nothing moves; rendering it into a WIDER one does not.
 * The frame's side edges then sit further out in angle, so a corner pushed onto
 * them measures a longer radial arm than any corner the engine can produce --
 * proportionally to the aspect change, several shade rows at ultrawide, enough
 * to saturate every corner of a near-player surface and take it to the
 * whole-polygon fill.  Anchoring the corner law to the engine's window (the
 * projection that places the corner, the window the corner is held inside, the
 * origin, and the radial scales, all together) makes it answer the same thing
 * at every target size and shape.
 *
 * Only the CPU corner law needs this.  The shader's per-fragment radial arm
 * evaluates at the fragment's own position, where the window scale divides out
 * of the answer and no boundary enters, so it stays on the scaled terms.  So do
 * the flat texel walk and span state, which are properties of the framebuffer
 * being drawn.
 *
 * HWR_SHADE_SOURCE_ANCHOR=0 restores the target-window terms. */
static int hwr_shade_source_anchor_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *setting = getenv("HWR_SHADE_SOURCE_ANCHOR");
        enabled = setting != NULL && setting[0] == '0' ? 0 : 1;
    }
    return enabled;
}

/* The screen window the candle corner law is evaluated in: a projection, the
 * candle origin, and the pixel rectangle a corner is held inside. */
struct hwr_shade_window {
    uint16_t focal_x;
    uint16_t focal_y;
    int32_t center_x;
    int32_t horizon;
    int16_t origin_x;
    int16_t origin_y;
    uint32_t width;
    uint32_t height;
};

static void hwr_shade_window_init(struct hwr_shade_window *window,
                                  const struct hwr_gl_camera *camera,
                                  uint32_t width, uint32_t height)
{
    if (hwr_shade_source_anchor_enabled() && camera->source_focal_x != 0 &&
        camera->source_focal_y != 0 && camera->source_width != 0 &&
        camera->source_height != 0) {
        window->focal_x = camera->source_focal_x;
        window->focal_y = camera->source_focal_y;
        window->center_x = camera->source_center_x;
        window->horizon = camera->source_horizon;
        window->origin_x = camera->source_shade_origin_x;
        window->origin_y = camera->source_shade_origin_y;
        window->width = camera->source_width;
        window->height = camera->source_height;
        return;
    }
    window->focal_x = camera->focal_x;
    window->focal_y = camera->focal_y;
    window->center_x = camera->center_x;
    window->horizon = camera->horizon;
    window->origin_x = camera->shade_origin_x;
    window->origin_y = camera->shade_origin_y;
    window->width = width;
    window->height = height;
}

/* Holds a projected corner inside the window the law is defined in. Inert on
 * every corner the engine could itself produce; in the extra view area a wider
 * target opens up it continues the shade at the value the engine's own frame-
 * edge corner carried, which is bounded where an unclamped arm is not. */
static void hwr_shade_window_clamp(const struct hwr_shade_window *window,
                                   int *screen_x, int *screen_y)
{
    if (*screen_x < 0)
        *screen_x = 0;
    else if (window->width != 0 && *screen_x >= (int)window->width)
        *screen_x = (int)window->width - 1;
    if (*screen_y < 0)
        *screen_y = 0;
    else if (window->height != 0 && *screen_y >= (int)window->height)
        *screen_y = (int)window->height - 1;
}

/* The candle radial edge term shared by walls and flats: the Chebyshev distance
 * from the jittered shade origin (0x90990/0x90992, the flickering light
 * position) to the projected point, in the engine's 16-bit shade domain, plus
 * the view-depth word.  screen_x/screen_y are the projected pixel coordinates
 * inside the window; depth is the clamped view-depth word. */
static uint16_t hwr_shade_edge_term(int screen_x, int screen_y,
                                    const struct hwr_shade_window *window,
                                    int depth)
{
    uint16_t scale_x = window->focal_x != 0
        ? (uint16_t)(0x40000u / window->focal_x) : 0;
    uint16_t scale_y = window->focal_y != 0
        ? (uint16_t)(0x40000u / window->focal_y) : 0;
    int16_t delta;
    uint16_t magnitude;
    uint16_t distance_x;
    uint16_t distance_y;
    uint16_t projected;

    delta = (int16_t)((uint16_t)screen_x - (uint16_t)window->origin_x);
    magnitude = (uint16_t)(delta < 0 ? -(int)delta : (int)delta);
    distance_x = (uint16_t)(((uint32_t)magnitude * scale_x) >> 8);
    delta = (int16_t)((uint16_t)screen_y - (uint16_t)window->origin_y);
    magnitude = (uint16_t)(delta < 0 ? -(int)delta : (int)delta);
    distance_y = (uint16_t)(((uint32_t)magnitude * scale_y) >> 8);
    projected = distance_y > distance_x ? distance_y : distance_x;
    return (uint16_t)(projected + (uint16_t)depth);
}

static uint16_t hwr_wall_endpoint_word(const struct hwr_mesh_surface *surface,
                                       const struct hwr_mesh_vertex *vertex,
                                       int16_t bottom_z,
                                       const struct hwr_gl_camera *camera,
                                       uint32_t width, uint32_t height,
                                       int tint_clip_adjust)
{
    double view_x;
    double depth_f = hwr_vertex_view_depth(vertex, camera, &view_x);
    struct hwr_shade_window window;
    int depth;
    int screen_x;
    int screen_y;
    uint16_t projected;
    uint8_t light = surface->light;
    int axv;
    int bias;
    int clip_scale;
    int level = camera->tint_shade_level;
    int shade;

    hwr_shade_window_init(&window, camera, width, height);
    if (depth_f < 16.0)
        depth_f = 16.0;
    depth = (int)(depth_f * (camera->shade_depth_scale > 0.0f
                          ? camera->shade_depth_scale : 1.0f));
    if (depth < 16)
        depth = 16;
    /* A point-blank wall projects both of its endpoints far outside the frame,
     * so the clamp below is what actually decides this word: it is the frame
     * edge's radial arm, and holding it in the engine's window is what keeps a
     * wide target from reading several rows darker there and solid-filling a
     * wall the engine draws textured. */
    screen_x = (int)(view_x * (double)window.focal_x / depth_f) +
               window.center_x;
    screen_y = (int)(((double)camera->eye_z - (double)bottom_z) *
                     (double)window.focal_y / depth_f) + window.horizon;
    hwr_shade_window_clamp(&window, &screen_x, &screen_y);

    projected = hwr_shade_edge_term(screen_x, screen_y, &window, depth);

    if (light != 0)
        light = (uint8_t)(light + camera->global_light_bias);
    /* An effective-light-0 face draws through the engine's unshaded mapper
     * (colormap row 0), so the endpoint word is row 0. */
    if (light == 0)
        return 0x0000u;
    axv = light == 0 ? 0 : (int)light - 128;
    bias = 8 + axv;
    /* The engine wall driver's flags&0x40 block darkens the tinted-wall bias by
     * a per-face amount (effective_bias = bias - adjust); fold it in so the CPU
     * fallback row matches the per-pixel shader. */
    bias -= tint_clip_adjust;
    clip_scale = light == 0 ? 32 : 32 - ((axv + 4 > 0 ? axv + 4 : 0) >> 2);
    shade = (int)(projected >> (5 + level));
    shade = (int16_t)(shade - bias);
    shade = (int16_t)(shade - (int16_t)camera->flat_edge_bias);
    if (shade <= 0)
        return 0x0100u;
    if (shade >= clip_scale)
        shade = clip_scale;
    if (shade >= 31)
        return 0x1fffu;
    return (uint16_t)(((uint16_t)(uint8_t)shade << 8) |
        (uint8_t)((uint8_t)projected << (3 - level)));
}

/* Per-FACE port of the engine wall driver's flags&0x40 depth-bias adjust
 * (renderer.c:4823-4846, VA 0x36c64).  The engine darkens a tinted wall by
 * effective_bias = bias - adjust, where adjust = ((ax + r12) << 4) / ax.
 *
 * Correlated ROTH_SHADELOG tracing (the flags&0x40 read at 0x36c64 paired by
 * screen column with the r12 WRITER at 0x2b029/0x29068) pinned the true
 * semantics on the slot-2 pose:
 *   - ax  (reoffset +0x8) = the mapping-record extent word (record +0x26), the
 *     wall segment's world-space length.  This mod's surface->extent is the same
 *     field (hwr_world.c: wall->extent = hwr_i16(mapping_raw)).
 *   - r12 (reoffset +0x12) = FS16(edge+9) - FS16(edge+0x1d) = f9[thisEdge] -
 *     f9[nextEdge], where f9 is the per-vertex accumulated VIEW-SPACE HORIZONTAL
 *     coordinate.  So r12 = -(view_x[A] - view_x[B]) up to sign = -|dview_x|, the
 *     negated projected horizontal extent of the wall.
 * Hence adj = 16*(extent - |dview_x|)/extent: a face-on wall's whole length is
 * horizontal (|dview_x| == extent -> adj 0), an edge-on receding wall projects to
 * zero width (|dview_x| == 0 -> adj 16), and every pose between is CONTINUOUS.
 * The prior heuristic keyed adj on int16 depth-word equality (bx==dx), a
 * knife-edge that mislabels a near-face-on wall as fully receding (spurious 16-
 * row darkening); this computes the engine's actual continuous quantity.
 * Verified: the exact f9 diff reproduces the engine's adj 28/28 on the traced
 * segments.  The two edge depths bx/dx are still needed for the engine's
 * near-plane extent rescale below (unclamped divA/divB words). */
static int hwr_wall_tint_clip_adjust(const struct hwr_mesh_surface *surface,
                                     const struct hwr_scene_mesh *mesh,
                                     const struct hwr_gl_camera *camera)
{
    const struct hwr_mesh_vertex *verts;
    double sine, cosine, da, db, vxa, vxb, dview;
    int have_a = 0, have_b = 0;
    int16_t ax, bx, dx, r12;
    int adjust = 0, proceed = 1;
    size_t i;

    /* texflags bit 0x10 set -> the composed fill-mode word never receives 0x40,
     * so the engine's wall driver skips this block for the surface. */
    if ((surface->mapping_flags & 0x10u) != 0)
        return 0;

    verts = mesh->vertices + surface->first_vertex;
    sine = (double)camera->yaw_sin / 16384.0;
    cosine = (double)camera->yaw_cos / 16384.0;
    da = db = vxa = vxb = 0.0;
    for (i = 0; i < surface->vertex_count; i++) {
        int vx = (int16_t)((uint16_t)verts[i].x + (uint16_t)camera->view_offset_x);
        int vy = (int16_t)((uint16_t)verts[i].y + (uint16_t)camera->view_offset_y);
        double d = sine * (double)vy - cosine * (double)vx;   /* view depth  */
        double h = sine * (double)vx + cosine * (double)vy;   /* view_x (f9) */
        if ((verts[i].reserved & HWR_WALL_CORNER_END_B) != 0) {
            db = d; vxb = h; have_b = 1;
        } else {
            da = d; vxa = h; have_a = 1;
        }
    }
    if (!have_a || !have_b)
        return 0;

    /* Quantise the depths to the engine's 16-bit signed depth-word domain
     * (divA>>8 / divB>>8); used only by the near-plane extent rescale. */
    da = da > 32767.0 ? 32767.0 : (da < -32768.0 ? -32768.0 : da);
    db = db > 32767.0 ? 32767.0 : (db < -32768.0 ? -32768.0 : db);
    dx = (int16_t)(int)da;   /* edge-A depth (g_wall_proj_y3 + 0x18) */
    bx = (int16_t)(int)db;   /* edge-B depth (g_wall_proj_y3 + 0x10) */

    ax = (int16_t)(uint16_t)surface->extent;   /* record +0x26 = mapping extent */

    /* r12 = -|view_x[A] - view_x[B]|, the engine's f9 difference.  For every real
     * face |dview_x| <= extent, so s = ax + r12 stays in [0, extent] and adj in
     * [0,16]; the final clamp guards the degenerate near-plane residual. */
    dview = vxa - vxb;
    if (dview < 0.0) dview = -dview;
    if (dview > 32767.0) dview = 32767.0;
    r12 = (int16_t)(-(int)(dview + 0.5));

    if ((int16_t)(dx | bx) < 0) {              /* segment touches/straddles near plane */
        int16_t bp = (int16_t)(dx - bx); if (bp < 0) bp = (int16_t)(-bp);
        int16_t base = (bx < 0) ? bx : dx;
        int16_t t = (int16_t)((int16_t)(base - 0x10) + bp);
        if (t <= 0 || bp == 0) proceed = 0;
        else ax = (int16_t)(((int32_t)ax * (int32_t)t) / (int32_t)bp);
    }
    if (proceed) {
        uint16_t cxv = (uint16_t)ax;
        if (ax != 0) {
            int16_t s = (int16_t)(ax + r12);
            if (s > 0) {
                uint16_t e16 = (uint16_t)((uint16_t)s << 4);
                adjust = (int)(uint16_t)(e16 / cxv);
            }
        }
    }
    if (adjust < 0) adjust = 0;
    if (adjust > 31) adjust = 31;
    return adjust;
}

/* The flat plane's own screen row, in whichever window the caller works in: the
 * shade law wants the engine's, the span state wants the framebuffer's. */
static int hwr_flat_project_y(const struct hwr_mesh_vertex *vertex,
                              const struct hwr_gl_camera *camera,
                              uint16_t focal_y, int32_t horizon,
                              uint32_t height)
{
    int32_t dx = (int16_t)((uint16_t)vertex->x +
                           (uint16_t)camera->view_offset_x);
    int32_t dy = (int16_t)((uint16_t)vertex->y +
                           (uint16_t)camera->view_offset_y);
    int32_t depth = (dy * camera->yaw_sin - dx * camera->yaw_cos) >> 6;
    int64_t projected;
    int y;
    if (depth < 0x1000)
        depth = 0x1000;
    projected = (int64_t)(camera->eye_z - vertex->z) *
                ((int64_t)focal_y << 8);
    y = (int)(projected / depth) + horizon;
    if (y < 0)
        return 0;
    if (height != 0 && y >= (int)height)
        return (int)height - 1;
    return y;
}

/* DEFECT-1 fix: report whether a tinted flat's triangle (three consecutive mesh
 * vertices at tri_base) has any corner the engine would CLIP away before shading.
 * The engine evaluates the flat candle row only at the vertices of the near- AND
 * frustum-CLIPPED projected polygon (rasterize_floorceil_polygon receives clipped
 * geometry); the mod triangulates the FULL, unclipped sector loop.  A corner that
 * the engine clips -- behind the near plane, or off the left/right/top/bottom of
 * the viewport -- projects to a screen position that does NOT lie on the visible
 * interior, so hwr_flat_shade_endpoint's row there (and the flickering shade
 * origin's radial to it) does not describe any drawn fragment.  Interpolating
 * that off-window row linearly across the visible interior is exactly the
 * +20..+29 bright-patch tail (the jittering shade origin makes an off-window
 * corner intermittently read row 0 -> a bright wedge that GL bleeds inward).
 * Because the staging buffer is pre-sized per triangle (CPU-clipping the polygon
 * would change the vertex count), the mod instead flags any triangle with a
 * clipped corner so the shader falls back to the per-pixel candle radial (the OFF
 * path) for its surviving fragments -- which IS evaluated on the visible fragment
 * and so is clip-faithful and glitch-free.  Fully-visible triangles keep the
 * engine's per-vertex screen-linear evaluation.  The near threshold is the 16.0
 * view-depth clamp shared by hwr_vertex_view_depth / hwr_flat_project_y (and the
 * GL near plane n=16 in the vertex shader). */
static int hwr_flat_triangle_needs_radial(const struct hwr_scene_mesh *mesh,
                                          const struct hwr_mesh_surface *surface,
                                          const struct hwr_gl_camera *camera,
                                          uint32_t width, uint32_t height,
                                          size_t tri_base)
{
    size_t k;
    for (k = 0; k < 3u && tri_base + k < surface->vertex_count; k++) {
        const struct hwr_mesh_vertex *v =
            &mesh->vertices[surface->first_vertex + tri_base + k];
        double view_x;
        double depth = hwr_vertex_view_depth(v, camera, &view_x);
        int sx, sy;
        if (depth < 16.0)
            return 1;                         /* behind the near plane */
        sx = (int)(view_x * (double)camera->focal_x / depth) + (int)camera->center_x;
        if (width != 0 && (sx < 0 || sx >= (int)width))
            return 1;                         /* off the left/right frustum */
        sy = (int)(((double)camera->eye_z - (double)v->z) *
                   (double)camera->focal_y / depth) + (int)camera->horizon;
        if (height != 0 && (sy < 0 || sy >= (int)height))
            return 1;                         /* off the top/bottom frustum */
    }
    return 0;
}

/* Per-FLAT-VERTEX candle shade row.  The engine draws a corridor floor/ceiling
 * through draw_scaled_sprite_spans (0x39610): rasterize_floorceil_polygon
 * (0x3b1c1) evaluates the shade at each PROJECTED polygon vertex via
 * project_floorceil_edge_texcoord (0x3b724), and sprite_span_setup (0x39bd2)
 * interpolates that row LINEARLY down the polygon edges and across each span --
 * screen-space bilinear, NOT per pixel and NOT perspective-correct.  This
 * mirrors that per-vertex evaluation for the mod's flat mesh corners; the GPU
 * carries the returned row as a noperspective (screen-linear) varying and
 * truncates it per fragment.  The two-lobe pool the engine shows arises because
 * it samples only at sector-corner vertices (none on the corridor's bright
 * centreline) and interpolates -- reproducing that structure is the point.
 *
 * The law is the same edge/depth structure as the wall endpoint word: the screen
 * column and the 16-bit view-depth word come from the shared projection, but the
 * screen row is the flat plane's own projection (hwr_flat_project_y, on the
 * vertex's plane height) and the clamps are the FLAT clamps -- row<=0 -> row 1
 * (the projector's own floor on this branch; the restore switch keeps the old
 * row 0) and row>=0x1f -> 31, with no per-face clip scale.
 *
 * This is the MESH-corner form, kept for the restore switch and for the rare
 * loop the view clip cannot fit; a clipped flat uses the corner form above,
 * which needs no viewport clamps because its corners are already inside. */
static uint16_t hwr_flat_shade_endpoint(const struct hwr_mesh_surface *surface,
                                    const struct hwr_mesh_vertex *vertex,
                                    const struct hwr_gl_camera *camera,
                                    uint32_t width, uint32_t height)
{
    double view_x;
    double depth_f = hwr_vertex_view_depth(vertex, camera, &view_x);
    struct hwr_shade_window window;
    int depth;
    int screen_x;
    int screen_y;
    uint16_t projected;
    uint8_t light = surface->light;
    int axv;
    int bias;
    int level = camera->tint_shade_level;
    int shade;

    hwr_shade_window_init(&window, camera, width, height);
    screen_y = hwr_flat_project_y(vertex, camera, window.focal_y,
                                  window.horizon, window.height);
    if (depth_f < 16.0)
        depth_f = 16.0;
    depth = (int)(depth_f * (camera->shade_depth_scale > 0.0f
                          ? camera->shade_depth_scale : 1.0f));
    if (depth < 16)
        depth = 16;
    screen_x = (int)(view_x * (double)window.focal_x / depth_f) +
               window.center_x;
    hwr_shade_window_clamp(&window, &screen_x, &screen_y);

    projected = hwr_shade_edge_term(screen_x, screen_y, &window, depth);

    if (light != 0)
        light = (uint8_t)(light + camera->global_light_bias);
    /* An effective-light-0 face draws through the engine's unshaded mapper
     * (row 0), matching the shader's top-of-shade_row gate. */
    if (light == 0)
        return 0;
    axv = light == 0 ? 0 : (int)light - 128;
    bias = 8 + axv;
    shade = (int)(projected >> (5 + level));
    shade = (int16_t)(shade - bias);
    shade = (int16_t)(shade - (int16_t)camera->flat_edge_bias);
    if (shade <= 0)
        return hwr_flat_vertex_law_enabled() ? 0x0100u : 0u;
    if (shade >= 31)
        return (uint16_t)(31u << 8);
    /* Compose the engine's 8-bit sub-row fraction the same way the wall corner
     * word does: the pre-shift low bits of (depth+radial) shifted << (3-level).
     * The shader floors the screen-linearly interpolated word (row<<8)|frac per
     * fragment, so a candle band boundary walks its sub-pixel height across the
     * flat instead of snapping to whole shade rows. */
    return (uint16_t)(((uint16_t)(uint8_t)shade << 8) |
        (uint8_t)((uint8_t)projected << (3 - level)));
}

static void hwr_flat_span_state(const struct hwr_scene_mesh *mesh,
                                const struct hwr_mesh_surface *surface,
                                const struct hwr_gl_camera *camera,
                                uint32_t height, int density_boost,
                                uint32_t *guard,
                                uint32_t *guard_step, int32_t *origin_y)
{
    int min_y = INT32_MAX;
    int max_y = INT32_MIN;
    int range;
    uint32_t height_delta;
    /* The extra step of perspective shift belongs to the opaque square-256
     * class only; a transparent flat of the same dimensions keeps the base. */
    uint32_t shift = 4u - surface->scale + (density_boost ? 1u : 0u);
    uint32_t perspective_scale;
    uint32_t product;
    uint32_t product_high;
    uint32_t product_low;
    uint32_t dividend_high;
    uint64_t dividend;
    size_t i;

    for (i = 0; i < surface->vertex_count; i++) {
        /* The span state drives the texel walk on the framebuffer being drawn,
         * so it stays on the target projection whatever window the shade law
         * is anchored to. */
        int y = hwr_flat_project_y(
            &mesh->vertices[surface->first_vertex + i], camera,
            camera->focal_y, camera->horizon, height);
        if (y < min_y)
            min_y = y;
        if (y > max_y)
            max_y = y;
    }
    if (min_y == INT32_MAX) {
        *guard = 0;
        *guard_step = 0;
        *origin_y = camera->horizon;
        return;
    }
    if (max_y > camera->horizon) {
        range = max_y - camera->horizon;
        *origin_y = max_y;
    } else {
        range = camera->horizon - min_y;
        *origin_y = min_y;
    }
    height_delta = (uint32_t)abs((int)camera->eye_z -
        (int)mesh->vertices[surface->first_vertex].z);
    if (range <= 0 || height_delta == 0 || camera->focal_y == 0) {
        *guard = 0;
        *guard_step = 0;
        return;
    }
    perspective_scale = (0x2000u * camera->focal_x / camera->focal_y) >> shift;
    product = perspective_scale * (uint32_t)range;
    product_high = product >> 16;
    product_low = product & 0xffffu;
    dividend_high = (int16_t)product_high < (int32_t)height_delta
        ? product_high : (height_delta - 1u) & 0xffffu;
    dividend = ((uint64_t)dividend_high << 32) |
               ((uint64_t)product_low << 16);
    *guard = (uint32_t)(dividend / height_delta) * 2u;
    *guard_step = *guard / (uint32_t)range;
}

struct hwr_wall_span_state {
    uint32_t numerator;
    uint32_t denominator;
    uint32_t numerator_step;
    int32_t denominator_step;
    uint32_t top_accum;
    uint32_t bottom_accum;
    int32_t top_step;
    int32_t bottom_step;
    int32_t first_x;
    uint32_t along_extent;
    uint32_t vertical_extent;
    uint32_t valid;
    int32_t vertical_origin;
    uint32_t vertical_seeded;
};

static int32_t hwr_shift_right_6(int32_t value)
{
    if (value >= 0)
        return value >> 6;
    return -(int32_t)(((uint32_t)(-value) + 63u) >> 6);
}

static int hwr_project_fixed(int32_t value, int32_t scale,
                             int32_t depth, int32_t center)
{
    int64_t projected = (int64_t)value * scale / depth + center;
    if (projected < -0x3ffe)
        return -0x3ffe;
    if (projected > 0x3ffe)
        return 0x3ffe;
    return (int)projected;
}

static void hwr_wall_view_point(const struct hwr_gl_camera *camera,
                                int16_t x, int16_t y,
                                int32_t *view_x, int32_t *depth)
{
    int32_t dx = (int16_t)((uint16_t)x + (uint16_t)camera->view_offset_x);
    int32_t dy = (int16_t)((uint16_t)y + (uint16_t)camera->view_offset_y);
    *view_x = hwr_shift_right_6(dx * camera->yaw_sin +
                               dy * camera->yaw_cos);
    *depth = hwr_shift_right_6(dy * camera->yaw_sin -
                              dx * camera->yaw_cos);
}

static int32_t hwr_wall_row_step(int left, int right, int divisor)
{
    int16_t difference;
    int32_t numerator;
    if (divisor == 0)
        return 0;
    difference = (int16_t)(right - left);
    numerator = (int32_t)((uint32_t)(uint16_t)difference << 16);
    return numerator / divisor;
}

static int hwr_wall_uses_image_fit(const struct hwr_mesh_surface *surface,
                                   uint16_t texture_width,
                                   uint16_t texture_height)
{
    int middle = surface->kind == HWR_SURFACE_SOLID_MIDDLE ||
                 surface->kind == HWR_SURFACE_PORTAL_MIDDLE;
    return texture_width != 0 && texture_height != 0 &&
           (surface->mapping_flags & HWR_TEX_IMAGE_FIT) != 0 &&
           ((surface->mapping_flags & HWR_TEX_TRANSPARENT) == 0 || middle);
}

static void hwr_wall_span_state(const struct hwr_scene_mesh *mesh,
                                const struct hwr_mesh_surface *surface,
                                const struct hwr_gl_camera *camera,
                                uint32_t viewport_width,
                                uint16_t texture_width,
                                uint16_t texture_height,
                                struct hwr_wall_span_state *state)
{
    const struct hwr_mesh_vertex *vertices =
        &mesh->vertices[surface->first_vertex];
    const struct hwr_mesh_vertex *end_a = NULL;
    const struct hwr_mesh_vertex *end_b = NULL;
    int16_t bottom_a = INT16_MAX, top_a = INT16_MIN;
    int16_t bottom_b = INT16_MAX, top_b = INT16_MIN;
    int32_t view_a, view_b, depth_a_q8, depth_b_q8;
    int depth_a, depth_b;
    int xl, xr, row_top_a, row_top_b, row_bottom_a, row_bottom_b;
    int original_top_a, original_bottom_a;
    int segment_columns, left_clip, right_clip, columns;
    uint32_t reciprocal_a, reciprocal_b;
    uint32_t dividend = 2u << camera->shade_level;
    uint64_t product;
    int fit_texture;
    size_t i;

    memset(state, 0, sizeof *state);
    if (viewport_width == 0 || surface->vertex_count == 0)
        return;
    for (i = 0; i < surface->vertex_count; i++) {
        const struct hwr_mesh_vertex *vertex = &vertices[i];
        if ((vertex->reserved & HWR_WALL_CORNER_END_B) != 0) {
            if (end_b == NULL) end_b = vertex;
            if (vertex->z < bottom_b) bottom_b = vertex->z;
            if (vertex->z > top_b) top_b = vertex->z;
        } else {
            if (end_a == NULL) end_a = vertex;
            if (vertex->z < bottom_a) bottom_a = vertex->z;
            if (vertex->z > top_a) top_a = vertex->z;
        }
    }
    if (end_a == NULL || end_b == NULL)
        return;

    fit_texture = hwr_wall_uses_image_fit(surface, texture_width,
                                          texture_height);
    if (fit_texture) {
        state->along_extent = 2u * texture_height;
        state->vertical_extent = 2u * texture_width;
    } else {
        state->along_extent = surface->extent;
        state->vertical_extent = (uint16_t)(top_a - bottom_a);
        if ((surface->mapping_flags & HWR_TEX_HALF_PIXEL) != 0) {
            /* Halving the transposed wall dimensions doubles the texel rate
             * without changing the face's authored mapping extent. */
            state->along_extent *= 2u;
            state->vertical_extent *= 2u;
        }
    }
    /* The reflected pass reverses screen X after projection. The original
     * integer wall accumulators are directed left-to-right, so their endpoint
     * state would need a full handedness reversal. Reflected walls instead use
     * the existing perspective-interpolated fallback, whose authored endpoint
     * coordinates reverse naturally with the geometry. */
    if (camera->x_flip)
        return;

    hwr_wall_view_point(camera, end_a->x, end_a->y, &view_a, &depth_a_q8);
    hwr_wall_view_point(camera, end_b->x, end_b->y, &view_b, &depth_b_q8);
    if (depth_a_q8 < 0x1000 || depth_b_q8 < 0x1000)
        return;
    depth_a = depth_a_q8 >> 8;
    depth_b = depth_b_q8 >> 8;
    if (depth_a == 0 || depth_b == 0)
        return;
    xl = hwr_project_fixed(view_a, camera->focal_x,
                           depth_a_q8, camera->center_x);
    xr = hwr_project_fixed(view_b, camera->focal_x,
                           depth_b_q8, camera->center_x);
    if (xl >= xr)
        return;
    row_top_a = hwr_project_fixed((camera->eye_z - top_a) * 256,
                                  camera->focal_y, depth_a_q8,
                                  camera->horizon);
    row_top_b = hwr_project_fixed((camera->eye_z - top_b) * 256,
                                  camera->focal_y, depth_b_q8,
                                  camera->horizon);
    row_bottom_a = hwr_project_fixed((camera->eye_z - bottom_a) * 256,
                                     camera->focal_y, depth_a_q8,
                                     camera->horizon);
    row_bottom_b = hwr_project_fixed((camera->eye_z - bottom_b) * 256,
                                     camera->focal_y, depth_b_q8,
                                     camera->horizon);
    state->vertical_seeded =
        row_bottom_a - camera->horizon >= 0 &&
        row_top_a - camera->horizon < 0;
    if (fit_texture) {
        unsigned wall_height = top_a > bottom_a
            ? (unsigned)(top_a - bottom_a) : (unsigned)(top_b - bottom_b);
        uint64_t origin;
        if (wall_height == 0)
            return;
        origin = (uint64_t)(top_a - camera->eye_z) * 32768u *
                 state->vertical_extent / wall_height;
        state->vertical_origin = -(int32_t)origin;
    } else {
        int vertical_scale =
            (surface->mapping_flags & HWR_TEX_HALF_PIXEL) != 0 ? 2 : 1;
        state->vertical_origin =
            (camera->eye_z - top_a) * vertical_scale * 32768;
    }

    segment_columns = xr - xl + 1;
    left_clip = xl < 0 ? -xl : 0;
    right_clip = xr >= (int)viewport_width
        ? xr - ((int)viewport_width - 1) : 0;
    columns = segment_columns - left_clip - right_clip;
    if (columns <= 0)
        return;
    state->first_x = xl + left_clip;

    reciprocal_a = (dividend << 16) / (uint32_t)depth_a;
    reciprocal_b = (dividend << 16) / (uint32_t)depth_b;
    state->denominator_step =
        (int16_t)(uint16_t)(reciprocal_b - reciprocal_a);
    state->denominator = (uint16_t)reciprocal_a * (uint32_t)segment_columns;
    state->numerator_step = (uint16_t)reciprocal_b * state->along_extent;
    if (left_clip != 0) {
        state->denominator +=
            (uint32_t)(state->denominator_step * left_clip);
        product = (uint64_t)state->numerator_step * (uint32_t)left_clip;
        while ((product >> 32) != 0) {
            product >>= 1;
            state->denominator_step >>= 1;
            state->numerator_step >>= 1;
            state->denominator >>= 1;
        }
        state->numerator = (uint32_t)product;
    }

    original_top_a = row_top_a;
    original_bottom_a = row_bottom_a;
    if (left_clip != 0) {
        row_bottom_a -= (row_bottom_a - row_bottom_b) *
                        left_clip / segment_columns;
        row_top_a -= (row_top_a - row_top_b) *
                     left_clip / segment_columns;
    }
    if (right_clip != 0) {
        row_bottom_b -= (row_bottom_b - original_bottom_a) *
                        right_clip / segment_columns;
        row_top_b -= (row_top_b - original_top_a) *
                     right_clip / segment_columns;
    }
    state->top_step = hwr_wall_row_step(row_top_a, row_top_b, columns - 1);
    state->bottom_step = hwr_wall_row_step(row_bottom_a, row_bottom_b,
                                           columns - 1);
    state->top_accum = ((uint32_t)(uint16_t)row_top_a << 16) | 0x7fffu;
    state->bottom_accum = ((uint32_t)(uint16_t)row_bottom_a << 16) | 0x7fffu;
    state->valid = 1;
}

/* ======================================================================
 * DEFECT B / SOLID-FILL: per-frame along-wall subdivision so the candle
 * shade is evaluated at the same anchor points the engine uses (per
 * portal-segment column blocks), and the all-corners-saturated fast path.
 * All arithmetic is in world-space doubles; every along-wall screen-column
 * boundary is an affine function of world (x,y,z) within one triangle, so
 * linear edge interpolation of the signed distance gives the exact crossing.
 * ====================================================================== */

/* A wall band-triangle clipped to a screen-column slab is at most a quad, so a
 * small fixed polygon buffer is enough. */
#define HWR_CLIP_MAX 12

struct hwr_clip_pt {
    double x;
    double y;
    double z;
};

/* Round to nearest int16 without pulling in libm (the mod links neither -lm nor
 * any float runtime symbol). */
static int16_t hwr_round_i16(double v)
{
    return (int16_t)(int)(v >= 0.0 ? v + 0.5 : v - 0.5);
}

/* View-space (view_x, depth) of a world point, matching hwr_vertex_view_depth
 * but on doubles and WITHOUT the 16-bit coordinate wrap (a single triangle
 * never spans a wrap boundary, so the wrap is the identity here). */
static void hwr_view_project(double x, double y,
                             const struct hwr_gl_camera *camera,
                             double *view_x, double *depth)
{
    double sine = (double)camera->yaw_sin / 16384.0;
    double cosine = (double)camera->yaw_cos / 16384.0;
    double dx = x + (double)camera->view_offset_x;
    double dy = y + (double)camera->view_offset_y;
    *view_x = sine * dx + cosine * dy;
    *depth = sine * dy - cosine * dx;
}

/* Sutherland-Hodgman clip of polygon `in` (n points) against one half-plane,
 * given the pre-computed signed distance of every input point.  Keeps the
 * dist>=0 side and inserts the exact edge crossings.  Output <= n+1 points. */
static int hwr_clip_halfplane(const struct hwr_clip_pt *in, int n,
                              const double *dist, struct hwr_clip_pt *out)
{
    int i, m = 0;
    for (i = 0; i < n; i++) {
        int j = (i + 1) % n;
        int in_i = dist[i] >= 0.0;
        int in_j = dist[j] >= 0.0;
        if (in_i)
            out[m++] = in[i];
        if (in_i != in_j) {
            double t = dist[i] / (dist[i] - dist[j]);
            out[m].x = in[i].x + t * (in[j].x - in[i].x);
            out[m].y = in[i].y + t * (in[j].y - in[i].y);
            out[m].z = in[i].z + t * (in[j].z - in[i].z);
            m++;
        }
    }
    return m;
}

/* --- DEFECT B along-wall subdivision --- */
#define HWR_WALLSEG_MAX 8      /* cap on along-screen sub-quads per wall */
#define HWR_WALLSEG_PX 40      /* target screen span per sub-quad (px) */

/* Along-wall parameter t in [0,1] (0 at end_a, 1 at end_b), affine in (x,y).
 * Used only to interpolate the wall's top/bottom edge Z and the along texture
 * coordinate at a sub-vertex; the SPLIT itself is done in screen-X. */
static double hwr_wall_param(double x, double y, double ax, double ay,
                             double dir_x, double dir_y, double inv_len2)
{
    return ((x - ax) * dir_x + (y - ay) * dir_y) * inv_len2;
}

/* depth * (screen_x - bound)  for upper==0, or depth * (bound - screen_x) for
 * upper==1.  Affine in world (x,y) and, since a visible wall's depth is > 0
 * throughout, sign-equal to the screen_x comparison -- so it is a valid clip
 * half-plane for splitting the wall at a screen column. */
static double hwr_wall_screenx_dist(double x, double y,
                                    const struct hwr_gl_camera *camera,
                                    double bound, int upper)
{
    double view_x, depth;
    double sx_term, b_term;
    hwr_view_project(x, y, camera, &view_x, &depth);
    sx_term = view_x * (double)camera->focal_x;
    b_term = (bound - (double)camera->center_x) * depth;
    return upper ? (b_term - sx_term) : (sx_term - b_term);
}

/* Decide how many along-SCREEN sub-quads a tinted wall wants.  Splitting in
 * screen-X (the space the noperspective corner-word varying interpolates in)
 * puts equal-error anchors along the ramp, so the far, screen-compressed end
 * where the hyperbolic depth law bends most gets its own anchors.  Only a
 * receding wall with a meaningful screen span subdivides; a face-on or short
 * wall keeps its single ramp (returns 1). */
static int hwr_wall_segment_count(int xl, int xr, int depth_a, int depth_b)
{
    int span = xr - xl;
    int lo = depth_a < depth_b ? depth_a : depth_b;
    int hi = depth_a < depth_b ? depth_b : depth_a;
    int n;
    if (span < 2 * HWR_WALLSEG_PX || lo <= 0)
        return 1;
    /* A near-face-on wall (little depth variation) already interpolates almost
     * linearly, so only subdivide when the far end is clearly deeper. */
    if ((double)hi < 1.3 * (double)lo)
        return 1;
    n = (span + HWR_WALLSEG_PX / 2) / HWR_WALLSEG_PX;
    if (n < 2)
        n = 2;
    if (n > HWR_WALLSEG_MAX)
        n = HWR_WALLSEG_MAX;
    return n;
}

/* SOLID-FILL exemption.  The engine reaches its counter==4 fast path only inside
 * the corner-projection variant gated on g_world_alt_render_flags 0x8000, AND
 * only when g_world_surface_draw_flags bit 0 is clear (the `& 1` gate,
 * renderer.c:5018).  Two engine mechanisms take a face off that path, and this
 * mirrors both:
 *
 *  1. GLOW / emissive faces.  The per-face shade prologue (renderer.c:9200)
 *     SETS alt_render 0x8000 for a tinted face but CLEARS it again for a glow
 *     face (edge flag 0x40 + the blend gate), so a glow face never enters the
 *     fill variant -- it is drawn through the glow colormap at full emissive
 *     brightness instead, which is why the studyshade stained-glass window is
 *     bright in the engine and must never be solid-filled to the map's dark
 *     fill index.  The mod carries the glow bit at sector granularity
 *     (sector_flags 0x40, the same bit its fragment glow branch keys on), so
 *     drop the fill for any glow-sector face.
 *
 *  2. MASKED transparent middles.  For a non-glow tinted face the `& 1` bit is
 *     the per-part render-flags bit 0 (emit_world_span_record 0x2d130 copies it
 *     into the span mode word; it is loaded per part from the ES record at
 *     renderer.c:9026).  It is set on the masked span writers -- a one-sided
 *     wall's masked middle (a window) or a two-sided wall's masked opening --
 *     and clear on the opaque upper/lower/slab parts, so those always fill.  The
 *     mod already carries that per-part distinction as HWR_SURFACE_MASKED (set
 *     only for the masked middle).  The texture's 0x2000000 qflags bit OVERRIDES
 *     it back onto the fill path (renderer.c:13285: `draw_flags &= ~1`), so a
 *     masked middle whose texture carries it (a tiling transparent grate) still
 *     fills; only a masked middle LACKING it stays exempt.  In the runtime block
 *     the on-disk 16-bit payload flags land in the HIGH word of the 32-bit
 *     qflags read (read_das_block_payload writes the payload to block+0xa), so
 *     runtime 0x2000000 == the mod's staged qflags 0x200 -- the same field
 *     hwr_surface_translucent reads at 0x400. */
static int hwr_fill_face_allowed(const struct hwr_scene *scene,
                                 const struct hwr_mesh_surface *surface)
{
    uint16_t logical;
    if ((surface->sector_flags & 0x40u) != 0)
        return 0;                                   /* glow face: never filled */
    if ((surface->flags & HWR_SURFACE_MASKED) == 0)
        return 1;                                   /* opaque part: always fills */
    logical = (uint16_t)surface->texture_id;
    if (logical < 0x1600u && (scene->textures[logical].qflags & 0x200u) != 0)
        return 1;                                   /* 0x2000000 override: fills */
    return 0;                                       /* masked, no override: exempt */
}

/* DEFECT B: emit a tinted wall as `segments` along-length sub-quads.  Each of
 * the surface's band triangles is clipped to each along-slab [t_k,t_{k+1}] and
 * fan-triangulated; every surviving vertex carries the TOP/BOTTOM wd_project
 * corner words evaluated at ITS along-position, so the shader's screen-linear
 * corner-word ramp becomes piecewise and tracks the hyperbolic law.  The whole-
 * wall span/DDA state in `tmpl` is copied unchanged (the shader keys its texel
 * and row DDAs on the absolute screen column), so texel mapping is untouched.
 * Per slab, when every corner word saturates (== 0x1fff) and fill_solid is set,
 * flag its vertices SOLID_FILL so the shader outputs the map fill index.
 * Returns the emitted vertex count. */
static size_t hwr_emit_wall_segmented(const struct hwr_scene_mesh *mesh,
                                      const struct hwr_mesh_surface *surface,
                                      const struct hwr_gl_camera *camera,
                                      uint32_t width, uint32_t height,
                                      int segments, int wall_tint_adjust,
                                      int half_pixel, double xl, double xr,
                                      double ax, double ay, double bx, double by,
                                      int16_t bottom_a, int16_t top_a,
                                      int16_t bottom_b, int16_t top_b,
                                      float sa, float sb, int fill_solid,
                                      const struct hwr_gl_vertex *tmpl,
                                      struct hwr_gl_vertex *dst)
{
    double dir_x = bx - ax;
    double dir_y = by - ay;
    double len2 = dir_x * dir_x + dir_y * dir_y;
    double inv_len2 = len2 > 0.0 ? 1.0 / len2 : 0.0;
    double vscale = half_pixel ? 2.0 : 1.0;
    size_t out = 0;
    size_t base;
    int seg;
    if (inv_len2 == 0.0)
        return 0;
    for (base = 0; base + 3u <= surface->vertex_count; base += 3u) {
        struct hwr_clip_pt tri[3];
        int k;
        for (k = 0; k < 3; k++) {
            const struct hwr_mesh_vertex *v =
                &mesh->vertices[surface->first_vertex + base + (size_t)k];
            tri[k].x = v->x;
            tri[k].y = v->y;
            tri[k].z = v->z;
        }
        for (seg = 0; seg < segments; seg++) {
            double x0 = xl + (xr - xl) * (double)seg / (double)segments;
            double x1 = xl + (xr - xl) * (double)(seg + 1) / (double)segments;
            struct hwr_clip_pt poly[HWR_CLIP_MAX];
            struct hwr_clip_pt tmp2[HWR_CLIP_MAX];
            double dist[HWR_CLIP_MAX];
            int n = 3, c, m;
            for (c = 0; c < 3; c++)
                poly[c] = tri[c];
            /* clip to screen_x >= x0 (skip on the first slab: xl already bounds
             * the wall, and an exact-boundary re-clip could drop a seam column) */
            if (seg > 0) {
                for (c = 0; c < n; c++)
                    dist[c] = hwr_wall_screenx_dist(poly[c].x, poly[c].y,
                                                    camera, x0, 0);
                m = hwr_clip_halfplane(poly, n, dist, tmp2);
                if (m > HWR_CLIP_MAX)
                    m = HWR_CLIP_MAX;
                memcpy(poly, tmp2, (size_t)m * sizeof poly[0]);
                n = m;
                if (n < 3)
                    continue;
            }
            /* clip to screen_x <= x1 (skip on the last slab: xr already bounds) */
            if (seg + 1 < segments) {
                for (c = 0; c < n; c++)
                    dist[c] = hwr_wall_screenx_dist(poly[c].x, poly[c].y,
                                                    camera, x1, 1);
                m = hwr_clip_halfplane(poly, n, dist, tmp2);
                if (m > HWR_CLIP_MAX)
                    m = HWR_CLIP_MAX;
                memcpy(poly, tmp2, (size_t)m * sizeof poly[0]);
                n = m;
                if (n < 3)
                    continue;
            }
            /* Per-slab SOLID-FILL: the engine's counter==4 fast path fires per
             * portal-clipped column block, so decide it per sub-quad here.  When
             * every corner word this slab emits saturates (== 0x1fff), flag all
             * of its vertices so the shader outputs the fill index directly. */
            size_t seg_first = out;
            int seg_all_sat = 1;
            for (k = 1; k + 1 < n; k++) {
                int corner[3];
                int j;
                corner[0] = 0;
                corner[1] = k;
                corner[2] = k + 1;
                for (j = 0; j < 3; j++) {
                    struct hwr_mesh_vertex mv;
                    struct hwr_gl_vertex *o = &dst[out++];
                    double t = hwr_wall_param(poly[corner[j]].x,
                                              poly[corner[j]].y, ax, ay,
                                              dir_x, dir_y, inv_len2);
                    double bz = (double)bottom_a + ((double)bottom_b - bottom_a) * t;
                    double tz = (double)top_a + ((double)top_b - top_a) * t;
                    int16_t vz = hwr_round_i16(poly[corner[j]].z);
                    mv.x = hwr_round_i16(poly[corner[j]].x);
                    mv.y = hwr_round_i16(poly[corner[j]].y);
                    mv.z = vz;
                    mv.reserved = 0;
                    mv.wall_s = 0.0f;
                    mv.wall_top = 0.0f;
                    *o = *tmpl;
                    o->x = mv.x;
                    o->y = mv.y;
                    o->z = vz;
                    o->wall_shade_endpoint = hwr_wall_endpoint_word(
                        surface, &mv, hwr_round_i16(bz), camera, width, height,
                        wall_tint_adjust);
                    o->wall_shade_endpoint_top = hwr_wall_endpoint_word(
                        surface, &mv, hwr_round_i16(tz), camera, width, height,
                        wall_tint_adjust);
                    if (o->wall_shade_endpoint != 0x1fffu ||
                        o->wall_shade_endpoint_top != 0x1fffu)
                        seg_all_sat = 0;
                    o->wall_s = (float)((double)sa + ((double)sb - sa) * t) *
                                (float)vscale;
                    o->wall_top = (float)(tz - (double)vz) * (float)vscale;
                }
            }
            if (fill_solid && seg_all_sat && out > seg_first) {
                size_t f;
                for (f = seg_first; f < out; f++)
                    dst[f].wall_wrap_flags |= HWR_GL_WALL_SOLID_FILL;
            }
        }
    }
    return out;
}

/* ======================================================================
 * Tinted flats: the engine's corner set
 *
 * The engine shades a floor or ceiling ONLY at the corners of the polygon it
 * rasterises, and that polygon is the sector loop AFTER it has been clipped to
 * the drawn area (rasterize_floorceil_polygon projects the loop, then runs
 * Sutherland-Hodgman against the view bounds and emits a corner wherever the
 * loop leaves them); the interior is a screen-linear interpolation of those
 * corner values.  So a floor the player stands on carries corner samples ON the
 * bottom edge of the frame, at the player's feet, and on the side edges --
 * exactly where the sector's own corners are not.
 *
 * The clip is done here in WORLD space, which is both cheaper and equivalent:
 * the projection of a straight world edge is a straight screen edge, so the
 * crossing found on either side is the same point.  A flat is planar in Z,
 * which makes all five half-spaces (near depth, the two side edges, the two
 * horizontal edges) AFFINE in world (x,y) -- the frame's top and bottom become
 * plain depth bounds -- so one linear clipper handles the lot and the surviving
 * corners keep their world position for the texture mapping, the span state and
 * the surface identities.
 * ====================================================================== */

/* A convex loop gains at most one point per half-space; a concave one can gain
 * a point per crossing, so the working buffer carries real headroom and any run
 * that would still exceed it falls back to the unclipped emit -- which is what
 * that surface had before. The retail maps' longest sector loop is well under
 * the input cap. */
#define HWR_FLAT_LOOP_MAX 48
#define HWR_FLAT_CLIP_MAX 128

enum hwr_flat_plane {
    HWR_FLAT_PLANE_NEAR = 0,
    HWR_FLAT_PLANE_LEFT,
    HWR_FLAT_PLANE_RIGHT,
    HWR_FLAT_PLANE_TOP,
    HWR_FLAT_PLANE_BOTTOM
};

/* The affine world->view terms of one flat surface.  The view transform is a
 * 16-bit add, so the loop's first point is wrapped once and every other point is
 * carried from it by its linear delta: that keeps a loop authored across the
 * coordinate wrap a single un-torn polygon to clip.
 *
 * The projection here is the shade window's, so the clip boundaries and the
 * corner words it produces are both stated in the window the candle law is
 * defined in.  The drawn area is expressed in the same coordinates by mapping
 * the framebuffer's edges through the projection, so anchoring the law does not
 * move a single covered pixel. */
struct hwr_flat_view {
    double sine;
    double cosine;
    double base_x;      /* view-space delta of the anchor point */
    double base_y;
    double anchor_x;    /* the anchor's own (unwrapped) world coordinates */
    double anchor_y;
    double focal_x;
    double focal_y;
    double center_x;
    double horizon;
    /* (eye_z - plane_z) * focal_y: the numerator of the surface's screen row. */
    double plane;
    double depth_scale;
    int x_flip;
    struct hwr_shade_window window;
};

static void hwr_flat_view_point(const struct hwr_flat_view *view,
                                double px, double py,
                                double *view_x, double *depth)
{
    double dx = view->base_x + (px - view->anchor_x);
    double dy = view->base_y + (py - view->anchor_y);
    *view_x = view->sine * dx + view->cosine * dy;
    *depth = view->sine * dy - view->cosine * dx;
}

/* Signed distance to one clip half-space, positive inside.  Every form is
 * multiplied through by the (positive, past the near plane) depth, which is what
 * makes it affine in world (x,y) and the crossing an exact linear interpolation.
 * The near plane is clipped FIRST so the others may assume depth > 0. */
static double hwr_flat_clip_distance(const struct hwr_flat_view *view,
                                     enum hwr_flat_plane plane, double bound,
                                     double px, double py)
{
    double view_x;
    double depth;
    double sx;
    hwr_flat_view_point(view, px, py, &view_x, &depth);
    sx = (view->x_flip ? -view_x : view_x) * view->focal_x;
    switch (plane) {
    case HWR_FLAT_PLANE_NEAR:
        return depth - 16.0;
    case HWR_FLAT_PLANE_LEFT:
        return sx - (bound - view->center_x) * depth;
    case HWR_FLAT_PLANE_RIGHT:
        return (bound - view->center_x) * depth - sx;
    case HWR_FLAT_PLANE_TOP:
        return view->plane - (bound - view->horizon) * depth;
    default:
        return (bound - view->horizon) * depth - view->plane;
    }
}

/* Bounded Sutherland-Hodgman against one half-space: keeps the dist>=0 side and
 * inserts the exact crossings.  Returns -1 rather than writing past `capacity`,
 * which a concave loop crossing the same line repeatedly could otherwise do. */
static int hwr_flat_clip_halfplane(const struct hwr_clip_pt *in, int n,
                                   const double *dist, struct hwr_clip_pt *out,
                                   int capacity)
{
    int i;
    int m = 0;
    for (i = 0; i < n; i++) {
        int j = (i + 1) % n;
        int in_i = dist[i] >= 0.0;
        int in_j = dist[j] >= 0.0;
        if (in_i) {
            if (m >= capacity)
                return -1;
            out[m++] = in[i];
        }
        if (in_i != in_j) {
            double t = dist[i] / (dist[i] - dist[j]);
            if (m >= capacity)
                return -1;
            out[m].x = in[i].x + t * (in[j].x - in[i].x);
            out[m].y = in[i].y + t * (in[j].y - in[i].y);
            out[m].z = 0.0;
            m++;
        }
    }
    return m;
}

/* Clips the loop against the five half-spaces and compacts the coincident
 * points a crossing exactly on a boundary leaves behind (a repeated corner is
 * the same sample twice, and the ear-clip below rejects a loop carrying one).
 * Returns the surviving point count, 0 when the polygon is wholly outside, or
 * -1 when the working buffer would overflow and the caller should fall back. */
static int hwr_flat_clip_loop(const struct hwr_flat_view *view,
                              const struct hwr_point *loop, size_t count,
                              const double *bounds,
                              struct hwr_clip_pt *out)
{
    struct hwr_clip_pt work[HWR_FLAT_CLIP_MAX];
    double dist[HWR_FLAT_CLIP_MAX];
    int n = 0;
    int plane;
    int kept;
    int i;
    if (count < 3u || count > HWR_FLAT_LOOP_MAX)
        return -1;
    for (i = 0; (size_t)i < count; i++) {
        work[n].x = (double)loop[i].x;
        work[n].y = (double)loop[i].y;
        work[n].z = 0.0;
        n++;
    }
    for (plane = 0; plane < 5; plane++) {
        int c;
        int m;
        for (c = 0; c < n; c++)
            dist[c] = hwr_flat_clip_distance(
                view, (enum hwr_flat_plane)plane, bounds[plane],
                work[c].x, work[c].y);
        m = hwr_flat_clip_halfplane(work, n, dist, out, HWR_FLAT_CLIP_MAX);
        if (m < 0)
            return -1;
        if (m < 3)
            return 0;
        memcpy(work, out, (size_t)m * sizeof work[0]);
        n = m;
    }
    kept = 0;
    for (i = 0; i < n; i++) {
        double dx;
        double dy;
        if (kept != 0) {
            dx = work[i].x - out[kept - 1].x;
            dy = work[i].y - out[kept - 1].y;
            if (dx > -1e-6 && dx < 1e-6 && dy > -1e-6 && dy < 1e-6)
                continue;
        }
        out[kept++] = work[i];
    }
    while (kept > 1) {
        double dx = out[0].x - out[kept - 1].x;
        double dy = out[0].y - out[kept - 1].y;
        if (dx > -1e-6 && dx < 1e-6 && dy > -1e-6 && dy < 1e-6)
            kept--;
        else
            break;
    }
    return kept >= 3 ? kept : 0;
}

/* The candle shade word at one clipped corner: the same law as the mesh-corner
 * form, evaluated at a point that now lies ON the drawn surface.  The clamp is
 * back, and it is the one thing that could not be dropped: it is what holds a
 * corner of the extra view area a wider target opens up at the arm the engine's
 * own frame-edge corner carried, instead of letting it run out past anything the
 * engine can produce.  Inside the engine's window it never fires, so the law
 * there is the law.  The bright end returns row 1, the floor the engine's
 * projector uses on this branch (renderer.c: `dx <= 0` -> mode |= 4, return
 * 0x100), never row 0. */
static uint16_t hwr_flat_clipped_shade_word(
    const struct hwr_mesh_surface *surface,
    const struct hwr_gl_camera *camera,
    const struct hwr_flat_view *view, double px, double py)
{
    double view_x;
    double depth_f;
    int depth;
    int screen_x;
    int screen_y;
    uint16_t projected;
    uint8_t light = surface->light;
    int axv;
    int bias;
    int level = camera->tint_shade_level;
    int shade;

    hwr_flat_view_point(view, px, py, &view_x, &depth_f);
    if (depth_f < 16.0)
        depth_f = 16.0;
    depth = (int)(depth_f * view->depth_scale);
    if (depth < 16)
        depth = 16;
    screen_x = (int)(view_x * view->focal_x / depth_f) + (int)view->center_x;
    screen_y = (int)(view->plane / depth_f) + (int)view->horizon;
    hwr_shade_window_clamp(&view->window, &screen_x, &screen_y);
    projected = hwr_shade_edge_term(screen_x, screen_y, &view->window, depth);

    if (light != 0)
        light = (uint8_t)(light + camera->global_light_bias);
    if (light == 0)
        return 0;
    axv = (int)light - 128;
    bias = 8 + axv;
    shade = (int)(projected >> (5 + level));
    shade = (int16_t)(shade - bias);
    shade = (int16_t)(shade - (int16_t)camera->flat_edge_bias);
    if (shade <= 0)
        return 0x0100u;
    if (shade >= 31)
        return (uint16_t)(31u << 8);
    return (uint16_t)(((uint16_t)(uint8_t)shade << 8) |
        (uint8_t)((uint8_t)projected << (3 - level)));
}

/* A fan from an existing corner is the closest triangulation to the engine's
 * scanline scheme, and it is valid whenever the clipped polygon is convex --
 * which the clip makes it, unless the sector loop itself was concave.  Report
 * that so the concave remainder can keep the ear-clip. */
static int hwr_flat_polygon_convex(const struct hwr_clip_pt *poly, int n)
{
    int sign = 0;
    int i;
    for (i = 0; i < n; i++) {
        const struct hwr_clip_pt *a = &poly[i];
        const struct hwr_clip_pt *b = &poly[(i + 1) % n];
        const struct hwr_clip_pt *c = &poly[(i + 2) % n];
        double cross = (b->x - a->x) * (c->y - b->y) -
                       (b->y - a->y) * (c->x - b->x);
        int s = cross > 0.0 ? 1 : (cross < 0.0 ? -1 : 0);
        if (s == 0)
            continue;
        if (sign == 0)
            sign = s;
        else if (sign != s)
            return 0;
    }
    return 1;
}

/* Whole part of a double, rounding toward negative infinity, without pulling in
 * libm (this translation unit links no float runtime symbol). */
static double hwr_floor_double(double v)
{
    double whole = (double)(int32_t)v;
    return whole > v ? whole - 1.0 : whole;
}

/* Emits one tinted flat as its view-clipped polygon.  Every attribute other
 * than the position and the corner shade word is a property of the surface, so
 * it rides in from `tmpl` unchanged; the position carries a sub-unit remainder
 * because a clipped corner sits between authored world units.  Returns the
 * emitted vertex count (0 when the polygon is wholly outside the view), or
 * SIZE_MAX when this surface cannot be clipped and the caller should emit it
 * whole. */
static size_t hwr_emit_flat_clipped(const struct hwr_scene_mesh *mesh,
                                    const struct hwr_mesh_surface *surface,
                                    const struct hwr_gl_camera *camera,
                                    uint32_t width, uint32_t height,
                                    int clip_left, int clip_right, int fill_ok,
                                    const struct hwr_gl_vertex *tmpl,
                                    struct hwr_gl_vertex *dst)
{
    struct hwr_flat_view view;
    struct hwr_clip_pt poly[HWR_FLAT_CLIP_MAX];
    uint16_t fan[(HWR_FLAT_CLIP_MAX - 2) * 3];
    struct hwr_point rounded[HWR_FLAT_CLIP_MAX];
    double bounds[5];
    double fill_bounds[5];
    const struct hwr_point *loop =
        mesh->loop_points + surface->first_loop_point;
    size_t count = surface->loop_point_count;
    int16_t plane_z = mesh->vertices[surface->first_vertex].z;
    double band_lo;
    double band_hi;
    size_t out = 0;
    int n;
    int triangles;
    int k;
    int outside_window = 0;
    int all_saturated = 1;
    int shaded = surface->light != 0;

    if (count < 3u || count > HWR_FLAT_LOOP_MAX || width == 0 || height == 0 ||
        camera->focal_x == 0 || camera->focal_y == 0)
        return SIZE_MAX;

    view.sine = (double)camera->yaw_sin / 16384.0;
    view.cosine = (double)camera->yaw_cos / 16384.0;
    view.anchor_x = (double)loop[0].x;
    view.anchor_y = (double)loop[0].y;
    view.base_x = (double)(int16_t)((uint16_t)(int32_t)loop[0].x +
                                    (uint16_t)camera->view_offset_x);
    view.base_y = (double)(int16_t)((uint16_t)(int32_t)loop[0].y +
                                    (uint16_t)camera->view_offset_y);
    hwr_shade_window_init(&view.window, camera, width, height);
    view.focal_x = (double)view.window.focal_x;
    view.focal_y = (double)view.window.focal_y;
    view.center_x = (double)view.window.center_x;
    view.horizon = (double)view.window.horizon;
    view.plane = ((double)camera->eye_z - (double)plane_z) *
                 (double)view.window.focal_y;
    view.depth_scale = camera->shade_depth_scale > 0.0f
        ? (double)camera->shade_depth_scale : 1.0;
    view.x_flip = camera->x_flip != 0;

    /* The drawn area, expressed in the window the clip works in. Mapping the
     * framebuffer's own edges through the projection keeps coverage exactly
     * where it was -- a corner still lands on the frame edge, it is just
     * measured in engine columns and rows. */
    if (view.window.focal_x == camera->focal_x &&
        view.window.center_x == camera->center_x) {
        band_lo = 0.0;
        band_hi = (double)width;
        bounds[HWR_FLAT_PLANE_TOP] = 0.0;
        bounds[HWR_FLAT_PLANE_BOTTOM] = (double)height;
    } else {
        band_lo = hwr_projection_source_x(camera, 0.0);
        band_hi = hwr_projection_source_x(camera, (double)width);
        bounds[HWR_FLAT_PLANE_TOP] = hwr_projection_source_y(camera, 0.0);
        bounds[HWR_FLAT_PLANE_BOTTOM] =
            hwr_projection_source_y(camera, (double)height);
    }

    /* Where the surface carries a portal column band, that band IS the engine's
     * side bound for this chain entry, so the corners land on the doorway edge
     * rather than the frame edge. A primary view's band is already stated in
     * engine columns; a reflected view's is in framebuffer columns, so each is
     * brought into the clip's own coordinates. */
    if (clip_right > clip_left) {
        double lo = (double)clip_left;
        double hi = (double)clip_right;
        int band_is_source = !camera->reflected;
        int clip_is_source = view.window.focal_x != camera->focal_x ||
                             view.window.center_x != camera->center_x;
        if (band_is_source && !clip_is_source) {
            lo = (double)hwr_projection_map_x_fixed(camera, clip_left) /
                 65536.0;
            hi = (double)hwr_projection_map_x_fixed(camera, clip_right) /
                 65536.0;
        } else if (!band_is_source && clip_is_source) {
            lo = hwr_projection_source_x(camera, lo);
            hi = hwr_projection_source_x(camera, hi);
        }
        if (lo > band_lo)
            band_lo = lo;
        if (hi < band_hi)
            band_hi = hi;
        if (band_lo >= band_hi)
            return 0;
    }
    bounds[HWR_FLAT_PLANE_NEAR] = 0.0;
    bounds[HWR_FLAT_PLANE_LEFT] = band_lo;
    bounds[HWR_FLAT_PLANE_RIGHT] = band_hi;

    /* The whole-polygon fill is the engine's decision about the engine's own
     * corner set, so it is taken on the loop clipped to the engine's window --
     * never on the corners of the extra view area, whose words are the held
     * frame-edge value rather than a sample the engine ever takes. Where the
     * drawn area already sits inside that window the two clips coincide and the
     * emitted corners answer it directly. */
    memcpy(fill_bounds, bounds, sizeof fill_bounds);
    if (bounds[HWR_FLAT_PLANE_LEFT] < 0.0) {
        fill_bounds[HWR_FLAT_PLANE_LEFT] = 0.0;
        outside_window = 1;
    }
    if (bounds[HWR_FLAT_PLANE_RIGHT] > (double)view.window.width) {
        fill_bounds[HWR_FLAT_PLANE_RIGHT] = (double)view.window.width;
        outside_window = 1;
    }
    if (bounds[HWR_FLAT_PLANE_TOP] < 0.0) {
        fill_bounds[HWR_FLAT_PLANE_TOP] = 0.0;
        outside_window = 1;
    }
    if (bounds[HWR_FLAT_PLANE_BOTTOM] > (double)view.window.height) {
        fill_bounds[HWR_FLAT_PLANE_BOTTOM] = (double)view.window.height;
        outside_window = 1;
    }

    n = hwr_flat_clip_loop(&view, loop, count, bounds, poly);
    if (n < 0)
        return SIZE_MAX;
    if (n < 3)
        return 0;
    /* The reserved room is the loop plus one corner per half-space. A concave
     * loop that crossed a boundary repeatedly can beat that; emit it whole
     * rather than overrun. */
    if ((size_t)n > count + 5u)
        return SIZE_MAX;

    triangles = n - 2;
    if (!hwr_flat_polygon_convex(poly, n)) {
        /* A concave sector loop can survive the clip concave, where a fan would
         * paint outside the polygon; ear-clip those. The corner SET is the same
         * either way, which is what the shade law depends on. */
        for (k = 0; k < n; k++) {
            rounded[k].x = (int32_t)(poly[k].x >= 0.0 ? poly[k].x + 0.5
                                                      : poly[k].x - 0.5);
            rounded[k].y = (int32_t)(poly[k].y >= 0.0 ? poly[k].y + 0.5
                                                      : poly[k].y - 0.5);
        }
        if (hwr_triangulate(rounded, (size_t)n, fan) == 0)
            goto emit;
    }
    for (k = 1; k + 1 < n; k++) {
        fan[(k - 1) * 3] = 0;
        fan[(k - 1) * 3 + 1] = (uint16_t)k;
        fan[(k - 1) * 3 + 2] = (uint16_t)(k + 1);
    }

emit:
    for (k = 0; k < triangles * 3; k++) {
        const struct hwr_clip_pt *p = &poly[fan[k]];
        struct hwr_gl_vertex *o = &dst[out++];
        double fx = hwr_floor_double(p->x);
        double fy = hwr_floor_double(p->y);
        *o = *tmpl;
        o->x = (int16_t)(uint16_t)(int32_t)fx;
        o->y = (int16_t)(uint16_t)(int32_t)fy;
        o->z = plane_z;
        o->pos_frac_x = (float)(p->x - fx);
        o->pos_frac_y = (float)(p->y - fy);
        o->wall_shade_endpoint =
            hwr_flat_clipped_shade_word(surface, camera, &view, p->x, p->y);
        if (o->wall_shade_endpoint != (uint16_t)(31u << 8))
            all_saturated = 0;
    }
    /* The engine ORs one mode bit per projected corner and takes its untextured
     * fill only when every corner saturated the ramp (mode == 2 exactly); one
     * unsaturated corner sets the textured bit and the whole polygon keeps its
     * texels.  Where the drawn area reaches past the engine's window, re-take
     * the corner set on that window: the emitted corners out in the extra area
     * carry a held edge value, not a sample the engine makes, and neither
     * forcing nor blocking the fill with one would be its decision. */
    if (fill_ok && shaded && out != 0 && outside_window) {
        struct hwr_clip_pt fill_poly[HWR_FLAT_CLIP_MAX];
        int m = hwr_flat_clip_loop(&view, loop, count, fill_bounds, fill_poly);
        all_saturated = m >= 3;
        for (k = 0; k < m; k++) {
            if (hwr_flat_clipped_shade_word(surface, camera, &view,
                                            fill_poly[k].x, fill_poly[k].y) !=
                (uint16_t)(31u << 8)) {
                all_saturated = 0;
                break;
            }
        }
    }
    if (fill_ok && shaded && all_saturated && out != 0) {
        size_t f;
        for (f = 0; f < out; f++)
            dst[f].wall_wrap_flags |= HWR_GL_WALL_SOLID_FILL;
    }
    return out;
}

/* Does this surface take the per-frame view-clipped flat emit? */
static int hwr_flat_uses_clipped_emit(const struct hwr_mesh_surface *surface)
{
    return hwr_flat_vertex_law_enabled() &&
           (surface->flags & HWR_SURFACE_FLAT) != 0 &&
           (surface->flags & HWR_SURFACE_DIRECT) == 0 &&
           (surface->flags & HWR_SURFACE_SKY_CAP) == 0 &&
           (surface->sector_flags & 2u) != 0 &&
           surface->loop_point_count >= 3u &&
           surface->loop_point_count <= HWR_FLAT_LOOP_MAX;
}

/* Worst-case streamed vertex count for a surface, so hwr_prepare_stream can
 * pre-size the stream buffer before the (possibly expanding) staging pass.
 * Tinted walls expand into along-length sub-quads and tinted flats into their
 * view-clipped loop; everything else is 1:1. */
static size_t hwr_surface_reserve(const struct hwr_mesh_surface *surface)
{
    if ((surface->flags & HWR_SURFACE_SKY_CAP) != 0)
        return surface->vertex_count;
    if ((surface->flags & HWR_SURFACE_FLAT) != 0) {
        size_t clipped;
        if (!hwr_flat_uses_clipped_emit(surface))
            return surface->vertex_count;
        /* Five half-spaces, so at most five corners more than the loop; the
         * clipper refuses anything longer and the surface is emitted whole. */
        clipped = (surface->loop_point_count + 3u) * 3u;
        return clipped > surface->vertex_count ? clipped
                                               : surface->vertex_count;
    }
    if (surface->kind == HWR_SURFACE_DOOR)
        return surface->vertex_count;
    /* Band-triangle vertices sit only at the two wall ends (t==0 or t==1), never
     * strictly inside an along-slab, so a band triangle clipped to one slab is at
     * most a quad -> 2 output triangles = 6 vertices; over HWR_WALLSEG_MAX slabs
     * that is 2*HWR_WALLSEG_MAX per input vertex.  Reserve that so the staging
     * pass can never overrun the buffer. */
    if ((surface->sector_flags & 2u) != 0)
        return surface->vertex_count * (size_t)(2 * HWR_WALLSEG_MAX);
    return surface->vertex_count;
}

static size_t hwr_copy_gpu_surface_raw(struct hwr_scene *scene,
                                      const struct hwr_mesh_surface *surface,
                                      const struct hwr_gl_camera *camera,
                                      uint32_t width, uint32_t height,
                                      int clip_left, int clip_right,
                                      struct hwr_gl_vertex *destination)
{
    uint16_t logical = (uint16_t)surface->texture_id;
    uint16_t texture_width = 0;
    uint16_t texture_height = 0;
    uint8_t flags = 0;
    uint8_t direct_index = (uint8_t)surface->texture_id;
    uint32_t flat_guard = 0;
    uint32_t flat_guard_step = 0;
    int32_t flat_guard_origin_y = camera->horizon;
    int flat_transparent = 0;
    int flat_square256 = 0;
    int wall_stale = 0;
    /* Both decided by the same helpers the run classification uses, so the
     * material bits can never disagree with which pass a surface lands in. */
    int texture_blend = hwr_texture_blend_part(scene, surface);
    int solid_blend = hwr_solid_blend_surface(surface);
    int wall_tint_adjust = 0;
    int mirror_solid =
        hwr_mirror_surface_mode(scene, surface, camera) ==
        HWR_MIRROR_SURFACE_SOLID;
    int mirror_overlay = hwr_mirror_overlay(scene, surface, camera);
    /* SOLID-FILL is allowed unless this is a masked (transparent) part whose
     * texture lacks the 0x2000000 override -- the engine's per-part `& 1`
     * exemption; windows / stained glass are never filled however saturated. */
    int fill_ok = !mirror_overlay && hwr_fill_face_allowed(scene, surface);
    struct hwr_wall_span_state wall_span = {0};
    size_t i;
    /* Sky caps carry only a position; the SKY-material fragment branch derives
     * every texel from gl_FragCoord and the sky uniforms, so skip all texture,
     * wall-span, and flat-span staging for them. */
    if ((surface->flags & HWR_SURFACE_SKY_CAP) != 0) {
        for (i = 0; i < surface->vertex_count; i++) {
            const struct hwr_mesh_vertex *source =
                &scene->mesh.vertices[surface->first_vertex + i];
            struct hwr_gl_vertex *target = &destination[i];
            memset(target, 0, sizeof *target);
            target->x = source->x;
            target->y = source->y;
            target->z = source->z;
            target->material_flags = (uint8_t)(HWR_GL_MATERIAL_SKY |
                ((surface->flags & HWR_SURFACE_SKY_OCCLUDER) != 0
                     ? HWR_GL_MATERIAL_SKY_OCCLUDER : 0u) |
                (surface->aux != 0
                     ? HWR_GL_MATERIAL_SKY_FULL_HEIGHT : 0u));
        }
        return surface->vertex_count;
    }
    if (logical < 0x1600u) {
        texture_width = scene->textures[logical].width;
        texture_height = scene->textures[logical].height;
    }
    if ((surface->flags & HWR_SURFACE_FLAT) != 0) {
        /* The flat driver's density class is joint over both dimensions: a
         * texture qualifies only when it is square and its side is a whole
         * multiple of 256, never on one axis alone. */
        flat_square256 = texture_width != 0 &&
                         texture_width == texture_height &&
                         (texture_width & 0xffu) == 0;
        flat_transparent = hwr_flat_transparent(scene, surface);
    } else if (!mirror_overlay &&
               surface->kind == HWR_SURFACE_SOLID_MIDDLE &&
               (surface->flags & (HWR_SURFACE_MASKED | HWR_SURFACE_DIRECT)) == 0 &&
               logical < 0x1600u && !scene->textures[logical].direct &&
               (scene->textures[logical].qflags & 0x400u) != 0 &&
               hwr_wall_stale_enabled()) {
        /* The span driver keys its masked writer on the texture's own
         * transparency bit for every surface kind, with no face-flag gate.  On
         * a one-sided wall nothing behind is ever traversed, so its holes and
         * high-bit blends read the untouched -- previous-frame -- pixels. */
        wall_stale = 1;
    }
    /* A solid-colour blend writer fills every pixel it covers, so a part on
     * that path must NOT carry the masked writer's index-0 skip.  The
     * suppression is scoped to exactly those parts: an ordinary palette-colour
     * middle keeps whatever it has today, including the handful of retail
     * openings authored with palette index 0. */
    if (((surface->flags & HWR_SURFACE_MASKED) != 0 && !solid_blend) ||
        flat_transparent || mirror_overlay || texture_blend)
        flags |= HWR_GL_MATERIAL_MASKED;
    if ((surface->flags & HWR_SURFACE_FLAT) != 0)
        flags |= HWR_GL_MATERIAL_FLAT;
    if ((surface->flags & HWR_SURFACE_DIRECT) != 0) {
        flags |= HWR_GL_MATERIAL_DIRECT;
    } else if (logical < 0x1600u && scene->textures[logical].direct) {
        flags |= HWR_GL_MATERIAL_DIRECT;
        direct_index = scene->textures[logical].direct_index;
    }
    if (((surface->mapping_flags & 2u) != 0) !=
        (logical < 0x1600u && scene->textures[logical].flip_toggle != 0))
        flags |= HWR_GL_MATERIAL_HFLIP;
    if ((surface->flags & HWR_SURFACE_TRANSLUCENT) != 0 ||
        (flat_transparent && hwr_flat_blend_enabled()))
        flags |= HWR_GL_MATERIAL_TRANSLUCENT;
    if (mirror_solid) {
        /* A mirror that was not admitted to the current top-level list, or any
         * mirror encountered inside a reflected view, falls back to the
         * engine's untextured solid path. */
        flags = HWR_GL_MATERIAL_DIRECT;
        direct_index = 0;
        flat_transparent = 0;
        wall_stale = 0;
        texture_blend = 0;
        solid_blend = 0;
        fill_ok = 0;
    }
    if ((surface->flags & HWR_SURFACE_FLAT) != 0) {
        /* Deliberately on the WHOLE loop, not the clipped one: the span state
         * drives the texel walk, whose mapping is settled work, and the flat
         * projection already clamps its scanline range to the frame. */
        hwr_flat_span_state(&scene->mesh, surface, camera, height,
                            flat_square256 && !flat_transparent,
                            &flat_guard, &flat_guard_step,
                            &flat_guard_origin_y);
    } else if (surface->kind != HWR_SURFACE_DOOR) {
        /* Stock paints fixed-orientation secondary surfaces after all world
         * faces in their owner sector. Publish that sector on ordinary walls so
         * the guarded billboard redraw can reproduce the painter overlap
         * without leaking through a nearer wall from another sector. */
        if (surface->sector_index <= (size_t)UINT32_MAX)
            flat_guard = HWR_WORLD_WALL_TAG |
                         (uint32_t)surface->sector_index;
        hwr_wall_span_state(&scene->mesh, surface, camera, width,
                            texture_width, texture_height, &wall_span);
        wall_tint_adjust =
            hwr_wall_tint_clip_adjust(surface, &scene->mesh, camera);
    }
    /* A tinted flat is emitted as its VIEW-CLIPPED loop, so its corners -- the
     * only places the candle law is sampled -- sit where the engine's are.
     * Everything but the position and that corner word is a property of the
     * surface, so it is staged once here and copied to every emitted corner. */
    if (!mirror_solid && hwr_flat_uses_clipped_emit(surface)) {
        struct hwr_gl_vertex tmpl;
        size_t emitted;
        memset(&tmpl, 0, sizeof tmpl);
        if (logical < 0x1600u) {
            tmpl.texture_offset = scene->textures[logical].offset;
            tmpl.texture_width = texture_width;
            tmpl.texture_height = texture_height;
        }
        tmpl.pan_u = surface->pan_u;
        tmpl.pan_v = surface->pan_v;
        tmpl.aux = surface->aux;
        tmpl.scale = surface->scale;
        tmpl.sector_flags = surface->sector_flags;
        tmpl.light = surface->light;
        tmpl.material_flags = flags;
        tmpl.direct_index = direct_index;
        tmpl.flat_guard = flat_guard;
        tmpl.flat_guard_step = flat_guard_step;
        tmpl.flat_guard_origin_y = flat_guard_origin_y;
        tmpl.flat_mapping_flags = surface->mapping_flags;
        /* A flat has no wall-column state, so that attribute pair carries the
         * two owner identities instead. */
        tmpl.wall_source_numerator = surface->owner_identity;
        tmpl.wall_source_denominator = surface->exception_identity;
        emitted = hwr_emit_flat_clipped(&scene->mesh, surface, camera,
                                        width, height, clip_left, clip_right,
                                        fill_ok, &tmpl, destination);
        if (emitted != SIZE_MAX)
            return emitted;
        /* Fell through: this loop cannot be clipped within the reserved room,
         * so it is emitted whole exactly as it was before. */
    }
    /* Solid-fill: a receding tinted wall is emitted as several along-length
     * sub-quads, each carrying its own wd_project corner words, so the shader's
     * screen-linear ramp re-anchors piecewise.  This subdivides only a face that
     * STRADDLES the saturation boundary -- some corners at 0x1fff and some not --
     * so its far sub-quads can solid-fill while the near ones stay texel-shaded;
     * a face with no saturation stays single-ramp and byte-exact. */
    if ((surface->flags & HWR_SURFACE_FLAT) == 0 &&
        surface->kind != HWR_SURFACE_DOOR &&
        (surface->sector_flags & 2u) != 0 && wall_span.valid &&
        fill_ok &&
        !hwr_wall_uses_image_fit(surface, texture_width, texture_height)) {
        const struct hwr_mesh_vertex *verts =
            &scene->mesh.vertices[surface->first_vertex];
        const struct hwr_mesh_vertex *va = NULL;
        const struct hwr_mesh_vertex *vb = NULL;
        int16_t bottom_a = INT16_MAX, top_a = INT16_MIN;
        int16_t bottom_b = INT16_MAX, top_b = INT16_MIN;
        float sa = 0.0f, sb = 0.0f;
        int half_pixel = (surface->mapping_flags & HWR_TEX_HALF_PIXEL) != 0;
        int32_t view_a, view_b, depth_a_q8, depth_b_q8;
        int xl, xr, depth_a, depth_b, segments;
        size_t j;
        for (j = 0; j < surface->vertex_count; j++) {
            if ((verts[j].reserved & HWR_WALL_CORNER_END_B) != 0) {
                if (vb == NULL) { vb = &verts[j]; sb = verts[j].wall_s; }
                if (verts[j].z < bottom_b) bottom_b = verts[j].z;
                if (verts[j].z > top_b) top_b = verts[j].z;
            } else {
                if (va == NULL) { va = &verts[j]; sa = verts[j].wall_s; }
                if (verts[j].z < bottom_a) bottom_a = verts[j].z;
                if (verts[j].z > top_a) top_a = verts[j].z;
            }
        }
        /* Decide whether to subdivide.  The solid-fill auto-trigger opts in only
         * faces whose corner words straddle the saturation boundary
         * (0 < saturated < 4), the exact receding faces whose far end the engine
         * solid-fills segment by segment. */
        int want_seg = 0;
        if (fill_ok &&
            va != NULL && vb != NULL) {
            int sat =
                (hwr_wall_endpoint_word(surface, va, bottom_a, camera,
                     width, height, wall_tint_adjust) == 0x1fffu) +
                (hwr_wall_endpoint_word(surface, va, top_a, camera,
                     width, height, wall_tint_adjust) == 0x1fffu) +
                (hwr_wall_endpoint_word(surface, vb, bottom_b, camera,
                     width, height, wall_tint_adjust) == 0x1fffu) +
                (hwr_wall_endpoint_word(surface, vb, top_b, camera,
                     width, height, wall_tint_adjust) == 0x1fffu);
            want_seg = (sat > 0 && sat < 4);
        }
        segments = 1;
        if (want_seg && va != NULL && vb != NULL) {
            hwr_wall_view_point(camera, va->x, va->y, &view_a, &depth_a_q8);
            hwr_wall_view_point(camera, vb->x, vb->y, &view_b, &depth_b_q8);
            if (depth_a_q8 >= 0x1000 && depth_b_q8 >= 0x1000) {
                depth_a = depth_a_q8 >> 8;
                depth_b = depth_b_q8 >> 8;
                xl = hwr_project_fixed(view_a, camera->focal_x,
                                       depth_a_q8, camera->center_x);
                xr = hwr_project_fixed(view_b, camera->focal_x,
                                       depth_b_q8, camera->center_x);
                if (xl > xr) { int t = xl; xl = xr; xr = t; }
                segments = hwr_wall_segment_count(xl, xr, depth_a, depth_b);
            }
        }
        if (segments >= 2) {
            struct hwr_gl_vertex tmpl;
            memset(&tmpl, 0, sizeof tmpl);
            if (logical < 0x1600u) {
                tmpl.texture_offset = scene->textures[logical].offset;
                tmpl.texture_width = texture_width;
                tmpl.texture_height = texture_height;
            }
            tmpl.pan_u = (surface->mapping_flags & HWR_TEX_IMAGE_FIT) != 0
                ? 0 : surface->pan_u;
            tmpl.pan_v = (surface->mapping_flags & HWR_TEX_IMAGE_FIT) != 0
                ? 0 : surface->pan_v;
            tmpl.aux = surface->aux;
            tmpl.scale = surface->scale;
            tmpl.sector_flags = surface->sector_flags;
            tmpl.light = surface->light;
            tmpl.material_flags = flags;
            tmpl.direct_index = direct_index;
            tmpl.wall_tint_adjust = (uint8_t)wall_tint_adjust;
            tmpl.flat_guard = flat_guard;
            tmpl.flat_mapping_flags = surface->mapping_flags;
            if (texture_height != 0 &&
                (texture_height & (uint16_t)(texture_height - 1u)) == 0)
                tmpl.wall_wrap_flags |= HWR_GL_WALL_WRAP_ALONG;
            if (texture_width != 0 &&
                (texture_width & (uint16_t)(texture_width - 1u)) == 0)
                tmpl.wall_wrap_flags |= HWR_GL_WALL_WRAP_VERTICAL;
            if (wall_stale) {
                tmpl.wall_wrap_flags |= HWR_GL_WALL_STALE_MASK;
                /* And the frame says it has one, so the previous frame is kept
                 * in colour as well wherever the picture is a composed one. */
                scene->stale_mask_seen = 1;
            }
            if (solid_blend)
                tmpl.wall_wrap_flags |= HWR_GL_WALL_SOLID_BLEND;
            tmpl.wall_source_numerator = wall_span.numerator;
            tmpl.wall_source_denominator = wall_span.denominator;
            tmpl.wall_source_step = wall_span.numerator_step;
            tmpl.wall_denominator_step = wall_span.denominator_step;
            tmpl.wall_top_accum = wall_span.top_accum;
            tmpl.wall_bottom_accum = wall_span.bottom_accum;
            tmpl.wall_top_step = wall_span.top_step;
            tmpl.wall_bottom_step = wall_span.bottom_step;
            tmpl.wall_first_x = wall_span.first_x;
            tmpl.wall_along_extent = wall_span.along_extent;
            tmpl.wall_vertical_extent = wall_span.vertical_extent;
            tmpl.wall_span_valid = wall_span.valid;
            tmpl.wall_vertical_origin = wall_span.vertical_origin;
            tmpl.wall_vertical_seeded = wall_span.vertical_seeded;
            return hwr_emit_wall_segmented(
                &scene->mesh, surface, camera, width, height, segments,
                wall_tint_adjust, half_pixel, (double)xl, (double)xr,
                va->x, va->y, vb->x, vb->y,
                bottom_a, top_a, bottom_b, top_b, sa, sb,
                fill_ok,
                &tmpl, destination);
        }
    }
    for (i = 0; i < surface->vertex_count; i++) {
        const struct hwr_mesh_vertex *source =
            &scene->mesh.vertices[surface->first_vertex + i];
        struct hwr_gl_vertex *target = &destination[i];
        memset(target, 0, sizeof *target);
        target->x = source->x;
        target->y = source->y;
        target->z = source->z;
        if (surface->kind == HWR_SURFACE_DOOR) {
            float row_max = texture_height != 0
                ? (float)texture_height - (1.0f / 64.0f) : 0.0f;
            float column_max = texture_width != 0
                ? (float)texture_width - (1.0f / 64.0f) : 0.0f;
            target->wall_s = source->wall_s * row_max;
            target->wall_top = source->wall_top * column_max;
            target->flat_mapping_flags = HWR_GL_MAPPING_MESH;
        } else if ((surface->flags & HWR_SURFACE_FLAT) == 0) {
            const struct hwr_mesh_vertex *wall_vertices =
                &scene->mesh.vertices[surface->first_vertex];
            int end_b = (source->reserved & HWR_WALL_CORNER_END_B) != 0;
            int16_t bottom_z = source->z;
            int16_t top_z = source->z;
            int fit_texture = hwr_wall_uses_image_fit(
                surface, texture_width, texture_height);
            int half_pixel =
                (surface->mapping_flags & HWR_TEX_HALF_PIXEL) != 0;
            int wrap_along = texture_height != 0 &&
                (texture_height & (uint16_t)(texture_height - 1u)) == 0;
            int wrap_vertical = texture_width != 0 &&
                (texture_width & (uint16_t)(texture_width - 1u)) == 0;
            size_t j;
            for (j = 0; j < surface->vertex_count; j++) {
                int candidate_end_b =
                    (wall_vertices[j].reserved & HWR_WALL_CORNER_END_B) != 0;
                if (candidate_end_b == end_b) {
                    if (wall_vertices[j].z < bottom_z)
                        bottom_z = wall_vertices[j].z;
                    if (wall_vertices[j].z > top_z)
                        top_z = wall_vertices[j].z;
                }
            }
            target->wall_shade_endpoint = hwr_wall_endpoint_word(
                surface, source, bottom_z, camera, width, height,
                wall_tint_adjust);
            /* Companion TOP-edge corner word: the same wd_project shade taken at
             * the wall's TOP screen row (screen_y from top_z) instead of the
             * bottom.  Both corners of a side share the vertex's x/y, so every
             * vertex of that side carries the identical top/bottom pair; the
             * shader interpolates them bilinearly for the sub-row candle DDA. */
            target->wall_shade_endpoint_top = hwr_wall_endpoint_word(
                surface, source, top_z, camera, width, height,
                wall_tint_adjust);
            target->wall_tint_adjust = (uint8_t)wall_tint_adjust;
            if (fit_texture) {
                target->wall_s = end_b
                    ? (float)wall_span.along_extent : 0.0f;
                target->wall_top = top_z != bottom_z
                    ? (float)wall_span.vertical_extent *
                      (float)(top_z - source->z) /
                      (float)(top_z - bottom_z)
                    : 0.0f;
            } else {
                target->wall_s = source->wall_s *
                    (half_pixel ? 2.0f : 1.0f);
                target->wall_top = (float)(source->wall_top - source->z) *
                    (half_pixel ? 2.0f : 1.0f);
            }
            target->pan_u = (surface->mapping_flags & HWR_TEX_IMAGE_FIT) != 0
                ? 0 : surface->pan_u;
            target->pan_v = (surface->mapping_flags & HWR_TEX_IMAGE_FIT) != 0
                ? 0 : surface->pan_v;
            if (wrap_along)
                target->wall_wrap_flags |= HWR_GL_WALL_WRAP_ALONG;
            if (wrap_vertical)
                target->wall_wrap_flags |= HWR_GL_WALL_WRAP_VERTICAL;
            if (wall_stale) {
                target->wall_wrap_flags |= HWR_GL_WALL_STALE_MASK;
                scene->stale_mask_seen = 1;
            }
            if (solid_blend)
                target->wall_wrap_flags |= HWR_GL_WALL_SOLID_BLEND;
        } else {
            target->wall_s = source->wall_s;
            target->wall_top = source->wall_top;
            target->pan_u = surface->pan_u;
            target->pan_v = surface->pan_v;
            /* Tinted flats carry the engine's per-vertex candle shade word so the
             * shader can interpolate it screen-linearly (noperspective) instead
             * of evaluating a per-pixel radial.  It reuses the wall endpoint
             * varying (unused by flats) in the wall's (row<<8)|frac format, which
             * the shader floors per fragment for sub-row precision.  Untinted
             * flats keep 0 here and take the clamped depth path. */
            if ((surface->sector_flags & 2u) != 0) {
                /* Fraction-bearing per-vertex candle word (row<<8)|frac; the
                 * shader floors it per fragment for sub-row precision. */
                target->wall_shade_endpoint = hwr_flat_shade_endpoint(
                    surface, source, camera, width, height);
                /* Flag every vertex of a clipped-corner triangle so the shader
                 * drops this unfaithful per-vertex row for the per-pixel radial
                 * (mesh vertices come in per-triangle triples). */
                if (hwr_flat_triangle_needs_radial(
                        &scene->mesh, surface, camera, width, height,
                        (i / 3u) * 3u))
                    target->flat_taint = 1u;
            }
        }
        if (logical < 0x1600u) {
            target->texture_offset = scene->textures[logical].offset;
            target->texture_width = texture_width;
            target->texture_height = texture_height;
        }
        target->aux = surface->aux;
        target->scale = surface->scale;
        target->sector_flags = surface->sector_flags;
        target->light = surface->light;
        target->material_flags = flags;
        target->direct_index = direct_index;
        target->flat_guard = flat_guard;
        target->flat_guard_step = flat_guard_step;
        target->flat_guard_origin_y = flat_guard_origin_y;
        if (surface->kind != HWR_SURFACE_DOOR)
            target->flat_mapping_flags = surface->mapping_flags;
        target->wall_source_numerator = wall_span.numerator;
        target->wall_source_denominator = wall_span.denominator;
        target->wall_source_step = wall_span.numerator_step;
        target->wall_denominator_step = wall_span.denominator_step;
        target->wall_top_accum = wall_span.top_accum;
        target->wall_bottom_accum = wall_span.bottom_accum;
        target->wall_top_step = wall_span.top_step;
        target->wall_bottom_step = wall_span.bottom_step;
        target->wall_first_x = wall_span.first_x;
        target->wall_along_extent = wall_span.along_extent;
        target->wall_vertical_extent = wall_span.vertical_extent;
        target->wall_span_valid = wall_span.valid;
        target->wall_vertical_origin = wall_span.vertical_origin;
        target->wall_vertical_seeded = wall_span.vertical_seeded;
        /* A flat has no wall-column state, so that attribute carries the two
         * owner identities instead: the one this flat publishes as the opaque
         * winner, and the one its depth-off redraw has to find already
         * recorded. Both are zero on every flat that is not part of a
         * platform-over-its-own-flat pair, which leaves the shader on its
         * default height-derived identity. */
        if ((surface->flags & HWR_SURFACE_FLAT) != 0) {
            target->wall_source_numerator = surface->owner_identity;
            target->wall_source_denominator = surface->exception_identity;
        }
    }
    /* Whole-face SOLID-FILL: a tinted wall that reaches this single-ramp path
     * (not subdivided) fills only when EVERY one of its corner words saturated
     * (== 0x1fff), i.e. the engine's counter==4 across the whole face.  A face
     * that merely straddles the boundary took the subdivided path above and is
     * filled per sub-quad there; a partially saturated face left here keeps its
     * texel shading, matching the engine (only counter==4 fills). */
    if (fill_ok &&
        (surface->flags & HWR_SURFACE_FLAT) == 0 &&
        surface->kind != HWR_SURFACE_DOOR &&
        (surface->sector_flags & 2u) != 0) {
        int all_sat = 1;
        for (i = 0; i < surface->vertex_count; i++) {
            if (destination[i].wall_shade_endpoint != 0x1fffu ||
                destination[i].wall_shade_endpoint_top != 0x1fffu) {
                all_sat = 0;
                break;
            }
        }
        if (all_sat)
            for (i = 0; i < surface->vertex_count; i++)
                destination[i].wall_wrap_flags |= HWR_GL_WALL_SOLID_FILL;
    }
    return surface->vertex_count;
}

static void hwr_apply_frame_clip(struct hwr_gl_vertex *vertices, size_t count,
                                 const struct hwr_gl_camera *camera,
                                 int left, int right)
{
    int64_t fixed_left;
    int64_t fixed_right;
    size_t i;
    if (right <= left || left < INT16_MIN || right > INT16_MAX)
        return;
    if (camera != NULL && !camera->reflected) {
        fixed_left = hwr_projection_map_x_fixed(camera, left);
        fixed_right = hwr_projection_map_x_fixed(camera, right);
    } else {
        fixed_left = (int64_t)left * 65536;
        fixed_right = (int64_t)right * 65536;
    }
    if (fixed_left < 0)
        fixed_left = 0;
    if (fixed_right > UINT32_MAX)
        fixed_right = UINT32_MAX;
    if (fixed_left >= fixed_right || fixed_left > UINT32_MAX ||
        fixed_right < 0)
        return;
    for (i = 0; i < count; i++) {
        vertices[i].portal_clip_left = (uint32_t)fixed_left;
        vertices[i].portal_clip_right = (uint32_t)fixed_right;
    }
}

/* The direction a world surface faces, in the coordinates the map is authored
 * in: x and y as the map has them, z up.
 *
 * A flat is the easy half -- a floor faces up, a ceiling faces down, and the
 * mesh already records which of the two a surface is.
 *
 * A wall's normal is the perpendicular of its directed segment, and which of the
 * two perpendiculars is decided by the ENGINE'S OWN visibility rule rather than
 * by where the camera happens to be. The original admits a wall face only where
 * its A endpoint projects left of its B endpoint; putting that condition through
 * the view transform, the focal terms and the basis cancel and it reduces to
 * cross(B-A, eye-A) < 0. The vector (dy,-dx) dotted with (eye-A) is exactly the
 * negative of that cross, so (dy,-dx) points at every eye that is allowed to see
 * the face -- which is the interior of the sector that owns it, whether or not
 * anybody is standing there. Turning the normal to face the camera instead would
 * light both sides of every wall in the map and put light through them.
 *
 * A door leaf borrows the same perpendicular but reads it the other way round
 * where its own corner order says so: a leaf is a closed box rather than a
 * boundary with an inside, and the side that matters is the one facing out of
 * it. The bit was settled when the whole leaf was in hand.
 *
 * A sky cap has no direction at all and says so; its caller marks the class. */
static int hwr_surface_normal(const struct hwr_scene_mesh *mesh,
                              const struct hwr_mesh_surface *surface,
                              float *out_x, float *out_y, float *out_z)
{
    const struct hwr_mesh_vertex *vertices;
    const struct hwr_mesh_vertex *a = NULL;
    const struct hwr_mesh_vertex *b = NULL;
    double dx;
    double dy;
    double length;
    size_t i;

    if ((surface->flags & HWR_SURFACE_SKY_CAP) != 0)
        return 0;
    if ((surface->flags & HWR_SURFACE_FLAT) != 0) {
        *out_x = 0.0f;
        *out_y = 0.0f;
        *out_z = (surface->flags & HWR_SURFACE_FACE_DOWN) != 0 ? -1.0f : 1.0f;
        return 1;
    }
    vertices = mesh->vertices + surface->first_vertex;
    for (i = 0; i < surface->vertex_count; i++) {
        if ((vertices[i].reserved & HWR_WALL_CORNER_END_B) != 0) {
            if (b == NULL)
                b = &vertices[i];
        } else if (a == NULL) {
            a = &vertices[i];
        }
    }
    if (a == NULL || b == NULL)
        return 0;
    /* The endpoints are modular map coordinates, so the difference is taken in
     * the same sixteen bits the world is authored in: a segment that spans the
     * wrap is a short segment, not a map-wide one. */
    dx = (double)(int16_t)((uint16_t)b->x - (uint16_t)a->x);
    dy = (double)(int16_t)((uint16_t)b->y - (uint16_t)a->y);
    length = sqrt(dx * dx + dy * dy);
    if (!(length > 0.0))
        return 0;
    if (surface->normal_reversed != 0)
        length = -length;
    *out_x = (float)(dy / length);
    *out_y = (float)(-dx / length);
    *out_z = 0.0f;
    return 1;
}

/* Written onto the emitted range rather than inside each emitter's inner loop:
 * one surface has one normal however many triangles, segments or portal windows
 * it was cut into, and one place to author it is one place to hold to the
 * analytic answer. With the original law in force this does nothing at all and
 * the vertices leaving here are byte for byte the vertices they were. */
static void hwr_apply_surface_normal(const struct hwr_scene_mesh *mesh,
                                     const struct hwr_mesh_surface *surface,
                                     struct hwr_gl_vertex *vertices,
                                     size_t count)
{
    float nx = 0.0f;
    float ny = 0.0f;
    float nz = 0.0f;
    uint8_t packed_x = 0;
    uint8_t packed_y = 0;
    uint8_t class_bits = 0;
    size_t i;

    if (count == 0 || !hwr_gl_shading_normals())
        return;
    if (hwr_surface_normal(mesh, surface, &nx, &ny, &nz))
        hwr_normal_encode(nx, ny, nz, &packed_x, &packed_y);
    else if ((surface->flags & HWR_SURFACE_SKY_CAP) != 0)
        class_bits = HWR_GL_NORMAL_UNLIT;
    for (i = 0; i < count; i++) {
        vertices[i].normal_x = packed_x;
        vertices[i].normal_y = packed_y;
        vertices[i].wall_wrap_flags |= class_bits;
    }
}

static size_t hwr_copy_gpu_surface(struct hwr_scene *scene,
                                   const struct hwr_mesh_surface *surface,
                                   const struct hwr_gl_camera *camera,
                                   uint32_t width, uint32_t height,
                                   int clip_left, int clip_right,
                                   struct hwr_gl_vertex *destination)
{
    size_t count = hwr_copy_gpu_surface_raw(
        scene, surface, camera, width, height, clip_left, clip_right,
        destination);
    hwr_apply_frame_clip(destination, count, camera, clip_left, clip_right);
    hwr_apply_surface_normal(&scene->mesh, surface, destination, count);
    return count;
}

/* Converts one pre-X-flip engine portal interval (inclusive) to the GL
 * framebuffer's half-open interval. */
static int hwr_reflection_frame_window(const struct hwr_gl_camera *camera,
                                       uint32_t width, int source_left,
                                       int source_right,
                                       int *frame_left, int *frame_right)
{
    int64_t left;
    int64_t right;
    if (source_left > source_right || width == 0)
        return 0;
    if (camera->x_flip) {
        left = 2 * (int64_t)camera->center_x - source_right;
        right = 2 * (int64_t)camera->center_x - source_left + 1;
    } else {
        left = source_left;
        right = (int64_t)source_right + 1;
    }
    if (left < 0)
        left = 0;
    if (right > width)
        right = width;
    if (left >= right)
        return 0;
    *frame_left = (int)left;
    *frame_right = (int)right;
    return 1;
}

static void hwr_prepare_reflection_object_windows(
    struct hwr_scene *scene, const struct hwr_objects *objects,
    const struct hwr_gl_camera *camera, uint32_t width)
{
    size_t i;
    if (objects == NULL || scene->spans == NULL ||
        scene->span_count < objects->count)
        return;
    memset(scene->spans, 0, objects->count * sizeof *scene->spans);
    for (i = 0; i < objects->count; i++) {
        const struct hwr_object *object = &objects->items[i];
        struct hwr_sector_xspan *destination = &scene->spans[i];
        const struct hwr_sector_xspan *source;
        unsigned k;
        if (object->sector_index == HWR_OBJECT_NO_SECTOR ||
            object->sector_index >= scene->reflection_span_count)
            continue;
        source = &scene->reflection_spans[object->sector_index];
        if (source->count == 0 || source->count > HWR_SECTOR_CLIP_MAX)
            continue;
        for (k = 0; k < source->count; k++) {
            int left, right;
            if (!hwr_reflection_frame_window(
                    camera, width, source->lo[k], source->hi[k],
                    &left, &right))
                continue;
            destination->lo[destination->count] = (int16_t)left;
            /* Reflected object spans are already framebuffer-space and use a
             * half-open right edge, as the billboard fragment gate expects. */
            destination->hi[destination->count] = (int16_t)right;
            destination->count++;
        }
    }
}

/* `gap` is how far apart two stored windows may be and still be fused into
 * one. Windows gate half-open column ranges, so `gap == 0` fuses exactly the
 * ones that touch or overlap and leaves a store that admits precisely the
 * columns it was given. `gap == 1` also fuses across a single column neither
 * window admits, which widens the store by that column -- harmless where the
 * windows being combined are already a full interval, and the long-standing
 * behaviour of every caller that asks for it. */
static void hwr_scene_span_add_gap(struct hwr_sector_xspan *span,
                                   int16_t lo, int16_t hi, int gap)
{
    unsigned i;
    if (span->count == UINT8_MAX)
        return;
    if (lo > hi) {
        int16_t swap = lo;
        lo = hi;
        hi = swap;
    }
    for (i = 0; i < span->count; i++) {
        if ((int)lo <= (int)span->hi[i] + gap &&
            (int)span->lo[i] <= (int)hi + gap) {
            unsigned j = 0;
            if (lo < span->lo[i])
                span->lo[i] = lo;
            if (hi > span->hi[i])
                span->hi[i] = hi;
            while (j < span->count) {
                if (j != i &&
                    (int)span->lo[i] <= (int)span->hi[j] + gap &&
                    (int)span->lo[j] <= (int)span->hi[i] + gap) {
                    if (span->lo[j] < span->lo[i])
                        span->lo[i] = span->lo[j];
                    if (span->hi[j] > span->hi[i])
                        span->hi[i] = span->hi[j];
                    span->count--;
                    span->lo[j] = span->lo[span->count];
                    span->hi[j] = span->hi[span->count];
                    if (i == span->count)
                        i = j;
                    j = 0;
                    continue;
                }
                j++;
            }
            return;
        }
    }
    if (span->count >= HWR_SECTOR_CLIP_MAX) {
        span->count = UINT8_MAX; /* overflow fails open in the mirror scissor */
        return;
    }
    span->lo[span->count] = lo;
    span->hi[span->count] = hi;
    span->count++;
}

static void hwr_scene_span_add(struct hwr_sector_xspan *span,
                               int16_t lo, int16_t hi)
{
    hwr_scene_span_add_gap(span, lo, hi, 1);
}

static void hwr_scene_add_wing_windows(
    struct hwr_sector_xspan *destination,
    const struct hwr_sector_xspan *source,
    uint32_t source_width)
{
    unsigned i;
    if (destination == NULL || source == NULL ||
        source->count > HWR_SECTOR_CLIP_MAX ||
        source_width > INT16_MAX)
        return;
    /* Side windows are combined with the exact rule: some destinations already
     * carry the original's own centre window for this object or door, and
     * fusing across a column no window admits would hand them a column of the
     * centre the original clipped away. */
    for (i = 0; i < source->count; i++) {
        int left = source->lo[i];
        int right = source->hi[i];
        if (left < 0)
            hwr_scene_span_add_gap(
                destination, (int16_t)left,
                (int16_t)(right < 0 ? right : 0), 0);
        if (right > (int)source_width)
            hwr_scene_span_add_gap(
                destination,
                (int16_t)(left > (int)source_width
                              ? left : (int)source_width),
                (int16_t)right, 0);
    }
}

/* The traversal projects portal endpoints into whole source columns, dividing
 * toward zero, while the geometry those windows clip is projected in floating
 * point. A window edge can therefore land up to one source column inside the
 * opening it describes. In the centre that is harmless -- the interval there is
 * the full one, and depth alone resolves the surfaces -- but a side window is a
 * hard gate, and in that one column the far sector is discarded while the near
 * wall has already ended at the true, fractional edge, so nothing covers it.
 *
 * Dividing toward zero always moves an endpoint TOWARD the view centre, and by
 * less than one column: a bound at or below zero can be short on its left, one
 * at or above zero short on its right. Widening each window on exactly those
 * sides, before it is split into wings, makes the stored side windows a
 * guaranteed superset of the true opening while leaving every edge that faces
 * the centre where the traversal put it. The extra fragments a widened window
 * admits lie behind the near geometry that forms the edge and lose the depth
 * test. The traversal's own result stays exact -- it also serves as the centre
 * visibility oracle -- and so do the boundary cuts the split makes, which are
 * policy rather than projection. HWR_WING_CLIP_DILATE=0 restores the unwidened
 * windows. */
static int hwr_wing_clip_dilate_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *setting = getenv("HWR_WING_CLIP_DILATE");
        enabled = !(setting != NULL && setting[0] == '0');
    }
    return enabled;
}

static void hwr_scene_span_dilate(struct hwr_sector_xspan *span)
{
    unsigned i;
    if (span->count > HWR_SECTOR_CLIP_MAX)
        return;
    for (i = 0; i < span->count; i++) {
        if (span->lo[i] <= 0 && span->lo[i] > INT16_MIN)
            span->lo[i] = (int16_t)(span->lo[i] - 1);
        if (span->hi[i] >= 0 && span->hi[i] < INT16_MAX)
            span->hi[i] = (int16_t)(span->hi[i] + 1);
    }
}

/* Retains only newly exposed side windows from the transcribed traversal, then
 * restores a full central interval for sectors the engine itself drew. This
 * makes widening additive: a disagreement can never replace the original
 * center with a different cull result. */
static void hwr_prepare_wide_sector_windows(
    struct hwr_scene *scene, const struct hwr_world *world,
    const uint8_t *engine_visible, int engine_valid,
    uint32_t source_width)
{
    size_t i;
    int dilate = hwr_wing_clip_dilate_enabled();
    for (i = 0; i < world->sector_count; i++) {
        struct hwr_sector_xspan original = scene->wide_spans[i];
        struct hwr_sector_xspan *destination = &scene->wide_spans[i];
        memset(destination, 0, sizeof *destination);
        if (engine_valid) {
            /* Only where the centre is the engine's own interval: without that
             * anchor these windows gate the whole canvas, and widening them
             * would move a boundary the original decided. */
            if (dilate)
                hwr_scene_span_dilate(&original);
            hwr_scene_add_wing_windows(
                destination, &original, source_width);
            if (engine_visible[i])
                hwr_scene_span_add(
                    destination, 0, (int16_t)source_width);
        } else {
            unsigned k;
            for (k = 0; k < original.count; k++)
                hwr_scene_span_add(
                    destination, original.lo[k], original.hi[k]);
        }
        if (destination->count == 0 && engine_valid &&
            !engine_visible[i])
            scene->visible[i] = 0;
    }
}

static void hwr_prepare_wide_object_windows(
    struct hwr_scene *scene, const struct hwr_objects *objects,
    uint32_t source_width)
{
    size_t i;
    if (objects == NULL || scene->spans == NULL ||
        scene->span_count < objects->count)
        return;
    for (i = 0; i < objects->count; i++) {
        const struct hwr_object *object = &objects->items[i];
        struct hwr_sector_xspan *destination = &scene->spans[i];
        const struct hwr_sector_xspan *source;
        int had_exact = destination->count != 0;
        unsigned k;
        if (object->sector_index == HWR_OBJECT_NO_SECTOR ||
            object->sector_index >= scene->wide_span_count)
            continue;
        source = &scene->wide_spans[object->sector_index];
        hwr_scene_add_wing_windows(destination, source, source_width);
        if (had_exact || source->count > HWR_SECTOR_CLIP_MAX)
            continue;
        for (k = 0; k < source->count; k++) {
            int left = source->lo[k] > 0 ? source->lo[k] : 0;
            int right = source->hi[k] < (int)source_width
                      ? source->hi[k] : (int)source_width;
            if (left < right)
                hwr_scene_span_add(
                    destination, (int16_t)left, (int16_t)right);
        }
    }
}

static void hwr_prepare_reflection_door_windows(
    struct hwr_scene *scene, const struct hwr_world *world)
{
    size_t di;
    memset(scene->door_spans, 0, sizeof scene->door_spans);
    for (di = 0; di < world->door_count && di < HWR_DOOR_MAX; di++) {
        const struct hwr_door *door = &world->doors[di];
        size_t sectors[2];
        size_t si;
        sectors[0] = (door->state_flags & 2u) == 0
                   ? door->owner_sector_index : SIZE_MAX;
        sectors[1] = door->neighbor_sector_index;
        for (si = 0; si < 2u; si++) {
            const struct hwr_sector_xspan *source;
            unsigned wi;
            if (sectors[si] >= scene->reflection_span_count)
                continue;
            source = &scene->reflection_spans[sectors[si]];
            if (source->count == 0 ||
                source->count > HWR_SECTOR_CLIP_MAX)
                continue;
            for (wi = 0; wi < source->count; wi++)
                hwr_scene_span_add(
                    &scene->door_spans[di],
                    source->lo[wi], source->hi[wi]);
        }
    }
}

static void hwr_prepare_wide_door_windows(
    struct hwr_scene *scene, const struct hwr_world *world,
    uint32_t source_width)
{
    size_t di;
    for (di = 0; di < world->door_count && di < HWR_DOOR_MAX; di++) {
        const struct hwr_door *door = &world->doors[di];
        size_t sectors[2];
        size_t si;
        sectors[0] = (door->state_flags & 2u) == 0
                   ? door->owner_sector_index : SIZE_MAX;
        sectors[1] = door->neighbor_sector_index;
        for (si = 0; si < 2u; si++) {
            if (sectors[si] >= scene->wide_span_count)
                continue;
            hwr_scene_add_wing_windows(
                &scene->door_spans[di],
                &scene->wide_spans[sectors[si]], source_width);
        }
    }
}

static const struct hwr_sector_xspan *hwr_surface_windows(
    const struct hwr_scene *scene, const struct hwr_mesh_surface *surface,
    const struct hwr_gl_camera *camera)
{
    const struct hwr_sector_xspan *span;
    if (!camera->reflected) {
        if (scene->wide_clip_valid) {
            if (surface->kind == HWR_SURFACE_DOOR &&
                surface->door_index < HWR_DOOR_MAX) {
                span = &scene->door_spans[surface->door_index];
            } else if (surface->sector_index < scene->wide_span_count) {
                span = &scene->wide_spans[surface->sector_index];
            } else {
                return NULL;
            }
            return span->count != 0 &&
                   span->count <= HWR_SECTOR_CLIP_MAX ? span : NULL;
        }
        if (!scene->door_portal_clip_valid ||
            surface->kind != HWR_SURFACE_DOOR ||
            surface->door_index >= HWR_DOOR_MAX)
            return NULL;
        span = &scene->door_spans[surface->door_index];
        return span->count != 0 && span->count <= HWR_SECTOR_CLIP_MAX
             ? span : NULL;
    }
    if (!scene->reflection_clip_valid)
        return NULL;
    if (surface->kind == HWR_SURFACE_DOOR &&
        surface->door_index < HWR_DOOR_MAX) {
        span = &scene->door_spans[surface->door_index];
        return span->count != 0 && span->count <= HWR_SECTOR_CLIP_MAX
             ? span : NULL;
    }
    if (surface->sector_index >= scene->reflection_span_count)
        return NULL;
    span = &scene->reflection_spans[surface->sector_index];
    /* A poisoned/overflowed set fails open inside the mirror's global scissor. */
    if (span->count == 0 || span->count > HWR_SECTOR_CLIP_MAX)
        return NULL;
    return span;
}

static size_t hwr_surface_window_count(
    const struct hwr_scene *scene, const struct hwr_mesh_surface *surface,
    const struct hwr_gl_camera *camera)
{
    const struct hwr_sector_xspan *span =
        hwr_surface_windows(scene, surface, camera);
    return span != NULL ? span->count : 1u;
}

static size_t hwr_copy_gpu_surface_windows(
    struct hwr_scene *scene, const struct hwr_mesh_surface *surface,
    const struct hwr_gl_camera *camera, uint32_t width, uint32_t height,
    struct hwr_gl_vertex *destination)
{
    const struct hwr_sector_xspan *span =
        hwr_surface_windows(scene, surface, camera);
    size_t emitted = 0;
    unsigned i;
    if (span == NULL)
        return hwr_copy_gpu_surface(scene, surface, camera, width, height,
                                    0, 0, destination);
    for (i = 0; i < span->count; i++) {
        int left, right;
        if (camera->reflected) {
            if (!hwr_reflection_frame_window(camera, width,
                                             span->lo[i], span->hi[i],
                                             &left, &right))
                continue;
        } else {
            left = span->lo[i];
            /* Primary ordered-chain bounds use stock's half-open
             * [left,right) convention in SOURCE projection coordinates.
             * Wide left-wing bounds are deliberately negative; the GPU clip
             * mapper below converts and clamps them in target space. */
            right = span->hi[i];
            if (left >= right)
                continue;
        }
        emitted += hwr_copy_gpu_surface(
            scene, surface, camera, width, height, left, right,
            destination + emitted);
    }
    return emitted;
}

enum hwr_translucent_source {
    HWR_TRANSLUCENT_WORLD,
    HWR_TRANSLUCENT_MESH,
    HWR_TRANSLUCENT_BILLBOARD
};

struct hwr_translucent_item {
    size_t source_index;
    size_t first_vertex;
    size_t vertex_count;
    float depth;
    uint8_t source;
    uint8_t flags;
};

static float hwr_translucent_vertex_depth(
    const struct hwr_gl_vertex *vertices, size_t count,
    const struct hwr_gl_camera *camera)
{
    double x = 0.0;
    double y = 0.0;
    double sine = (double)camera->yaw_sin / 16384.0;
    double cosine = (double)camera->yaw_cos / 16384.0;
    size_t i;
    for (i = 0; i < count; i++) {
        x += (int16_t)((uint16_t)vertices[i].x +
                       (uint16_t)camera->view_offset_x);
        y += (int16_t)((uint16_t)vertices[i].y +
                       (uint16_t)camera->view_offset_y);
    }
    if (count == 0)
        return 0.0f;
    return (float)((sine * y - cosine * x) / (double)count);
}

static void hwr_translucent_insert(struct hwr_translucent_item *items,
                                   size_t *count,
                                   const struct hwr_translucent_item *item)
{
    size_t position = *count;
    /* Strict comparison preserves the existing order of equal-depth runs. */
    while (position > 0 && items[position - 1u].depth < item->depth) {
        items[position] = items[position - 1u];
        position--;
    }
    items[position] = *item;
    (*count)++;
}

static int hwr_prepare_stream(struct hwr_scene *scene,
                              const struct hwr_gl_camera *camera,
                              uint32_t width, uint32_t height,
                              size_t *sky_count, size_t *mirror_depth_count,
                              size_t *opaque_count,
                              size_t *flat_count,
                              size_t *translucent_count,
                              size_t *translucent_run_count,
                              size_t *exception_run_count)
{
    struct hwr_translucent_item *items = NULL;
    size_t visible_vertices = 0;
    size_t sky_vertices = 0;
    size_t translucent_surfaces = 0;
    size_t item_count = 0;
    size_t i;
    size_t sky_cursor = 0;
    size_t opaque_cursor;
    size_t translucent_cursor;
    size_t object_first = 0;
    size_t billboard_first = 0;
    const struct hwr_meshes *object_meshes = &scene->object_meshes;
    const struct hwr_billboards *bb = &scene->billboards;
    size_t object_mesh_opaque = object_meshes->opaque_count;
    size_t object_mesh_translucent = object_meshes->translucent_count;
    size_t object_mesh_runs = object_meshes->run_count;
    size_t bb_opaque = bb->opaque_count;
    size_t bb_flat = bb->flat_count;
    size_t bb_translucent = bb->translucent_count;
    size_t bb_runs = bb->run_count;
    size_t bb_shadow_runs = bb->shadow_run_count <= bb_runs
                          ? bb->shadow_run_count : bb_runs;
    size_t bb_main_runs = bb_runs - bb_shadow_runs;
    size_t total_runs;

    /* The stream packs [sky][opaque][translucent][mirror-depth]. Mirror planes
     * live at the tail so their exact expanded count need not be known before
     * the ordinary cursors are placed; GL submits that tail before opaque
     * geometry while leaving reflected colour untouched. */
    for (i = 0; i < scene->mesh.surface_count; i++) {
        struct hwr_mesh_surface *surface = &scene->mesh.surfaces[i];
        int regular_visible;
        int mirror_depth_visible;
        size_t reserve;
        size_t windows;
        surface->flags &= (uint8_t)~HWR_SURFACE_TRANSLUCENT;
        regular_visible = hwr_surface_visible(scene, surface, camera);
        mirror_depth_visible =
            hwr_mirror_depth_surface(scene, surface, camera) &&
            hwr_surface_geometry_visible(scene, surface, camera);
        if (!regular_visible && !mirror_depth_visible)
            continue;
        if (regular_visible &&
            (surface->flags & HWR_SURFACE_SKY_CAP) != 0) {
            sky_vertices += surface->vertex_count *
                            hwr_surface_window_count(scene, surface, camera);
        } else if (regular_visible &&
                   hwr_surface_translucent(scene, surface, camera)) {
            surface->flags |= HWR_SURFACE_TRANSLUCENT;
            translucent_surfaces++;
        }
        /* Reserve the worst-case expansion: a tinted wall is split into along-
         * length sub-quads (<=2*HWR_WALLSEG_MAX); everything else is 1:1. */
        reserve = hwr_surface_reserve(surface);
        windows = hwr_surface_window_count(scene, surface, camera);
        if (windows != 0 && reserve > SIZE_MAX / windows)
            return hwr_scene_fail(scene,
                                  "reflected surface stream overflow");
        reserve *= windows;
        if (regular_visible) {
            if (reserve > SIZE_MAX - visible_vertices)
                return hwr_scene_fail(scene, "scene vertex count overflow");
            visible_vertices += reserve;
        }
        if (mirror_depth_visible) {
            if (reserve > SIZE_MAX - visible_vertices)
                return hwr_scene_fail(scene, "scene vertex count overflow");
            visible_vertices += reserve;
        }
    }
    visible_vertices += object_mesh_opaque + object_mesh_translucent +
                        bb_opaque + bb_flat + bb_translucent;
    if (visible_vertices > scene->stream_capacity) {
        struct hwr_gl_vertex *vertices = (struct hwr_gl_vertex *)realloc(
            scene->stream_vertices, visible_vertices * sizeof *vertices);
        if (vertices == NULL)
            return hwr_scene_fail(scene, "out of memory streaming scene vertices");
        scene->stream_vertices = vertices;
        scene->stream_capacity = visible_vertices;
    }
    if (translucent_surfaces > SIZE_MAX - object_mesh_runs ||
        translucent_surfaces + object_mesh_runs > SIZE_MAX - bb_runs)
        return hwr_scene_fail(scene, "too many translucent runs");
    total_runs = translucent_surfaces + object_mesh_runs + bb_runs;
    if (total_runs != 0) {
        items = (struct hwr_translucent_item *)malloc(
            (total_runs - bb_shadow_runs) * sizeof *items);
        if (items == NULL && total_runs != bb_shadow_runs)
            return hwr_scene_fail(scene,
                                  "out of memory sorting translucent runs");
    }

    opaque_cursor = sky_vertices;
    scene->slab_run_count = 0;
    for (i = 0; i < scene->mesh.surface_count; i++) {
        struct hwr_mesh_surface *surface = &scene->mesh.surfaces[i];
        if (!hwr_surface_visible(scene, surface, camera))
            continue;
        if ((surface->flags & HWR_SURFACE_SKY_CAP) != 0) {
            sky_cursor += hwr_copy_gpu_surface_windows(
                scene, surface, camera, width, height,
                scene->stream_vertices + sky_cursor);
        } else if ((surface->flags & HWR_SURFACE_TRANSLUCENT) == 0) {
            size_t first = opaque_cursor;
            size_t emitted = hwr_copy_gpu_surface_windows(
                scene, surface, camera, width, height,
                scene->stream_vertices + opaque_cursor);
            opaque_cursor += emitted;
            /* An opaque platform surface over its own flat: keep it on the
             * ordinary depth-tested path -- it still owns everything nearer
             * than the flat -- and record the range so the exception stage can
             * redraw it where that flat won instead. */
            if (surface->exception_identity != 0 && emitted != 0 &&
                scene->slab_run_count < HWR_GL_SLAB_EXCEPTION_MAX &&
                first <= UINT32_MAX && emitted <= UINT32_MAX) {
                struct hwr_gl_exception_run *run =
                    &scene->slab_runs[scene->slab_run_count++];
                run->first = (uint32_t)first;
                run->count = (uint32_t)emitted;
            }
        } else {
            struct hwr_translucent_item item;
            memset(&item, 0, sizeof item);
            item.source_index = i;
            item.depth = hwr_surface_depth(surface, &scene->mesh, camera);
            item.source = HWR_TRANSLUCENT_WORLD;
            if (surface->exception_identity != 0)
                item.flags = HWR_GL_TRANSLUCENT_RUN_SLAB_EXCEPTION;
            hwr_translucent_insert(items, &item_count, &item);
        }
    }
    /* Stock paints an object's opaque mesh after the sector's world faces.
     * Preserve that order so an object face placed on a floor or ceiling owns
     * the coplanar painter tie. The GL layer supplies the minimal toward-viewer
     * offset needed to make that ownership stable under raster depth. */
    if (object_mesh_opaque != 0) {
        memcpy(scene->stream_vertices + opaque_cursor, object_meshes->opaque,
               object_mesh_opaque * sizeof *scene->stream_vertices);
        opaque_cursor += object_mesh_opaque;
    }
    /* Raw billboards ride the tail of the opaque range; fixed-orientation
     * quads remain last so the GL layer can apply their coplanar depth bias. */
    if (bb_opaque != 0) {
        memcpy(scene->stream_vertices + opaque_cursor, bb->opaque,
               bb_opaque * sizeof *scene->stream_vertices);
        opaque_cursor += bb_opaque;
    }
    if (bb_flat != 0) {
        memcpy(scene->stream_vertices + opaque_cursor, bb->flat,
               bb_flat * sizeof *scene->stream_vertices);
        opaque_cursor += bb_flat;
    }

    /* Merge every ordinary translucent operation in one camera-space order.
     * The mesh and billboard emitters already establish stable order within
     * their own equal-depth groups; insertion keeps those ties unchanged. */
    for (i = 0; i < object_mesh_runs; i++) {
        struct hwr_translucent_item item;
        size_t count = object_meshes->runs[i];
        memset(&item, 0, sizeof item);
        item.first_vertex = object_first;
        item.vertex_count = count;
        item.depth = hwr_translucent_vertex_depth(
            object_meshes->translucent + object_first, count, camera);
        item.source = HWR_TRANSLUCENT_MESH;
        hwr_translucent_insert(items, &item_count, &item);
        object_first += count;
    }
    for (i = 0; i < bb_main_runs; i++) {
        struct hwr_translucent_item item;
        size_t count = bb->runs[i];
        memset(&item, 0, sizeof item);
        item.first_vertex = billboard_first;
        item.vertex_count = count;
        item.depth = hwr_translucent_vertex_depth(
            bb->translucent + billboard_first, count, camera);
        item.source = HWR_TRANSLUCENT_BILLBOARD;
        item.flags = HWR_GL_TRANSLUCENT_RUN_FLOOR_EXCEPTION;
        hwr_translucent_insert(items, &item_count, &item);
        billboard_first += count;
    }

    if (total_runs > scene->translucent_run_capacity) {
        uint32_t *runs = (uint32_t *)realloc(
            scene->translucent_runs, total_runs * sizeof *runs);
        uint8_t *flags;
        if (runs == NULL) {
            free(items);
            return hwr_scene_fail(scene,
                                  "out of memory recording translucent runs");
        }
        scene->translucent_runs = runs;
        flags = (uint8_t *)realloc(scene->translucent_run_flags,
                                  total_runs * sizeof *flags);
        if (flags == NULL) {
            free(items);
            return hwr_scene_fail(scene,
                                  "out of memory recording translucent run flags");
        }
        scene->translucent_run_flags = flags;
        scene->translucent_run_capacity = total_runs;
    }

    translucent_cursor = opaque_cursor;
    for (i = 0; i < item_count; i++) {
        const struct hwr_translucent_item *item = &items[i];
        size_t emitted = item->vertex_count;
        if (item->source == HWR_TRANSLUCENT_WORLD) {
            const struct hwr_mesh_surface *surface =
                &scene->mesh.surfaces[item->source_index];
            emitted = hwr_copy_gpu_surface_windows(
                scene, surface, camera, width, height,
                scene->stream_vertices + translucent_cursor);
        } else {
            const struct hwr_gl_vertex *source =
                item->source == HWR_TRANSLUCENT_MESH
                ? object_meshes->translucent : bb->translucent;
            if (emitted != 0)
                memcpy(scene->stream_vertices + translucent_cursor,
                       source + item->first_vertex,
                       emitted * sizeof *scene->stream_vertices);
        }
        scene->translucent_runs[i] = (uint32_t)emitted;
        scene->translucent_run_flags[i] = item->flags;
        translucent_cursor += emitted;
    }

    /* Ground shadows retain their dedicated tail and are excluded from the
     * ordinary blend loop. */
    for (i = 0; i < bb_shadow_runs; i++) {
        size_t run = bb_main_runs + i;
        size_t count = bb->runs[run];
        if (count != 0)
            memcpy(scene->stream_vertices + translucent_cursor,
                   bb->translucent + billboard_first,
                   count * sizeof *scene->stream_vertices);
        scene->translucent_runs[item_count + i] = (uint32_t)count;
        scene->translucent_run_flags[item_count + i] = 0;
        billboard_first += count;
        translucent_cursor += count;
    }

    {
        size_t mirror_cursor = translucent_cursor;
        for (i = 0; i < scene->mesh.surface_count; i++) {
            const struct hwr_mesh_surface *surface = &scene->mesh.surfaces[i];
            if (!hwr_mirror_depth_surface(scene, surface, camera) ||
                !hwr_surface_geometry_visible(scene, surface, camera))
                continue;
            mirror_cursor += hwr_copy_gpu_surface_windows(
                scene, surface, camera, width, height,
                scene->stream_vertices + mirror_cursor);
        }
        *mirror_depth_count = mirror_cursor - translucent_cursor;
    }
    *translucent_run_count = item_count + bb_shadow_runs;
    *exception_run_count = 0;
    for (i = 0; i < item_count; i++)
        if ((scene->translucent_run_flags[i] &
             (uint8_t)(HWR_GL_TRANSLUCENT_RUN_FLOOR_EXCEPTION |
                       HWR_GL_TRANSLUCENT_RUN_SLAB_EXCEPTION)) != 0)
            (*exception_run_count)++;
    free(items);
    *sky_count = sky_cursor;
    *opaque_count = opaque_cursor - sky_vertices;
    *flat_count = bb_flat;
    *translucent_count = translucent_cursor - opaque_cursor;
    return 0;
}

/* A freshly drawn world carries no held palette effect: the desaturated
 * inventory background belongs to an image the engine has already stopped
 * redrawing, so it can only exist on a held frame. The damage flash is live
 * state and does reach a frame being drawn, so it travels with the palette
 * rather than being applied to it: the true-colour path composes in the
 * untinted colours and lands the flash on the result. */
static void hwr_build_palette(const struct hwr_world *world,
                              uint8_t rgba[1024], uint8_t base_rgba[1024],
                              struct hwr_gl_palette *palette)
{
    hwr_composite_publish_palette(world->luts.palette, NULL,
                                  world->luts.damage_flash_level, rgba);
    hwr_composite_base_rgba(base_rgba);
    memset(palette, 0, sizeof *palette);
    palette->rgba = rgba;
    palette->base_rgba = base_rgba;
    palette->damage_level = world->luts.damage_flash_level;
    palette->grayscale = 0;
}

/* Steps a delta animation's countdown, byte for byte as the engine steps it.
 * The timer is loaded with the block's rate byte and has the frame time scale
 * taken off it once per rendered frame; the subtraction borrows exactly when a
 * frame is due, which is what the low threshold detects, and adding the rate
 * back leaves the borrowed remainder in place so the animation keeps its phase
 * rather than drifting. An overshoot larger than one whole step is dropped
 * instead of caught up, so however long the game stalls the animation still
 * moves only one frame. A rate byte of 0xff steps on every frame. */
static void hwr_animation_step_delta(struct hwr_scene_animation *animation,
                                     uint16_t tick, uint8_t scale)
{
    uint8_t step;
    if (!animation->started) {
        /* A block starts with no frame chosen: the first frame it is drawn on
         * shows frame zero and only arms the timer. */
        animation->started = 1;
        animation->frame_index = 0;
        animation->timer = animation->rate;
        animation->last_tick = tick;
        return;
    }
    if (animation->last_tick == tick)
        return;
    animation->last_tick = tick;
    if (animation->rate != 0xffu) {
        animation->timer = (uint8_t)(animation->timer - scale);
        if (animation->timer < 0x10u)
            return;
        step = (uint8_t)((animation->rate & 0x1fu) + 1u);
        animation->timer = (uint8_t)(animation->timer + step);
        if ((animation->timer & 0x80u) != 0)
            animation->timer = 0;
    }
    animation->frame_index++;
    if (animation->frame_index >= animation->frame_count)
        animation->frame_index = 0;
}

/* Sub-image animations keep no state: the frame is a plain function of the
 * global animation clock and the ticks each frame is held for. The count they
 * cycle through is the one the block's frame chain actually yields, not the
 * larger number its first header claims -- the game corrects that claim in
 * place when it loads the block, and selects frames with the corrected value,
 * so a block's final sub-image is never reached. */
static uint16_t hwr_animation_subimage_frame(const struct hwr_scene_animation *animation,
                                             uint16_t clock)
{
    if (animation->divisor == 0 || animation->frame_count == 0)
        return 0;
    return (uint16_t)((((uint32_t)clock & 0x7fffu) / animation->divisor) %
                      animation->frame_count);
}

/* Advances every animated texture in the scene and pushes the frames that
 * changed into their slots in the texture store. Playback is keyed by texture
 * rather than by surface, so a texture on a hundred walls still steps once,
 * and one the engine culled still keeps time. */
static void hwr_scene_animate(struct hwr_scene *scene,
                              const struct roth_api_v1 *api)
{
    static int enabled = -1;
    uint16_t tick;
    uint16_t clock;
    uint8_t scale;
    size_t i;
    if (enabled < 0) {
        /* On by default. HWR_ANIMATE=0 holds every animated texture on the
         * frame the scene was built with. */
        const char *setting = getenv("HWR_ANIMATE");
        enabled = setting != NULL && setting[0] == '0' ? 0 : 1;
    }
    if (!enabled || scene->animation_count == 0)
        return;
    tick = api->game_ram->u16(VA_G_RENDER_TICK);
    clock = api->game_ram->u16(VA_G_ANIM_CLOCK);
    scale = (uint8_t)api->game_ram->u32(VA_G_FRAME_TIME_SCALE);
    for (i = 0; i < scene->animation_count; i++) {
        struct hwr_scene_animation *animation = &scene->animations[i];
        if (animation->kind == HWR_ANIMATION_SUBIMAGE)
            animation->frame_index = hwr_animation_subimage_frame(animation, clock);
        else
            hwr_animation_step_delta(animation, tick, scale);
        if (animation->frame_index == animation->uploaded_index)
            continue;
        if (hwr_gl_scene_update_texels(animation->offset,
                                       animation->frames +
                                       (size_t)animation->frame_index *
                                       animation->frame_bytes,
                                       animation->frame_bytes))
            animation->uploaded_index = animation->frame_index;
    }
}

/* Grows the per-sector visibility bitset to hold sector_count bytes. */
static int hwr_scene_ensure_visible(struct hwr_scene *scene, size_t sector_count,
                                    size_t object_count)
{
    if (sector_count == 0)
        return -1;
    if (scene->visible_count < sector_count) {
        uint8_t *buffer = (uint8_t *)realloc(scene->visible, sector_count);
        if (buffer == NULL)
            return -1;
        scene->visible = buffer;
        scene->visible_count = sector_count;
    }
    if (scene->oracle_visible_count < sector_count) {
        uint8_t *buffer =
            (uint8_t *)realloc(scene->oracle_visible, sector_count);
        if (buffer == NULL)
            return -1;
        scene->oracle_visible = buffer;
        scene->oracle_visible_count = sector_count;
    }
    if (scene->reflection_span_count < sector_count) {
        struct hwr_sector_xspan *buffer =
            (struct hwr_sector_xspan *)realloc(
                scene->reflection_spans, sector_count * sizeof *buffer);
        if (buffer == NULL)
            return -1;
        scene->reflection_spans = buffer;
        scene->reflection_span_count = sector_count;
    }
    if (scene->wide_span_count < sector_count) {
        struct hwr_sector_xspan *buffer =
            (struct hwr_sector_xspan *)realloc(
                scene->wide_spans, sector_count * sizeof *buffer);
        if (buffer == NULL)
            return -1;
        scene->wide_spans = buffer;
        scene->wide_span_count = sector_count;
    }
    if (object_count != 0 && scene->span_count < object_count) {
        struct hwr_sector_xspan *buffer = (struct hwr_sector_xspan *)realloc(
            scene->spans, object_count * sizeof *buffer);
        if (buffer == NULL)
            return -1;
        scene->spans = buffer;
        scene->span_count = object_count;
    }
    return 0;
}

/* Reports the cull ratio once per mesh build under HWR_PVS_LOG=1. */
static void hwr_scene_log_visibility(const struct hwr_scene *scene,
                                     const struct hwr_world *world)
{
    size_t visible_sectors = 0;
    size_t i;
    if (!scene->pvs_valid || getenv("HWR_PVS_LOG") == NULL)
        return;
    for (i = 0; i < world->sector_count; i++)
        visible_sectors += scene->visible[i] != 0 ? 1u : 0u;
    fprintf(stderr,
            "[renderer_overhaul] portal-reachable visibility: %u/%u sectors\n",
            (unsigned)visible_sectors, (unsigned)world->sector_count);
}

static void hwr_scene_check_visibility_oracle(
    struct hwr_scene *scene, const struct hwr_world *world,
    int engine_valid, int transcribed_valid)
{
    size_t missing = 0;
    size_t extra = 0;
    size_t i;
    if (!engine_valid || !transcribed_valid)
        return;
    for (i = 0; i < world->sector_count; i++) {
        if (scene->oracle_visible[i] && !scene->visible[i])
            missing++;
        else if (!scene->oracle_visible[i] && scene->visible[i])
            extra++;
    }
    if (missing != 0 || extra != 0) {
        if (!scene->wide_oracle_error_logged) {
            fprintf(stderr,
                    "[renderer_overhaul] widened portal walk differs in the original view: missing=%u extra=%u; retaining the engine center\n",
                    (unsigned)missing, (unsigned)extra);
            scene->wide_oracle_error_logged = 1;
        }
    } else {
        scene->wide_oracle_error_logged = 0;
        if (getenv("HWR_PVS_ORACLE_LOG") != NULL)
            fprintf(stderr,
                    "[renderer_overhaul] widened portal walk matches the original view\n");
    }
}

#define VA_G_REFLECTION_COUNT   0x85340u
#define VA_G_REFLECTION_RECORDS 0x85344u

static int hwr_mirrors_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *setting = getenv("HWR_MIRRORS");
        enabled = setting != NULL && setting[0] == '0' ? 0 : 1;
    }
    return enabled;
}

static size_t hwr_find_wall_offset(const struct hwr_world *world,
                                   uint16_t wall_offset)
{
    size_t i;
    for (i = 0; i < world->wall_count; i++)
        if (world->walls[i].section_offset == wall_offset)
            return i;
    return SIZE_MAX;
}

static int16_t hwr_reflect_view_offset(int16_t offset, int16_t coordinate)
{
    uint16_t sum = (uint16_t)((uint16_t)offset + (uint16_t)coordinate);
    return (int16_t)(uint16_t)((uint16_t)offset -
                               (uint16_t)(sum + sum));
}

static int16_t hwr_negate_i16(int16_t value)
{
    return (int16_t)(uint16_t)(0u - (uint16_t)value);
}

static void hwr_reflect_camera(struct hwr_gl_camera *reflected,
                               const struct hwr_gl_camera *primary,
                               const struct hwr_world *world,
                               const struct hwr_wall *wall)
{
    const struct hwr_vertex *a = &world->vertices[wall->vertex_a_index];
    const struct hwr_vertex *b = &world->vertices[wall->vertex_b_index];
    int16_t dx = (int16_t)(uint16_t)((uint16_t)a->world_x -
                                      (uint16_t)b->world_x);
    int16_t dy = (int16_t)(uint16_t)((uint16_t)a->world_y -
                                      (uint16_t)b->world_y);

    if (dx < 0)
        dx = hwr_negate_i16(dx);
    if (dy < 0)
        dy = hwr_negate_i16(dy);
    *reflected = *primary;
    reflected->x_flip = 1;
    reflected->reflected = 1;
    if (dx >= dy) {
        /* The wall spans X, so the engine snaps it to constant Y at endpoint
         * A and reflects the view across that axis. */
        reflected->view_offset_y = hwr_reflect_view_offset(
            primary->view_offset_y, a->world_y);
        reflected->yaw =
            (uint16_t)(0u - primary->yaw + 0x100u);
        reflected->yaw_sin = hwr_negate_i16(primary->yaw_sin);
        reflected->yaw_cos = primary->yaw_cos;
    } else {
        /* The wall spans Y: snap to constant X at endpoint A. */
        reflected->view_offset_x = hwr_reflect_view_offset(
            primary->view_offset_x, a->world_x);
        reflected->yaw = (uint16_t)(0u - primary->yaw);
        reflected->yaw_sin = primary->yaw_sin;
        reflected->yaw_cos = hwr_negate_i16(primary->yaw_cos);
    }
    reflected->sky_yaw = reflected->yaw;
}

static int hwr_reflection_project_x(const struct hwr_gl_camera *camera,
                                    int16_t x, int16_t y, int fallback)
{
    int32_t view_x, depth;
    hwr_wall_view_point(camera, x, y, &view_x, &depth);
    if (depth < 0x1000)
        return fallback;
    return hwr_project_fixed(view_x, camera->focal_x, depth,
                             camera->center_x);
}

static size_t hwr_scene_collect_reflections(
    struct hwr_scene *scene, const struct hwr_world *world,
    const struct roth_api_v1 *api, const struct hwr_gl_camera *primary,
    uint32_t width)
{
    uint32_t count;
    uint32_t i;

    scene->reflection_count = 0;
    if (!hwr_mirrors_enabled() || width == 0 ||
        world->sector_offset_to_index == NULL)
        return 0;
    count = api->game_ram->u32(VA_G_REFLECTION_COUNT);
    if (count > HWR_REFLECTION_MAX)
        return 0;
    for (i = 0; i < count; i++) {
        uint32_t record = VA_G_REFLECTION_RECORDS + i * 8u;
        uint32_t parent_key = api->game_ram->u32(record);
        uint16_t target_offset = api->game_ram->u16(record + 4u);
        uint16_t wall_offset = api->game_ram->u16(record + 6u);
        uint16_t target_index = UINT16_MAX;
        size_t wall_index;
        const struct hwr_wall *wall;
        const struct hwr_vertex *a;
        const struct hwr_vertex *b;
        struct hwr_scene_reflection *reflection;
        int xa, xb, lo, hi, final_lo, final_hi;

        if (parent_key != 0)
            continue;
        wall_index = hwr_find_wall_offset(world, wall_offset);
        if (wall_index == SIZE_MAX)
            continue;
        wall = &world->walls[wall_index];
        a = &world->vertices[wall->vertex_a_index];
        b = &world->vertices[wall->vertex_b_index];
        reflection = &scene->reflections[scene->reflection_count];
        reflection->wall_offset = wall_offset;
        reflection->renderable = 0;
        scene->reflection_count++;

        /* Registration and subview rendering are separate engine decisions.
         * Even a record whose target/span cannot be rendered still owns its
         * mirror surface in the primary pass, so retain the wall identity. */
        if (target_offset == 0)
            continue;
        target_index = world->sector_offset_to_index[target_offset];
        if (target_index == UINT16_MAX ||
            target_index >= world->sector_count)
            continue;
        hwr_reflect_camera(&reflection->camera, primary, world, wall);

        xa = hwr_reflection_project_x(&reflection->camera,
                                      a->world_x, a->world_y,
                                      (int)width - 1);
        xb = hwr_reflection_project_x(&reflection->camera,
                                      b->world_x, b->world_y, 0);
        /* Endpoint A is the high edge and endpoint B the low edge. The
         * original never sorts them: a reversed pair rejects the subview. */
        if (xa < 0 || xb >= (int)width)
            continue;
        lo = xb < 0 ? 0 : xb;
        hi = xa >= (int)width ? (int)width - 1 : xa;
        if (lo > hi)
            continue;
        /* The engine projects the aperture before its rasterizer x-flips the
         * destination. Convert that accepted source interval to framebuffer
         * columns for the GL scissor. */
        final_lo = 2 * reflection->camera.center_x - hi;
        final_hi = 2 * reflection->camera.center_x - lo;
        if (final_lo < 0)
            final_lo = 0;
        if (final_hi >= (int)width)
            final_hi = (int)width - 1;
        if (final_lo > final_hi)
            continue;
        reflection->target_sector_index = target_index;
        reflection->source_clip_left = lo;
        reflection->source_clip_right = hi;
        reflection->clip_left = final_lo;
        reflection->clip_right = final_hi + 1;
        reflection->renderable = 1;
    }
    return scene->reflection_count;
}

static int hwr_scene_store_reflection(struct hwr_scene_reflection *reflection,
                                      const struct hwr_scene *scene,
                                      size_t sky_count,
                                      size_t mirror_depth_count,
                                      size_t opaque_count,
                                      size_t flat_count,
                                      size_t translucent_count,
                                      size_t translucent_run_count,
                                      size_t exception_run_count)
{
    size_t vertex_count = sky_count + opaque_count + translucent_count +
                          mirror_depth_count;
    if (vertex_count > reflection->vertex_capacity) {
        struct hwr_gl_vertex *vertices = (struct hwr_gl_vertex *)realloc(
            reflection->vertices, vertex_count * sizeof *vertices);
        if (vertices == NULL)
            return -1;
        reflection->vertices = vertices;
        reflection->vertex_capacity = vertex_count;
    }
    if (translucent_run_count > reflection->run_capacity) {
        uint32_t *runs = (uint32_t *)realloc(
            reflection->runs, translucent_run_count * sizeof *runs);
        uint8_t *flags;
        if (runs == NULL)
            return -1;
        reflection->runs = runs;
        flags = (uint8_t *)realloc(
            reflection->run_flags, translucent_run_count * sizeof *flags);
        if (flags == NULL)
            return -1;
        reflection->run_flags = flags;
        reflection->run_capacity = translucent_run_count;
    }
    if (vertex_count != 0)
        memcpy(reflection->vertices, scene->stream_vertices,
               vertex_count * sizeof *reflection->vertices);
    if (translucent_run_count != 0)
        memcpy(reflection->runs, scene->translucent_runs,
               translucent_run_count * sizeof *reflection->runs);
    if (translucent_run_count != 0)
        memcpy(reflection->run_flags, scene->translucent_run_flags,
               translucent_run_count * sizeof *reflection->run_flags);
    reflection->vertex_count = vertex_count;
    reflection->run_count = translucent_run_count;
    reflection->sky_count = sky_count;
    reflection->mirror_depth_count = mirror_depth_count;
    reflection->opaque_count = opaque_count;
    reflection->flat_count = flat_count;
    reflection->translucent_count = translucent_count;
    reflection->opaque_mesh_count = scene->object_meshes.opaque_count;
    reflection->opaque_billboard_count = scene->billboards.opaque_count;
    reflection->shadow_count = scene->billboards.shadow_count;
    reflection->shadow_run_count = scene->billboards.shadow_run_count;
    reflection->translucent_exception_run_count = exception_run_count;
    reflection->slab_run_count = scene->slab_run_count;
    if (scene->slab_run_count != 0)
        memcpy(reflection->slab_runs, scene->slab_runs,
               scene->slab_run_count * sizeof *reflection->slab_runs);
    return 0;
}

static int hwr_scene_emit_view(struct hwr_scene *scene,
                               const struct hwr_world *world,
                               const struct hwr_objects *objects,
                               const struct roth_api_v1 *api,
                               const struct hwr_gl_camera *camera,
                               const struct hwr_sector_xspan *spans,
                               size_t span_count,
                               uint32_t width, uint32_t height,
                               size_t *sky_count, size_t *mirror_depth_count,
                               size_t *opaque_count,
                               size_t *flat_count,
                               size_t *translucent_count,
                               size_t *translucent_run_count,
                               size_t *exception_run_count)
{
    if (hwr_meshes_emit(&scene->object_meshes, objects, world, camera,
                        scene->visible, scene->visible_count, scene->pvs_valid,
                        spans, span_count,
                        api) != 0)
        return hwr_scene_fail(scene, "%s",
                              hwr_meshes_error(&scene->object_meshes));
    hwr_billboards_emit(&scene->billboards, objects, world, camera,
                        scene->visible, scene->visible_count, scene->pvs_valid,
                        spans, span_count, api);
    return hwr_prepare_stream(scene, camera, width, height,
                              sky_count, mirror_depth_count, opaque_count,
                              flat_count,
                              translucent_count, translucent_run_count,
                              exception_run_count);
}

void hwr_scene_sun_frame(struct hwr_scene *scene, const struct hwr_world *world,
                         const struct hwr_objects *objects,
                         const struct roth_api_v1 *api)
{
    struct hwr_gl_camera camera;

    if (scene == NULL || !scene->valid || !scene->uploaded || world == NULL ||
        !world->valid || api == NULL || api->game_ram == NULL) {
        hwr_sun_frame(NULL, NULL, NULL, NULL, NULL);
        return;
    }
    hwr_read_camera(&camera, world, api);
    /* The card sizing law reads the ENGINE's own focals; before the
     * projection rescale those are exactly what hwr_read_camera staged. */
    camera.source_focal_x = camera.focal_x;
    camera.source_focal_y = camera.focal_y;
    hwr_sun_frame(scene, world, objects, api, &camera);
}

const uint32_t *hwr_scene_render(struct hwr_scene *scene,
                                 const struct hwr_world *world,
                                 const struct hwr_objects *objects,
                                 const struct roth_api_v1 *api,
                                 uint32_t width, uint32_t height,
                                 uint32_t source_width,
                                 uint32_t source_height)
{
    struct hwr_gl_camera camera;
    struct hwr_gl_scene_view views[HWR_REFLECTION_MAX + 1u];
    uint8_t palette[1024];
    uint8_t base_palette[1024];
    struct hwr_gl_palette published;
    size_t sky_count;
    size_t mirror_depth_count;
    size_t opaque_count;
    size_t flat_count;
    size_t translucent_count;
    size_t translucent_run_count;
    size_t exception_run_count;
    size_t i;
    size_t view_count = 0;
    int floor_exception = hwr_billboards_floor_exception_enabled();
    int source_left = 0;
    int source_right = 0;
    int wide_projection = 0;
    if (scene == NULL || !scene->valid || !scene->uploaded ||
        world == NULL || !world->valid || api == NULL || api->game_ram == NULL)
        return NULL;
    if (hwr_scene_build_mesh(&scene->mesh, world, 0) != 0) {
        hwr_scene_fail(scene, "%s", scene->mesh.error);
        return NULL;
    }
    /* Counted afresh over every view this frame emits, the reflected ones
     * included: a surface that shows the previous frame is a property of what is
     * on screen, not of the map. */
    scene->stale_mask_seen = 0;
    hwr_scene_animate(scene, api);
    /* The light the player carries, moved on for this frame: the flame in it
     * and what this map's own candle word makes of it are both one number a
     * frame, and this is where the frame is. It sits beside the animated
     * surfaces on purpose -- they are driven by the same world clock, and a
     * flame that kept moving behind a held frame while every animation in the
     * room stopped would be the only thing in the picture still running. */
    hwr_gl_lamp_advance(world->luts.candle_glow,
                        api->game_ram->u16(VA_G_ANIM_CLOCK),
                        hwr_perf_now_ns());
    hwr_read_camera(&camera, world, api);
    if (!hwr_projection_scale_camera(
            &camera, source_width, source_height, width, height,
            hwr_settings_get()->fov_deg)) {
        hwr_scene_fail(scene, "projection dimensions are not representable");
        return NULL;
    }
    if (hwr_projection_source_x_bounds(
            &camera, width, &source_left, &source_right)) {
        wide_projection =
            source_left != 0 || source_right != (int)source_width;
    }
    camera.sky_texture_offset = scene->sky_texture_offset;
    camera.sky_projection_offset = scene->sky_projection_offset;
    camera.sky_source_offset = scene->sky_source_offset;
    camera.sky_texture_height = scene->sky_texture_height;
    camera.sky_start_row = (int16_t)(api->game_ram->u16(VA_G_SKY_START_ROW) +
                                     scene->sky_row_bias);
    camera.sky_yaw = api->game_ram->u16(VA_G_SKY_YAW);
    camera.sky_double_scanline = api->game_ram->u8(VA_G_DOUBLE_SCANLINE) != 0;
    camera.sky_valid = scene->sky_valid;
    if (hwr_scene_ensure_visible(scene, world->sector_count,
                                 objects != NULL ? objects->count : 0u) != 0) {
        hwr_scene_fail(scene, "out of memory preparing view visibility");
        return NULL;
    }

    /* The software pass consumed the reflected render tree before the hardware
     * callback. Rebuild its portal-reachable sector set from the recorded root
     * sector and pre-X-flip mirror aperture. Submitting the whole map here lets
     * geometry behind the virtual reflected camera leak through the mirror. */
    hwr_scene_collect_reflections(scene, world, api, &camera, width);
    for (i = 0; i < scene->reflection_count; i++) {
        struct hwr_scene_reflection *reflection = &scene->reflections[i];
        if (!reflection->renderable)
            continue;
        scene->pvs_valid = hwr_compute_reflection_pvs(
            world, &reflection->camera, reflection->target_sector_index,
            reflection->source_clip_left, reflection->source_clip_right,
            scene->visible, scene->reflection_spans,
            scene->reflection_span_count);
        if (!scene->pvs_valid) {
            /* A failed reflected walk must not reveal unrelated sectors. The
             * root alone is the safe, useful fallback for a valid record. */
            memset(scene->visible, 0, world->sector_count);
            scene->visible[reflection->target_sector_index] = 1;
            memset(scene->reflection_spans, 0,
                   world->sector_count * sizeof *scene->reflection_spans);
            scene->reflection_spans[
                reflection->target_sector_index].lo[0] =
                    (int16_t)reflection->source_clip_left;
            scene->reflection_spans[
                reflection->target_sector_index].hi[0] =
                    (int16_t)reflection->source_clip_right;
            scene->reflection_spans[
                reflection->target_sector_index].count = 1;
            scene->pvs_valid = 1;
        }
        scene->reflection_clip_valid = 1;
        hwr_prepare_reflection_door_windows(scene, world);
        hwr_prepare_reflection_object_windows(
            scene, objects, &reflection->camera, width);
        scene->portal_clip_valid =
            objects != NULL && scene->span_count >= objects->count;
        if (hwr_scene_emit_view(scene, world, objects, api,
                                &reflection->camera,
                                scene->portal_clip_valid
                                    ? scene->spans : NULL,
                                scene->portal_clip_valid && objects != NULL
                                    ? objects->count : 0u,
                                width, height, &sky_count,
                                &mirror_depth_count, &opaque_count,
                                &flat_count, &translucent_count,
                                &translucent_run_count,
                                &exception_run_count) != 0)
            return NULL;
        if (hwr_scene_store_reflection(reflection, scene, sky_count,
                                       mirror_depth_count,
                                       opaque_count, flat_count,
                                       translucent_count,
                                       translucent_run_count,
                                       exception_run_count) != 0) {
            hwr_scene_fail(scene,
                           "out of memory retaining reflected view geometry");
            return NULL;
        }
        memset(&views[view_count], 0, sizeof views[view_count]);
        views[view_count].vertices = reflection->vertices;
        views[view_count].sky_vertices = reflection->sky_count;
        views[view_count].mirror_depth_vertices =
            reflection->mirror_depth_count;
        views[view_count].opaque_vertices = reflection->opaque_count;
        views[view_count].flat_vertices = reflection->flat_count;
        views[view_count].translucent_vertices =
            reflection->translucent_count;
        views[view_count].translucent_runs = reflection->runs;
        views[view_count].translucent_run_flags = reflection->run_flags;
        views[view_count].translucent_run_count = reflection->run_count;
        views[view_count].opaque_mesh_vertices =
            reflection->opaque_mesh_count;
        views[view_count].opaque_billboard_vertices =
            reflection->opaque_billboard_count;
        views[view_count].shadow_vertices = reflection->shadow_count;
        views[view_count].shadow_run_count = reflection->shadow_run_count;
        views[view_count].translucent_exception_run_count =
            reflection->translucent_exception_run_count;
        views[view_count].slab_runs = reflection->slab_runs;
        views[view_count].slab_run_count = reflection->slab_run_count;
        views[view_count].floor_exception = floor_exception;
        views[view_count].camera = reflection->camera;
        views[view_count].clip_left = reflection->clip_left;
        views[view_count].clip_right = reflection->clip_right;
        view_count++;
    }
    /* Per-frame visibility (HWR_PVS). When disabled or when the renderer's draw
     * list cannot be read, pvs_valid stays 0 and the per-surface gate is a
     * no-op, leaving the frame identical to the plain stream-everything path. */
    scene->pvs_valid = 0;
    scene->portal_clip_valid = 0;
    scene->door_portal_clip_valid = 0;
    scene->reflection_clip_valid = 0;
    scene->wide_clip_valid = 0;
    if (hwr_pvs_enabled()) {
        if (wide_projection) {
            int engine_valid;
            int central_valid;
            int wide_valid;
            size_t s;

            memset(scene->oracle_visible, 0, world->sector_count);
            engine_valid = hwr_compute_pvs(
                world, &camera, api, scene->oracle_visible);
            central_valid = hwr_compute_portal_pvs(
                world, &camera, api, 0, (int)source_width, 0,
                scene->visible, scene->wide_spans,
                scene->wide_span_count);
            hwr_scene_check_visibility_oracle(
                scene, world, engine_valid, central_valid);
            wide_valid = hwr_compute_portal_pvs(
                world, &camera, api, source_left, source_right, 1,
                scene->visible, scene->wide_spans,
                scene->wide_span_count);
            if (wide_valid) {
                if (engine_valid)
                    for (s = 0; s < world->sector_count; s++)
                        if (scene->oracle_visible[s])
                            scene->visible[s] = 1;
                hwr_prepare_wide_sector_windows(
                    scene, world, scene->oracle_visible,
                    engine_valid, source_width);
                scene->pvs_valid = 1;
                scene->wide_clip_valid = hwr_portal_clip_enabled();
            } else if (engine_valid) {
                memcpy(scene->visible, scene->oracle_visible,
                       world->sector_count);
                scene->pvs_valid = 1;
            }

            if (objects != NULL && scene->spans != NULL &&
                scene->span_count >= objects->count)
                memset(scene->spans, 0,
                       objects->count * sizeof *scene->spans);
            memset(scene->door_spans, 0, sizeof scene->door_spans);
            /* Preserve the engine's object-depth-specific center windows, then
             * add only the side portions of the widened sector windows. */
            scene->portal_clip_valid =
                hwr_compute_object_portal_windows(
                    world, objects, api, scene->spans, scene->span_count);
            scene->door_portal_clip_valid =
                hwr_compute_door_portal_windows(
                    world, api, scene->door_spans, HWR_DOOR_MAX);
            if (scene->wide_clip_valid) {
                hwr_prepare_wide_object_windows(
                    scene, objects, source_width);
                hwr_prepare_wide_door_windows(
                    scene, world, source_width);
                scene->portal_clip_valid =
                    objects != NULL &&
                    scene->span_count >= objects->count;
                scene->door_portal_clip_valid = 1;
            }
        } else {
            scene->pvs_valid =
                hwr_compute_pvs(world, &camera, api, scene->visible);
            /* Reuse each type-1 worklist node's object-depth-specific window. */
            scene->portal_clip_valid = hwr_compute_object_portal_windows(
                world, objects, api, scene->spans, scene->span_count);
            scene->door_portal_clip_valid =
                hwr_compute_door_portal_windows(
                    world, api, scene->door_spans, HWR_DOOR_MAX);
        }
    }
    hwr_scene_log_visibility(scene, world);
    if (hwr_scene_emit_view(
            scene, world, objects, api, &camera,
            scene->portal_clip_valid ? scene->spans : NULL,
            scene->portal_clip_valid && objects != NULL ? objects->count : 0u,
            width, height, &sky_count, &mirror_depth_count, &opaque_count,
            &flat_count,
            &translucent_count, &translucent_run_count,
            &exception_run_count) != 0)
        return NULL;
    memset(&views[view_count], 0, sizeof views[view_count]);
    views[view_count].vertices = scene->stream_vertices;
    views[view_count].sky_vertices = sky_count;
    views[view_count].mirror_depth_vertices = mirror_depth_count;
    views[view_count].opaque_vertices = opaque_count;
    views[view_count].flat_vertices = flat_count;
    views[view_count].translucent_vertices = translucent_count;
    views[view_count].translucent_runs = scene->translucent_runs;
    views[view_count].translucent_run_flags = scene->translucent_run_flags;
    views[view_count].translucent_run_count = translucent_run_count;
    views[view_count].opaque_mesh_vertices =
        scene->object_meshes.opaque_count;
    views[view_count].opaque_billboard_vertices =
        scene->billboards.opaque_count;
    views[view_count].shadow_vertices = scene->billboards.shadow_count;
    views[view_count].shadow_run_count = scene->billboards.shadow_run_count;
    views[view_count].translucent_exception_run_count = exception_run_count;
    views[view_count].slab_runs = scene->slab_runs;
    views[view_count].slab_run_count = scene->slab_run_count;
    views[view_count].floor_exception = floor_exception;
    views[view_count].camera = camera;
    view_count++;

    hwr_build_palette(world, palette, base_palette, &published);
    hwr_gl_set_stale_mask(scene->stale_mask_seen);
    /* The lights that are not at the eye, found afresh for this frame and handed
     * over whole. It sits here, after every view is built and before any of them
     * is drawn, for the reason the array is in world space: a light belongs to
     * the frame rather than to the view that happens to see it, and each view
     * puts it into its own space as it binds. */
    {
        struct hwr_gl_point_light lights[HWR_GL_POINT_LIGHT_MAX];
        struct hwr_lights_frame frame;
        double hscale = 0.0;
        double vscale = 0.0;
        size_t count;

        memset(&frame, 0, sizeof frame);
        frame.world = world;
        frame.objects = objects;
        frame.api = api;
        frame.map_archive = scene->map_archive;
        frame.shared_archive = scene->shared_archive;
        frame.view_yaw = api->game_ram->u16(VA_G_SKY_YAW);
        /* How tall a sprite stands in the world, in the one number the seeding
         * needs of it: a thing's light comes from the middle of the sprite it is
         * drawn as, and that middle is the emitter's own sizing law rather than
         * a second opinion about it. A view with no projection staged yet hands
         * over nothing and leaves every light at its object's anchor. */
        if (hwr_sprite_scales(api, (double)camera.source_focal_x,
                              (double)camera.source_focal_y, &hscale, &vscale))
            frame.sprite_vscale = (float)vscale;
        /* The primary view's own visibility, which is the frame's: one light
         * set serves every view, so what a mirror shows is lit by the lights of
         * the rooms the player can see rather than by a second walk of its own.
         * That is the same reason the array is in world space to begin with. */
        frame.visible = scene->visible;
        frame.visible_count = scene->visible_count;
        frame.pvs_valid = scene->pvs_valid;
        frame.camera_sector = hwr_camera_sector(world, api);
        frame.view_offset_x = camera.view_offset_x;
        frame.view_offset_y = camera.view_offset_y;
        frame.eye_z = camera.eye_z;
        frame.candle_glow = world->luts.candle_glow;
        frame.now_ns = hwr_perf_now_ns();
        count = hwr_lights_build(&frame, lights, HWR_GL_POINT_LIGHT_MAX);
        hwr_gl_set_point_lights(lights, count);
    }
    return hwr_gl_scene_render(views, view_count, &published, width, height);
}
