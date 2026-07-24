#include "hwr_scene.h"

#include "hwr_settings.h"

#include "hwr_gl.h"

#include "hwr_visibility.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

struct hwr_point {
    int16_t x;
    int16_t y;
};

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
            hwr_set_vertex(&vertices[i * 3u + k], point->x, point->y, z, 0.0f, 0.0f);
        }
    }
    mesh->vertex_count += count;
    return 0;
}

static int hwr_build_sector_flats(struct hwr_scene_mesh *mesh,
                                  const struct hwr_world *world,
                                  const struct hwr_sector *sector,
                                  size_t sector_index, int debug)
{
    struct hwr_point *points;
    uint16_t *triangles;
    size_t count = sector->wall_count;
    size_t i;
    int result = 0;
    if (count < 3)
        goto degenerate;
    points = (struct hwr_point *)malloc(count * sizeof *points);
    triangles = (uint16_t *)malloc((count - 2u) * 3u * sizeof *triangles);
    if (points == NULL || triangles == NULL) {
        free(points);
        free(triangles);
        return hwr_mesh_fail(mesh, "out of memory triangulating sector %u",
                             (unsigned)sector_index);
    }
    for (i = 0; i < count; i++) {
        const struct hwr_wall *wall = &world->walls[sector->first_wall + i];
        const struct hwr_wall *next = &world->walls[
            sector->first_wall + (i + 1u) % count];
        const struct hwr_vertex *vertex = &world->vertices[wall->vertex_a_index];
        if (wall->vertex_b_index != next->vertex_a_index) {
            free(points);
            free(triangles);
            goto degenerate;
        }
        points[i].x = vertex->world_x;
        points[i].y = vertex->world_y;
    }
    if (hwr_triangulate(points, count, triangles) != 0) {
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
                if ((wall->mapping_flags & 1u) != 0) {
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

void hwr_scene_clear(struct hwr_scene *scene)
{
    if (scene == NULL)
        return;
    hwr_gl_scene_clear();
    hwr_scene_mesh_clear(&scene->mesh);
    hwr_scene_animations_clear(scene);
    hwr_billboards_clear(&scene->billboards);
    free(scene->stream_vertices);
    free(scene->translucent_runs);
    free(scene->visible);
    memset(scene, 0, sizeof *scene);
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

static int hwr_upload_referenced_textures(struct hwr_scene *scene,
                                           const struct hwr_world *world,
                                           struct hwr_das_archive *map_archive,
                                           struct hwr_das_archive *shared_archive,
                                           const struct roth_api_v1 *api,
                                           int debug)
{
    uint8_t *bytes = NULL;
    size_t size = 0;
    size_t capacity = 0;
    uint8_t zero = 0;
    uint16_t logical;
    int has_sky = 0;
    size_t surface_index;
    struct hwr_scene_texture_stats stats;
    memset(&stats, 0, sizeof stats);
    if (hwr_append_bytes(&bytes, &size, &capacity, &zero, 1) != 0)
        return hwr_scene_fail(scene, "out of memory creating texture buffer");
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
    /* Pack the sprite atlas into the SAME texel store, after the world textures,
     * so a billboard vertex references a byte offset exactly as a wall vertex
     * does. Appending leaves every world texture offset unchanged. */
    if (hwr_billboards_pack(&scene->billboards, map_archive, shared_archive, api,
                            &bytes, &size, &capacity) != 0) {
        free(bytes);
        return hwr_scene_fail(scene, "%s", scene->billboards.error);
    }
    if (debug)
        fprintf(stderr, "[renderer_overhaul] billboard atlas: map slots=%u shared slots=%u store=%u bytes\n",
                (unsigned)scene->billboards.map_dir_count,
                (unsigned)scene->billboards.shared_dir_count, (unsigned)size);
    if (!hwr_gl_scene_upload(bytes, size, world->luts.colormap, world->luts.tint,
                             world->luts.glow, world->luts.blend)) {
        free(bytes);
        return hwr_scene_fail(scene, "%s", hwr_gl_last_error());
    }
    free(bytes);
    scene->uploaded = 1;
    return 0;
}

int hwr_scene_rebuild(struct hwr_scene *scene, const struct hwr_world *world,
                      struct hwr_das_archive *map_archive,
                      struct hwr_das_archive *shared_archive,
                      const struct roth_api_v1 *api, int debug)
{
    if (scene == NULL)
        return -1;
    hwr_scene_clear(scene);
    hwr_billboards_init(&scene->billboards);
    scene->map_archive = map_archive;
    scene->shared_archive = shared_archive;
    if (hwr_scene_build_mesh(&scene->mesh, world, debug) != 0)
        return hwr_scene_fail(scene, "%s", scene->mesh.error);
    if (hwr_upload_referenced_textures(scene, world, map_archive,
                                       shared_archive, api, debug) != 0)
        return -1;
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
    camera->focal_x = api->game_ram->u16(VA_G_FOCAL_X);
    camera->focal_y = api->game_ram->u16(VA_G_FOCAL_Y);
    camera->center_x = (int32_t)api->game_ram->u32(VA_G_CENTER_X);
    camera->horizon = (int32_t)api->game_ram->u32(VA_G_HORIZON);
    camera->shade_origin_x = (int16_t)api->game_ram->u16(VA_G_SHADE_ORIGIN_X);
    camera->shade_origin_y = (int16_t)api->game_ram->u16(VA_G_SHADE_ORIGIN_Y);
    camera->shade_level = (uint8_t)world->luts.shade_level;
    camera->tint_shade_level = (uint8_t)world->luts.tint_shade_level;
    camera->global_light_bias = api->game_ram->u8(VA_G_GLOBAL_LIGHT_BIAS);
    camera->glow_phase = api->game_ram->u8(VA_G_GLOW_PHASE);
    {
        uintptr_t params = (uintptr_t)api->game_ram->u32(VA_G_VIEW_PARAMS_A);
        uint16_t view_bias = params >= 0x1000u
            ? hwr_scene_read_u16((const uint8_t *)params + 0x1c) : 0;
        camera->flat_edge_bias = (uint16_t)(view_bias +
            api->game_ram->u16(VA_G_PLAYER_LIGHT_BIAS));
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

static int hwr_surface_visible(const struct hwr_scene *scene,
                               const struct hwr_mesh_surface *surface,
                               const struct hwr_gl_camera *camera)
{
    const struct hwr_scene_mesh *mesh = &scene->mesh;
    int16_t z;
    /* Per-frame visibility gate. When the renderer's own drawn-sector set was
     * read successfully, a surface whose sector that set did not draw is
     * dropped -- including its sky caps -- so a sector stacked over or under the
     * camera's own, but not on the sight path, no longer bleeds through. Inert
     * while pvs_valid is 0. */
    if (scene->pvs_valid && surface->sector_index < scene->visible_count &&
        !scene->visible[surface->sector_index])
        return 0;
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

static int hwr_surface_translucent(struct hwr_scene *scene,
                                   struct hwr_mesh_surface *surface)
{
    uint16_t logical;
    uint16_t qflags;
    if (hwr_flat_blend_enabled() && hwr_flat_transparent(scene, surface))
        return 1;
    if ((surface->flags & (HWR_SURFACE_DIRECT | HWR_SURFACE_MASKED)) !=
        HWR_SURFACE_MASKED)
        return 0;
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

/* The candle radial edge term shared by walls and flats: the Chebyshev distance
 * from the jittered shade origin (0x90990/0x90992, the flickering light
 * position) to the projected point, in the engine's 16-bit shade domain, plus
 * the view-depth word.  screen_x/screen_y are the projected, viewport-clamped
 * pixel coordinates; depth is the clamped view-depth word. */
static uint16_t hwr_shade_edge_term(int screen_x, int screen_y,
                                    const struct hwr_gl_camera *camera,
                                    int depth)
{
    uint16_t scale_x = camera->focal_x != 0
        ? (uint16_t)(0x40000u / camera->focal_x) : 0;
    uint16_t scale_y = camera->focal_y != 0
        ? (uint16_t)(0x40000u / camera->focal_y) : 0;
    int16_t delta;
    uint16_t magnitude;
    uint16_t distance_x;
    uint16_t distance_y;
    uint16_t projected;

    delta = (int16_t)((uint16_t)screen_x - (uint16_t)camera->shade_origin_x);
    magnitude = (uint16_t)(delta < 0 ? -(int)delta : (int)delta);
    distance_x = (uint16_t)(((uint32_t)magnitude * scale_x) >> 8);
    delta = (int16_t)((uint16_t)screen_y - (uint16_t)camera->shade_origin_y);
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

    if (depth_f < 16.0)
        depth_f = 16.0;
    depth = (int)(depth_f * (camera->shade_depth_scale > 0.0f
                          ? camera->shade_depth_scale : 1.0f));
    if (depth < 16)
        depth = 16;
    screen_x = (int)(view_x * (double)camera->focal_x / depth_f) +
               camera->center_x;
    screen_y = (int)(((double)camera->eye_z - (double)bottom_z) *
                     (double)camera->focal_y / depth_f) + camera->horizon;
    if (screen_y < 0)
        screen_y = 0;
    else if (height != 0 && screen_y >= (int)height)
        screen_y = (int)height - 1;
    if (width != 0) {
        if (screen_x < 0)
            screen_x = 0;
        else if (screen_x >= (int)width)
            screen_x = (int)width - 1;
    }

    projected = hwr_shade_edge_term(screen_x, screen_y, camera, depth);

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

static int hwr_flat_project_y(const struct hwr_mesh_vertex *vertex,
                              const struct hwr_gl_camera *camera,
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
                ((int64_t)camera->focal_y << 8);
    y = (int)(projected / depth) + camera->horizon;
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
 * vertex's plane height) and the clamps are the FLAT clamps -- row<=0 -> 0
 * (BRIGHT; the flat driver floors at 0, unlike the wall's floor of 1) and
 * row>=0x1f -> 31, with no per-face clip scale. */
static uint16_t hwr_flat_shade_endpoint(const struct hwr_mesh_surface *surface,
                                    const struct hwr_mesh_vertex *vertex,
                                    const struct hwr_gl_camera *camera,
                                    uint32_t width, uint32_t height)
{
    double view_x;
    double depth_f = hwr_vertex_view_depth(vertex, camera, &view_x);
    int depth;
    int screen_x;
    int screen_y = hwr_flat_project_y(vertex, camera, height);
    uint16_t projected;
    uint8_t light = surface->light;
    int axv;
    int bias;
    int level = camera->tint_shade_level;
    int shade;

    if (depth_f < 16.0)
        depth_f = 16.0;
    depth = (int)(depth_f * (camera->shade_depth_scale > 0.0f
                          ? camera->shade_depth_scale : 1.0f));
    if (depth < 16)
        depth = 16;
    screen_x = (int)(view_x * (double)camera->focal_x / depth_f) +
               camera->center_x;
    if (width != 0) {
        if (screen_x < 0)
            screen_x = 0;
        else if (screen_x >= (int)width)
            screen_x = (int)width - 1;
    }

    projected = hwr_shade_edge_term(screen_x, screen_y, camera, depth);

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
        return 0;
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
        int y = hwr_flat_project_y(
            &mesh->vertices[surface->first_vertex + i], camera, height);
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

/* Worst-case streamed vertex count for a surface, so hwr_prepare_stream can
 * pre-size the stream buffer before the (possibly expanding) staging pass.  Only
 * tinted walls expand (along-length sub-quads); flats and sky caps are 1:1. */
static size_t hwr_surface_reserve(const struct hwr_mesh_surface *surface)
{
    if ((surface->flags & HWR_SURFACE_SKY_CAP) != 0)
        return surface->vertex_count;
    if ((surface->flags & HWR_SURFACE_FLAT) != 0)
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

static size_t hwr_copy_gpu_surface(struct hwr_scene *scene,
                                 const struct hwr_mesh_surface *surface,
                                 const struct hwr_gl_camera *camera,
                                 uint32_t width, uint32_t height,
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
    int wall_tint_adjust = 0;
    /* SOLID-FILL is allowed unless this is a masked (transparent) part whose
     * texture lacks the 0x2000000 override -- the engine's per-part `& 1`
     * exemption; windows / stained glass are never filled however saturated. */
    int fill_ok = hwr_fill_face_allowed(scene, surface);
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
    } else if (surface->kind == HWR_SURFACE_SOLID_MIDDLE &&
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
    if ((surface->flags & HWR_SURFACE_MASKED) != 0 || flat_transparent)
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
    if ((surface->flags & HWR_SURFACE_FLAT) != 0)
        hwr_flat_span_state(&scene->mesh, surface, camera, height,
                            flat_square256 && !flat_transparent,
                            &flat_guard, &flat_guard_step,
                            &flat_guard_origin_y);
    else {
        hwr_wall_span_state(&scene->mesh, surface, camera, width,
                            texture_width, texture_height, &wall_span);
        wall_tint_adjust =
            hwr_wall_tint_clip_adjust(surface, &scene->mesh, camera);
    }
    /* Solid-fill: a receding tinted wall is emitted as several along-length
     * sub-quads, each carrying its own wd_project corner words, so the shader's
     * screen-linear ramp re-anchors piecewise.  This subdivides only a face that
     * STRADDLES the saturation boundary -- some corners at 0x1fff and some not --
     * so its far sub-quads can solid-fill while the near ones stay texel-shaded;
     * a face with no saturation stays single-ramp and byte-exact. */
    if ((surface->flags & HWR_SURFACE_FLAT) == 0 &&
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
            tmpl.flat_mapping_flags = surface->mapping_flags;
            if (texture_height != 0 &&
                (texture_height & (uint16_t)(texture_height - 1u)) == 0)
                tmpl.wall_wrap_flags |= HWR_GL_WALL_WRAP_ALONG;
            if (texture_width != 0 &&
                (texture_width & (uint16_t)(texture_width - 1u)) == 0)
                tmpl.wall_wrap_flags |= HWR_GL_WALL_WRAP_VERTICAL;
            if (wall_stale)
                tmpl.wall_wrap_flags |= HWR_GL_WALL_STALE_MASK;
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
        if ((surface->flags & HWR_SURFACE_FLAT) == 0) {
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
            if (wall_stale)
                target->wall_wrap_flags |= HWR_GL_WALL_STALE_MASK;
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
    }
    /* Whole-face SOLID-FILL: a tinted wall that reaches this single-ramp path
     * (not subdivided) fills only when EVERY one of its corner words saturated
     * (== 0x1fff), i.e. the engine's counter==4 across the whole face.  A face
     * that merely straddles the boundary took the subdivided path above and is
     * filled per sub-quad there; a partially saturated face left here keeps its
     * texel shading, matching the engine (only counter==4 fills). */
    if (fill_ok &&
        (surface->flags & HWR_SURFACE_FLAT) == 0 &&
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

static int hwr_prepare_stream(struct hwr_scene *scene,
                              const struct hwr_gl_camera *camera,
                              uint32_t width, uint32_t height,
                              size_t *sky_count, size_t *opaque_count,
                              size_t *flat_count,
                              size_t *translucent_count,
                              size_t *translucent_run_count)
{
    size_t *translucent_order;
    float *depths;
    size_t visible_vertices = 0;
    size_t sky_vertices = 0;
    size_t translucent_surfaces = 0;
    size_t i;
    size_t sky_cursor = 0;
    size_t opaque_cursor;
    size_t translucent_cursor;
    /* Billboard quads emitted for this frame (0 when the layer is disabled):
     * raw quads follow the opaque surfaces, masked/blend-capable quads follow
     * the translucent surfaces, one run each. */
    const struct hwr_billboards *bb = &scene->billboards;
    size_t bb_opaque = bb->opaque_count;
    size_t bb_flat = bb->flat_count;
    size_t bb_translucent = bb->translucent_count;
    size_t bb_runs = bb->run_count;
    /* The stream packs three back-to-back ranges: [sky][opaque][translucent].
     * Sky caps come first so they can be drawn as a depth-off background pass;
     * everything else keeps its opaque-vs-translucent classification. */
    for (i = 0; i < scene->mesh.surface_count; i++) {
        struct hwr_mesh_surface *surface = &scene->mesh.surfaces[i];
        surface->flags &= (uint8_t)~HWR_SURFACE_TRANSLUCENT;
        if (!hwr_surface_visible(scene, surface, camera))
            continue;
        if ((surface->flags & HWR_SURFACE_SKY_CAP) != 0) {
            sky_vertices += surface->vertex_count;
        } else if (hwr_surface_translucent(scene, surface)) {
            surface->flags |= HWR_SURFACE_TRANSLUCENT;
            translucent_surfaces++;
        }
        /* Reserve the worst-case expansion: a tinted wall is split into along-
         * length sub-quads (<=2*HWR_WALLSEG_MAX); everything else is 1:1. */
        visible_vertices += hwr_surface_reserve(surface);
    }
    /* Billboards are exactly six vertices per quad, no per-face expansion. */
    visible_vertices += bb_opaque + bb_flat + bb_translucent;
    if (visible_vertices > scene->stream_capacity) {
        struct hwr_gl_vertex *vertices = (struct hwr_gl_vertex *)realloc(
            scene->stream_vertices, visible_vertices * sizeof *vertices);
        if (vertices == NULL)
            return hwr_scene_fail(scene, "out of memory streaming scene vertices");
        scene->stream_vertices = vertices;
        scene->stream_capacity = visible_vertices;
    }
    translucent_order = translucent_surfaces != 0
        ? (size_t *)malloc(translucent_surfaces * sizeof *translucent_order) : NULL;
    depths = translucent_surfaces != 0
        ? (float *)malloc(translucent_surfaces * sizeof *depths) : NULL;
    if (translucent_surfaces != 0 && (translucent_order == NULL || depths == NULL)) {
        free(translucent_order);
        free(depths);
        return hwr_scene_fail(scene, "out of memory sorting translucent surfaces");
    }
    translucent_surfaces = 0;
    opaque_cursor = sky_vertices;
    for (i = 0; i < scene->mesh.surface_count; i++) {
        struct hwr_mesh_surface *surface = &scene->mesh.surfaces[i];
        if (!hwr_surface_visible(scene, surface, camera))
            continue;
        if ((surface->flags & HWR_SURFACE_SKY_CAP) != 0) {
            sky_cursor += hwr_copy_gpu_surface(scene, surface, camera, width,
                                 height, scene->stream_vertices + sky_cursor);
        } else if ((surface->flags & HWR_SURFACE_TRANSLUCENT) == 0) {
            opaque_cursor += hwr_copy_gpu_surface(scene, surface, camera, width,
                                 height, scene->stream_vertices + opaque_cursor);
        } else {
            size_t position = translucent_surfaces;
            float depth = hwr_surface_depth(surface, &scene->mesh, camera);
            while (position > 0 && depths[position - 1u] < depth) {
                depths[position] = depths[position - 1u];
                translucent_order[position] = translucent_order[position - 1u];
                position--;
            }
            depths[position] = depth;
            translucent_order[position] = i;
            translucent_surfaces++;
        }
    }
    /* Raw billboards ride the tail of the opaque range; the fixed-orientation
     * (flat) quads follow them, still opaque but kept at the very tail so the GL
     * layer can draw them with a depth bias toward the viewer. */
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
    if (translucent_surfaces + bb_runs > scene->translucent_run_capacity) {
        uint32_t *runs = (uint32_t *)realloc(
            scene->translucent_runs,
            (translucent_surfaces + bb_runs) * sizeof *runs);
        if (runs == NULL) {
            free(translucent_order);
            free(depths);
            return hwr_scene_fail(scene, "out of memory recording translucent runs");
        }
        scene->translucent_runs = runs;
        scene->translucent_run_capacity = translucent_surfaces + bb_runs;
    }
    translucent_cursor = opaque_cursor;
    for (i = 0; i < translucent_surfaces; i++) {
        const struct hwr_mesh_surface *surface =
            &scene->mesh.surfaces[translucent_order[i]];
        size_t emitted = hwr_copy_gpu_surface(scene, surface, camera, width,
                             height, scene->stream_vertices + translucent_cursor);
        scene->translucent_runs[i] = (uint32_t)emitted;
        translucent_cursor += emitted;
    }
    /* Masked / blend-capable billboards, already depth-sorted back to front,
     * ride the tail of the translucent range, each as its own blend run. */
    if (bb_translucent != 0) {
        size_t r;
        memcpy(scene->stream_vertices + translucent_cursor, bb->translucent,
               bb_translucent * sizeof *scene->stream_vertices);
        translucent_cursor += bb_translucent;
        for (r = 0; r < bb_runs; r++)
            scene->translucent_runs[translucent_surfaces + r] = bb->runs[r];
    }
    *translucent_run_count = translucent_surfaces + bb_runs;
    free(translucent_order);
    free(depths);
    *sky_count = sky_cursor;
    *opaque_count = opaque_cursor - sky_vertices;
    *flat_count = bb_flat;
    *translucent_count = translucent_cursor - opaque_cursor;
    return 0;
}

static int hwr_arithmetic_shift_8(int value)
{
    return value >= 0 ? value / 256 : -((-value + 255) / 256);
}

static void hwr_build_palette(const struct hwr_world *world, uint8_t rgba[1024])
{
    int fade = 0xff - world->luts.damage_flash_level;
    unsigned i;
    for (i = 0; i < 256; i++) {
        int r = world->luts.palette[i * 3u];
        int g = world->luts.palette[i * 3u + 1u];
        int b = world->luts.palette[i * 3u + 2u];
        r = hwr_arithmetic_shift_8((r - 0x2d) * fade) + 0x2d;
        g = (g * fade) >> 8;
        b = (b * fade) >> 8;
        rgba[i * 4u] = (uint8_t)(r << 2);
        rgba[i * 4u + 1u] = (uint8_t)(g << 2);
        rgba[i * 4u + 2u] = (uint8_t)(b << 2);
        rgba[i * 4u + 3u] = 0xffu;
    }
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
static int hwr_scene_ensure_visible(struct hwr_scene *scene, size_t sector_count)
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

const uint32_t *hwr_scene_render(struct hwr_scene *scene,
                                 const struct hwr_world *world,
                                 const struct hwr_objects *objects,
                                 const struct roth_api_v1 *api,
                                 uint32_t width, uint32_t height)
{
    struct hwr_gl_camera camera;
    uint8_t palette[1024];
    size_t sky_count;
    size_t opaque_count;
    size_t flat_count;
    size_t translucent_count;
    size_t translucent_run_count;
    if (scene == NULL || !scene->valid || !scene->uploaded ||
        world == NULL || !world->valid || api == NULL || api->game_ram == NULL)
        return NULL;
    if (hwr_scene_build_mesh(&scene->mesh, world, 0) != 0) {
        hwr_scene_fail(scene, "%s", scene->mesh.error);
        return NULL;
    }
    hwr_scene_animate(scene, api);
    hwr_read_camera(&camera, world, api);
    camera.sky_texture_offset = scene->sky_texture_offset;
    camera.sky_projection_offset = scene->sky_projection_offset;
    camera.sky_source_offset = scene->sky_source_offset;
    camera.sky_texture_height = scene->sky_texture_height;
    camera.sky_start_row = (int16_t)(api->game_ram->u16(VA_G_SKY_START_ROW) +
                                     scene->sky_row_bias);
    camera.sky_yaw = api->game_ram->u16(VA_G_SKY_YAW);
    camera.sky_double_scanline = api->game_ram->u8(VA_G_DOUBLE_SCANLINE) != 0;
    camera.sky_valid = scene->sky_valid;
    /* Per-frame visibility (HWR_PVS). When disabled or when the renderer's draw
     * list cannot be read, pvs_valid stays 0 and the per-surface gate is a
     * no-op, leaving the frame identical to the plain stream-everything path. */
    scene->pvs_valid = 0;
    if (hwr_pvs_enabled() &&
        hwr_scene_ensure_visible(scene, world->sector_count) == 0)
        scene->pvs_valid = hwr_compute_pvs(world, &camera, api, scene->visible);
    hwr_scene_log_visibility(scene, world);
    /* Resolve and project this frame's billboards against the same camera and
     * visible-sector set, ready to append to the stream below. */
    hwr_billboards_emit(&scene->billboards, objects, world, &camera,
                        scene->visible, scene->visible_count, scene->pvs_valid,
                        api);
    if (hwr_prepare_stream(scene, &camera, width, height,
                           &sky_count, &opaque_count, &flat_count,
                           &translucent_count,
                           &translucent_run_count) != 0)
        return NULL;
    hwr_build_palette(world, palette);
    return hwr_gl_scene_render(scene->stream_vertices, sky_count, opaque_count,
                               flat_count, translucent_count,
                               scene->translucent_runs,
                               translucent_run_count,
                               scene->billboards.opaque_count,
                               scene->billboards.shadow_count,
                               scene->billboards.shadow_run_count,
                               hwr_billboards_floor_exception_enabled(),
                               &camera, palette,
                               width, height);
}
