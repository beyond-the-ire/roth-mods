#include "hwr_scene.h"

#include "hwr_settings.h"

#include "hwr_gl.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HWR_WALL_U_REVERSED 0
#define HWR_SKY_FILL_INDEX 0x7fu
#define HWR_GROUP_PART_TABLE_VA 0x909b4u

struct hwr_point {
    int16_t x;
    int16_t y;
};

struct hwr_scene_texture_stats {
    size_t plain;
    size_t grouped;
    size_t animated_frame0;
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
    memset(mesh, 0, sizeof *mesh);
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
        surface->flags |= HWR_SURFACE_SKY | HWR_SURFACE_DIRECT;
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
    uint16_t extent;
    uint16_t mapped_extent;
    float sa;
    float sb;
    if (id >= 0 && (uint16_t)id == world->world_span_colorkey)
        return 0;
    if (top_a <= bottom_a && top_b <= bottom_b)
        return 0;
    a = &world->vertices[wall->vertex_a_index];
    b = &world->vertices[wall->vertex_b_index];
    extent = wall->extent < 0 ? (uint16_t)wall->extent & 0x0fffu
                              : (uint16_t)wall->extent;
    mapped_extent = (mapping_flags & 0x20u) != 0
        ? (uint16_t)(extent + extent) : extent;
    sa = HWR_WALL_U_REVERSED ? (float)mapped_extent : 0.0f;
    sb = HWR_WALL_U_REVERSED ? 0.0f : (float)mapped_extent;
    surface = hwr_add_surface(mesh, kind, 6);
    if (surface == NULL)
        return -1;
    surface->sector_index = wall->sector_index;
    surface->extent = extent;
    surface->scale = wall->scale;
    surface->pan_u = wall->extent < 0 ? wall->pan_u : 0;
    surface->pan_v = wall->extent < 0 ? wall->pan_v : 0;
    hwr_finish_material(mesh, surface, sector, id, mapping_flags, masked);
    v = mesh->vertices + mesh->vertex_count;
    hwr_set_vertex(&v[0], a->world_x, a->world_y, top_a, sa, (float)top_a);
    hwr_set_vertex(&v[1], a->world_x, a->world_y, bottom_a, sa, (float)top_a);
    hwr_set_vertex(&v[2], b->world_x, b->world_y, bottom_b, sb, (float)top_b);
    hwr_set_vertex(&v[3], a->world_x, a->world_y, top_a, sa, (float)top_a);
    hwr_set_vertex(&v[4], b->world_x, b->world_y, bottom_b, sb, (float)top_b);
    hwr_set_vertex(&v[5], b->world_x, b->world_y, top_b, sb, (float)top_b);
    mesh->vertex_count += 6;
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
    hwr_finish_material(mesh, surface, sector, id, 0, 0);
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
    result |= hwr_emit_flat(mesh, world, sector, sector_index, points, triangles,
                            count - 2u, HWR_SURFACE_FLOOR, sector->floor_z,
                            sector->floor_flat_id, sector->floor_pan,
                            (uint8_t)((sector->flags >> 4) & 3u), 0);
    if (sector->slab_index != SIZE_MAX) {
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
    if (neighbor->slab_index == SIZE_MAX)
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
    if (sector->slab_index != SIZE_MAX) {
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
                                  0, (uint8_t)(wall->mapping_flags & 0xfau)) != 0)
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
                                  0, (uint8_t)(wall->mapping_flags & 0xfau)) != 0)
                    goto failed;
                if ((wall->mapping_flags & 1u) != 0) {
                    int16_t top = sector->ceiling_z < neighbor->ceiling_z
                                ? sector->ceiling_z : neighbor->ceiling_z;
                    int16_t bottom = sector->floor_z > neighbor->floor_z
                                   ? sector->floor_z : neighbor->floor_z;
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
        }
    }
    mesh->valid = 1;
    return 0;

failed:
    if (mesh->error[0] == 0)
        snprintf(mesh->error, sizeof mesh->error, "mesh construction failed");
    return -1;
}

void hwr_scene_clear(struct hwr_scene *scene)
{
    if (scene == NULL)
        return;
    hwr_gl_scene_clear();
    hwr_scene_mesh_clear(&scene->mesh);
    free(scene->stream_vertices);
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
    struct hwr_scene_texture_stats stats;
    memset(&stats, 0, sizeof stats);
    if (hwr_append_bytes(&bytes, &size, &capacity, &zero, 1) != 0)
        return hwr_scene_fail(scene, "out of memory creating texture buffer");
    memset(scene->textures, 0, sizeof scene->textures);
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
            else if (resolved_class == HWR_DAS_CLASS_ANIMATED)
                stats.animated_frame0++;
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
    if (debug)
        fprintf(stderr, "[renderer_overhaul] scene stats: surfaces=%u quads=%u textures plain=%u grouped=%u anim-frame0=%u fill=%u fallbacks=%u\n",
                (unsigned)scene->mesh.surface_count,
                (unsigned)hwr_scene_quad_count(&scene->mesh),
                (unsigned)stats.plain, (unsigned)stats.grouped,
                (unsigned)stats.animated_frame0, (unsigned)stats.fill,
                (unsigned)stats.fallbacks);
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

static int hwr_surface_visible(const struct hwr_scene *scene,
                               const struct hwr_mesh_surface *surface,
                               int16_t eye_z)
{
    const struct hwr_scene_mesh *mesh = &scene->mesh;
    int16_t z;
    if (surface->texture_id >= 0 &&
        (uint16_t)surface->texture_id < 0x1600u &&
        scene->textures[(uint16_t)surface->texture_id].skip)
        return 0;
    if ((surface->flags & HWR_SURFACE_FLAT) == 0)
        return 1;
    z = mesh->vertices[surface->first_vertex].z;
    return (surface->flags & HWR_SURFACE_FACE_DOWN) != 0 ? eye_z < z : eye_z > z;
}

static int hwr_surface_translucent(struct hwr_scene *scene,
                                   struct hwr_mesh_surface *surface)
{
    uint16_t logical;
    uint16_t qflags;
    if ((surface->flags & (HWR_SURFACE_DIRECT | HWR_SURFACE_SKY |
                           HWR_SURFACE_MASKED)) != HWR_SURFACE_MASKED)
        return 0;
    logical = (uint16_t)surface->texture_id;
    qflags = logical < 0x1600u ? scene->textures[logical].qflags : 0;
    return (qflags & 0x400u) != 0 && (qflags & 0x200u) == 0;
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

static uint16_t hwr_wall_endpoint_word(const struct hwr_mesh_surface *surface,
                                       const struct hwr_mesh_vertex *vertex,
                                       int16_t bottom_z,
                                       const struct hwr_gl_camera *camera,
                                       uint32_t width, uint32_t height)
{
    int dx = (int16_t)((uint16_t)vertex->x + (uint16_t)camera->view_offset_x);
    int dy = (int16_t)((uint16_t)vertex->y + (uint16_t)camera->view_offset_y);
    double sine = (double)camera->yaw_sin / 16384.0;
    double cosine = (double)camera->yaw_cos / 16384.0;
    double view_x = sine * (double)dx + cosine * (double)dy;
    double depth_f = sine * (double)dy - cosine * (double)dx;
    int depth;
    int screen_x;
    int screen_y;
    int16_t delta;
    uint16_t magnitude;
    uint16_t scale_x;
    uint16_t scale_y;
    uint16_t distance_x;
    uint16_t distance_y;
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

    scale_x = camera->focal_x != 0
        ? (uint16_t)(0x40000u / camera->focal_x) : 0;
    scale_y = camera->focal_y != 0
        ? (uint16_t)(0x40000u / camera->focal_y) : 0;
    delta = (int16_t)((uint16_t)screen_x - (uint16_t)camera->shade_origin_x);
    magnitude = (uint16_t)(delta < 0 ? -(int)delta : (int)delta);
    distance_x = (uint16_t)(((uint32_t)magnitude * scale_x) >> 8);
    delta = (int16_t)((uint16_t)screen_y - (uint16_t)camera->shade_origin_y);
    magnitude = (uint16_t)(delta < 0 ? -(int)delta : (int)delta);
    distance_y = (uint16_t)(((uint32_t)magnitude * scale_y) >> 8);
    projected = distance_y > distance_x ? distance_y : distance_x;
    projected = (uint16_t)(projected + (uint16_t)depth);

    if (light != 0)
        light = (uint8_t)(light + camera->global_light_bias);
    axv = light == 0 ? 0 : (int)light - 128;
    bias = 8 + axv;
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

static void hwr_copy_gpu_surface(struct hwr_scene *scene,
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
    size_t i;
    if (logical < 0x1600u) {
        texture_width = scene->textures[logical].width;
        texture_height = scene->textures[logical].height;
    }
    if ((surface->flags & HWR_SURFACE_MASKED) != 0)
        flags |= HWR_GL_MATERIAL_MASKED;
    if ((surface->flags & HWR_SURFACE_FLAT) != 0)
        flags |= HWR_GL_MATERIAL_FLAT;
    if ((surface->flags & HWR_SURFACE_SKY) != 0) {
        flags |= HWR_GL_MATERIAL_SKY | HWR_GL_MATERIAL_DIRECT;
        direct_index = HWR_SKY_FILL_INDEX;
    } else if ((surface->flags & HWR_SURFACE_DIRECT) != 0) {
        flags |= HWR_GL_MATERIAL_DIRECT;
    } else if (logical < 0x1600u && scene->textures[logical].direct) {
        flags |= HWR_GL_MATERIAL_DIRECT;
        direct_index = scene->textures[logical].direct_index;
    }
    if (((surface->mapping_flags & 2u) != 0) !=
        (logical < 0x1600u && scene->textures[logical].flip_toggle != 0))
        flags |= HWR_GL_MATERIAL_HFLIP;
    if ((surface->flags & HWR_SURFACE_TRANSLUCENT) != 0)
        flags |= HWR_GL_MATERIAL_TRANSLUCENT;
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
            int16_t bottom_z = source->z;
            int fit_texture = (surface->mapping_flags & 4u) != 0 &&
                              texture_width != 0 && texture_height != 0;
            size_t j;
            for (j = 0; j < surface->vertex_count; j++) {
                if (wall_vertices[j].x == source->x &&
                    wall_vertices[j].y == source->y &&
                    wall_vertices[j].z < bottom_z)
                    bottom_z = wall_vertices[j].z;
            }
            target->wall_shade_endpoint = hwr_wall_endpoint_word(
                surface, source, bottom_z, camera, width, height);
            if (fit_texture) {
                unsigned scale_u = surface->scale & 0x0fu;
                unsigned scale_v = surface->scale >> 4;
                float along_extent = 2.0f * (float)texture_height *
                    (float)(scale_u != 0 ? scale_u + 1u : 1u);
                float vertical_extent = 2.0f * (float)texture_width *
                    (float)(scale_v != 0 ? scale_v + 1u : 1u);
                target->wall_s = source->x == wall_vertices[0].x &&
                                 source->y == wall_vertices[0].y
                    ? 0.0f : along_extent;
                target->wall_top = source->z == source->wall_top
                    ? 0.0f : vertical_extent;
            } else {
                target->wall_s = source->wall_s;
                target->wall_top = (float)(source->wall_top - source->z) *
                    ((surface->mapping_flags & 0x20u) != 0 ? 2.0f : 1.0f);
            }
            target->pan_u = fit_texture ? 0 : surface->pan_u;
            target->pan_v = fit_texture ? 0 : surface->pan_v;
        } else {
            target->wall_s = source->wall_s;
            target->wall_top = source->wall_top;
            target->pan_u = surface->pan_u;
            target->pan_v = surface->pan_v;
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
    }
}

static int hwr_prepare_stream(struct hwr_scene *scene,
                              const struct hwr_gl_camera *camera,
                              uint32_t width, uint32_t height,
                              size_t *opaque_count, size_t *translucent_count)
{
    size_t *translucent_order;
    float *depths;
    size_t visible_vertices = 0;
    size_t translucent_surfaces = 0;
    size_t i;
    size_t opaque_cursor = 0;
    size_t translucent_cursor;
    for (i = 0; i < scene->mesh.surface_count; i++) {
        struct hwr_mesh_surface *surface = &scene->mesh.surfaces[i];
        surface->flags &= (uint8_t)~HWR_SURFACE_TRANSLUCENT;
        if (!hwr_surface_visible(scene, surface, camera->eye_z))
            continue;
        if (hwr_surface_translucent(scene, surface)) {
            surface->flags |= HWR_SURFACE_TRANSLUCENT;
            translucent_surfaces++;
        }
        visible_vertices += surface->vertex_count;
    }
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
    for (i = 0; i < scene->mesh.surface_count; i++) {
        struct hwr_mesh_surface *surface = &scene->mesh.surfaces[i];
        if (!hwr_surface_visible(scene, surface, camera->eye_z))
            continue;
        if ((surface->flags & HWR_SURFACE_TRANSLUCENT) == 0) {
            hwr_copy_gpu_surface(scene, surface, camera, width, height,
                                 scene->stream_vertices + opaque_cursor);
            opaque_cursor += surface->vertex_count;
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
    translucent_cursor = opaque_cursor;
    for (i = 0; i < translucent_surfaces; i++) {
        const struct hwr_mesh_surface *surface =
            &scene->mesh.surfaces[translucent_order[i]];
        hwr_copy_gpu_surface(scene, surface, camera, width, height,
                             scene->stream_vertices + translucent_cursor);
        translucent_cursor += surface->vertex_count;
    }
    free(translucent_order);
    free(depths);
    *opaque_count = opaque_cursor;
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

const uint32_t *hwr_scene_render(struct hwr_scene *scene,
                                 const struct hwr_world *world,
                                 const struct roth_api_v1 *api,
                                 uint32_t width, uint32_t height)
{
    struct hwr_gl_camera camera;
    uint8_t palette[1024];
    size_t opaque_count;
    size_t translucent_count;
    if (scene == NULL || !scene->valid || !scene->uploaded ||
        world == NULL || !world->valid || api == NULL || api->game_ram == NULL)
        return NULL;
    if (hwr_scene_build_mesh(&scene->mesh, world, 0) != 0) {
        hwr_scene_fail(scene, "%s", scene->mesh.error);
        return NULL;
    }
    hwr_read_camera(&camera, world, api);
    if (hwr_prepare_stream(scene, &camera, width, height,
                           &opaque_count, &translucent_count) != 0)
        return NULL;
    hwr_build_palette(world, palette);
    return hwr_gl_scene_render(scene->stream_vertices, opaque_count,
                               translucent_count, &camera, palette, width, height);
}
