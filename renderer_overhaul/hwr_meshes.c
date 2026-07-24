#include "hwr_meshes.h"

#include "hwr_sprites.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HWR_MESH_HEADER_SIZE       0x16u
#define HWR_MESH_VERTEX_SIZE       0x10u
#define HWR_MESH_FACE_HEADER_SIZE  0x36u
#define HWR_MESH_NEAR_DEPTH        16.0
#define HWR_MESH_NO_MATERIAL       UINT32_MAX

#define VA_G_SINCOS_TABLE          0x72080u
#define VA_G_SPRITE_VIEW_ANGLE     0x909f8u
#define VA_G_VIEW_PARAMS_XSCALE    0x8527cu
#define VA_G_PERSPECTIVE_SCALE     0x85288u
#define VA_G_RENDER_TICK           0x90c0au
#define VA_G_FRAME_TIME_SCALE      0x85324u

struct hwr_mesh_pending {
    struct hwr_gl_vertex vertices[6];
    double depth;
    uint8_t count;
};

static uint16_t hwr_mesh_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint16_t hwr_mesh_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static int hwr_mesh_fail(struct hwr_meshes *meshes, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(meshes->error, sizeof meshes->error, format, args);
    va_end(args);
    return -1;
}

int hwr_meshes_enabled(void)
{
    static int state = -1;
    if (state < 0) {
        const char *setting = getenv("HWR_MESHES");
        state = setting != NULL && setting[0] == '0' ? 0 : 1;
    }
    return state;
}

void hwr_meshes_clear(struct hwr_meshes *meshes)
{
    if (meshes == NULL)
        return;
    free(meshes->map_dir);
    free(meshes->shared_dir);
    free(meshes->vertices);
    free(meshes->faces);
    free(meshes->materials);
    free(meshes->opaque);
    free(meshes->translucent);
    free(meshes->runs);
    memset(meshes, 0, sizeof *meshes);
}

const char *hwr_meshes_error(const struct hwr_meshes *meshes)
{
    return meshes != NULL && meshes->error[0] != 0
         ? meshes->error : "unknown mesh renderer error";
}

static int hwr_mesh_append(uint8_t **bytes, size_t *size, size_t *capacity,
                           const uint8_t *source, size_t count)
{
    size_t wanted;
    size_t next_capacity;
    uint8_t *grown;
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
        grown = (uint8_t *)realloc(*bytes, next_capacity);
        if (grown == NULL)
            return -1;
        *bytes = grown;
        *capacity = next_capacity;
    }
    memcpy(*bytes + *size, source, count);
    *size = wanted;
    return 0;
}

static int hwr_mesh_reserve_vertices(struct hwr_meshes *meshes, size_t count)
{
    struct hwr_mesh_local_vertex *grown;
    size_t next;
    if (count <= meshes->vertex_capacity)
        return 0;
    next = meshes->vertex_capacity != 0 ? meshes->vertex_capacity : 256u;
    while (next < count) {
        if (next > SIZE_MAX / 2u)
            return -1;
        next *= 2u;
    }
    grown = (struct hwr_mesh_local_vertex *)realloc(
        meshes->vertices, next * sizeof *grown);
    if (grown == NULL)
        return -1;
    meshes->vertices = grown;
    meshes->vertex_capacity = next;
    return 0;
}

static int hwr_mesh_reserve_faces(struct hwr_meshes *meshes, size_t count)
{
    struct hwr_mesh_face *grown;
    size_t next;
    if (count <= meshes->face_capacity)
        return 0;
    next = meshes->face_capacity != 0 ? meshes->face_capacity : 256u;
    while (next < count) {
        if (next > SIZE_MAX / 2u)
            return -1;
        next *= 2u;
    }
    grown = (struct hwr_mesh_face *)realloc(meshes->faces,
                                            next * sizeof *grown);
    if (grown == NULL)
        return -1;
    meshes->faces = grown;
    meshes->face_capacity = next;
    return 0;
}

static int hwr_mesh_reserve_materials(struct hwr_meshes *meshes, size_t count)
{
    struct hwr_mesh_material *grown;
    size_t next;
    if (count <= meshes->material_capacity)
        return 0;
    next = meshes->material_capacity != 0 ? meshes->material_capacity : 64u;
    while (next < count) {
        if (next > SIZE_MAX / 2u)
            return -1;
        next *= 2u;
    }
    grown = (struct hwr_mesh_material *)realloc(
        meshes->materials, next * sizeof *grown);
    if (grown == NULL)
        return -1;
    meshes->materials = grown;
    meshes->material_capacity = next;
    return 0;
}

static int hwr_mesh_is_resource(const struct hwr_das_entry *entry)
{
    return entry != NULL && entry->raw_payload != NULL &&
           entry->raw_payload_size >= HWR_MESH_HEADER_SIZE &&
           (entry->payload_flags & 0xff00u) == 0x8000u;
}

static int hwr_mesh_texture_entry(struct hwr_meshes *meshes, uint16_t logical,
                                  struct hwr_das_archive **owner,
                                  size_t *entry_id, uint8_t *source)
{
    struct hwr_das_entry *entry;
    if (logical < HWR_SPRITE_SHARED_FIRST) {
        *owner = meshes->map_archive;
        *entry_id = logical;
        *source = 0;
    } else {
        *owner = meshes->shared_archive;
        *entry_id = (size_t)(logical - HWR_SPRITE_SHARED_FIRST);
        *source = 1;
    }
    if (*owner == NULL || *entry_id >= (*owner)->entry_count)
        return -1;
    entry = &(*owner)->entries[*entry_id];
    if (entry->file_offset == 0 || entry->payload_size == 0 ||
        hwr_das_decode(*owner, *entry_id) != 0 ||
        entry->raw_payload == NULL ||
        (entry->payload_flags & 0xff00u) == 0x8000u)
        return -1;
    if (entry->resource_class != HWR_DAS_CLASS_PLAIN &&
        entry->resource_class != HWR_DAS_CLASS_GROUPED &&
        entry->resource_class != HWR_DAS_CLASS_ANIMATED)
        return -1;
    return 0;
}

static uint32_t hwr_mesh_find_material(const struct hwr_meshes *meshes,
                                       uint8_t source, size_t entry_id,
                                       uint8_t subtexture)
{
    size_t i;
    for (i = 0; i < meshes->material_count; i++) {
        const struct hwr_mesh_material *material = &meshes->materials[i];
        if (material->source == source && material->entry_id == entry_id &&
            material->subtexture == subtexture)
            return (uint32_t)i;
    }
    return HWR_MESH_NO_MATERIAL;
}

static uint32_t hwr_mesh_pack_material(struct hwr_meshes *meshes,
                                       uint16_t logical, uint8_t subtexture,
                                       uint8_t **bytes, size_t *size,
                                       size_t *capacity, int *fatal)
{
    struct hwr_das_archive *owner;
    struct hwr_das_entry *entry;
    struct hwr_mesh_material material;
    size_t entry_id;
    uint8_t source;
    uint8_t key_subtexture;
    uint32_t existing;
    size_t frame_bytes;
    size_t i;

    *fatal = 0;
    if (hwr_mesh_texture_entry(meshes, logical, &owner, &entry_id, &source) != 0)
        return HWR_MESH_NO_MATERIAL;
    entry = &owner->entries[entry_id];
    key_subtexture = entry->resource_class == HWR_DAS_CLASS_GROUPED
                   ? subtexture : 0xffu;
    existing = hwr_mesh_find_material(meshes, source, entry_id, key_subtexture);
    if (existing != HWR_MESH_NO_MATERIAL)
        return existing;

    memset(&material, 0, sizeof material);
    material.entry_id = entry_id;
    material.logical_id = logical;
    material.source = source;
    material.subtexture = key_subtexture;
    material.base_offset = (uint32_t)*size;
    material.origin_x = (int16_t)(uint16_t)(entry->size_prefix & 0xffffu);
    material.origin_y = (int16_t)(uint16_t)(entry->size_prefix >> 16);

    if (entry->resource_class == HWR_DAS_CLASS_PLAIN) {
        frame_bytes = (size_t)entry->width * entry->height;
        if (frame_bytes == 0 || entry->raw_payload_size < 6u ||
            frame_bytes > entry->raw_payload_size - 6u)
            return HWR_MESH_NO_MATERIAL;
        if (*size > UINT32_MAX - frame_bytes ||
            hwr_mesh_append(bytes, size, capacity, entry->raw_payload + 6u,
                            frame_bytes) != 0) {
            *fatal = 1;
            return HWR_MESH_NO_MATERIAL;
        }
        material.width = entry->width;
        material.height = entry->height;
        material.qflags = entry->payload_flags;
        material.frame_count = 1;
        material.frame_bytes = (uint32_t)frame_bytes;
    } else if (entry->resource_class == HWR_DAS_CLASS_ANIMATED) {
        if (entry->frames == NULL || entry->frame_count == 0 ||
            entry->frame_count > UINT16_MAX || entry->frames[0].pixels == NULL ||
            entry->raw_payload_size <= 0x11u)
            return HWR_MESH_NO_MATERIAL;
        material.width = entry->frames[0].width;
        material.height = entry->frames[0].height;
        frame_bytes = (size_t)material.width * material.height;
        if (frame_bytes == 0)
            return HWR_MESH_NO_MATERIAL;
        for (i = 0; i < entry->frame_count; i++) {
            if (entry->frames[i].pixels == NULL ||
                entry->frames[i].width != material.width ||
                entry->frames[i].height != material.height)
                return HWR_MESH_NO_MATERIAL;
        }
        for (i = 0; i < entry->frame_count; i++) {
            if (*size > UINT32_MAX - frame_bytes ||
                hwr_mesh_append(bytes, size, capacity, entry->frames[i].pixels,
                                frame_bytes) != 0) {
                *fatal = 1;
                return HWR_MESH_NO_MATERIAL;
            }
        }
        material.qflags = entry->payload_flags;
        material.frame_count = (uint16_t)entry->frame_count;
        material.frame_bytes = (uint32_t)frame_bytes;
        material.rate = entry->raw_payload[0x11u];
    } else {
        struct hwr_das_texture_view view;
        uint16_t selector;
        size_t word_offset = 8u + (size_t)subtexture * 2u;
        size_t child_offset;
        uint16_t child_word;
        int child_flip;
        if (entry->raw_payload_size < 8u)
            return HWR_MESH_NO_MATERIAL;
        selector = hwr_mesh_u16(entry->raw_payload + 6u);
        /* Mesh faces author an explicit child only for the grouped block's
         * per-face selector class. Directional and global-part groups require
         * live selector state and fail closed here. */
        if ((selector & 0x4000u) == 0)
            return HWR_MESH_NO_MATERIAL;
        if (word_offset > entry->raw_payload_size ||
            entry->raw_payload_size - word_offset < 2u)
            return HWR_MESH_NO_MATERIAL;
        child_word = hwr_mesh_u16(entry->raw_payload + word_offset);
        if (hwr_das_group_child_offset(entry, child_word, &child_offset,
                                       &child_flip) != 0 ||
            hwr_das_group_child_view(entry, child_offset, &view) != 0)
            return HWR_MESH_NO_MATERIAL;
        if (*size > UINT32_MAX - view.pixel_count ||
            hwr_mesh_append(bytes, size, capacity, view.pixels,
                            view.pixel_count) != 0) {
            *fatal = 1;
            return HWR_MESH_NO_MATERIAL;
        }
        material.width = view.width;
        material.height = view.height;
        material.qflags = view.flags;
        material.frame_count = 1;
        material.frame_bytes = (uint32_t)view.pixel_count;
        material.child_flip = (uint8_t)child_flip;
        material.origin_x = 0;
        material.origin_y = 0;
    }

    if (hwr_mesh_reserve_materials(meshes, meshes->material_count + 1u) != 0) {
        *fatal = 1;
        return HWR_MESH_NO_MATERIAL;
    }
    meshes->materials[meshes->material_count] = material;
    return (uint32_t)meshes->material_count++;
}

static int hwr_mesh_parse_resource(struct hwr_meshes *meshes,
                                   struct hwr_das_entry *entry,
                                   struct hwr_mesh_resource *resource,
                                   uint8_t **bytes, size_t *size,
                                   size_t *capacity)
{
    const uint8_t *raw = entry->raw_payload;
    size_t raw_size = entry->raw_payload_size;
    size_t vertex_count;
    size_t vertex_bytes;
    size_t face_section;
    size_t faces_size;
    size_t faces_start;
    size_t faces_end;
    size_t pos;
    size_t first_vertex = meshes->vertex_count;
    size_t first_face = meshes->face_count;
    int expl;
    size_t i;

    if (!hwr_mesh_is_resource(entry))
        return 0;
    vertex_count = hwr_mesh_u16(raw + 0x14u);
    if (vertex_count == 0 || vertex_count > (SIZE_MAX - HWR_MESH_HEADER_SIZE) /
                                             HWR_MESH_VERTEX_SIZE)
        goto invalid;
    vertex_bytes = vertex_count * HWR_MESH_VERTEX_SIZE;
    face_section = HWR_MESH_HEADER_SIZE + vertex_bytes;
    if (face_section > raw_size || raw_size - face_section < 8u)
        goto invalid;
    expl = memcmp(raw + face_section, "EXPL", 4u) == 0;
    if (!expl && memcmp(raw + face_section, "EXP2", 4u) != 0)
        goto invalid;
    faces_size = hwr_mesh_be16(raw + face_section + 6u);
    faces_start = face_section + 8u;
    if (faces_size > raw_size - faces_start)
        goto invalid;
    faces_end = faces_start + faces_size;

    if (hwr_mesh_reserve_vertices(meshes, first_vertex + vertex_count) != 0)
        return hwr_mesh_fail(meshes, "out of memory copying mesh vertices");
    for (i = 0; i < vertex_count; i++) {
        const uint8_t *v = raw + HWR_MESH_HEADER_SIZE +
                           i * HWR_MESH_VERTEX_SIZE;
        struct hwr_mesh_local_vertex *out = &meshes->vertices[first_vertex + i];
        out->x = (int16_t)hwr_mesh_u16(v);
        out->vertical = (int16_t)hwr_mesh_u16(v + 2u);
        out->y = (int16_t)hwr_mesh_u16(v + 4u);
    }
    meshes->vertex_count += vertex_count;

    pos = faces_start;
    while (pos + 4u <= faces_end) {
        struct hwr_mesh_face face;
        size_t record_size;
        size_t record_advance;
        uint16_t edge_count;
        uint16_t texture;
        size_t required;
        size_t edge;
        int fatal;
        if (raw[pos] == 0 && raw[pos + 1u] == 0 &&
            raw[pos + 2u] == 0 && raw[pos + 3u] == 0)
            break;
        if (faces_end - pos < HWR_MESH_FACE_HEADER_SIZE)
            goto invalid;
        record_size = hwr_mesh_u16(raw + pos);
        record_advance = record_size +
                         ((size_t)hwr_mesh_u16(raw + pos + 2u) << 4u);
        edge_count = hwr_mesh_u16(raw + pos + 0x34u);
        if (edge_count != 0 && edge_count != 3u && edge_count != 4u)
            goto invalid;
        required = HWR_MESH_FACE_HEADER_SIZE + ((size_t)edge_count + 1u) * 2u;
        if (record_size < required || record_advance < record_size ||
            record_advance > faces_end - pos)
            goto invalid;
        memset(&face, 0, sizeof face);
        texture = expl ? raw[pos + 0x0cu]
                       : hwr_mesh_be16(raw + pos + 0x0cu);
        face.texture_id = texture;
        face.flags = hwr_mesh_u16(raw + pos + 0x16u);
        memcpy(face.packed_control, raw + pos + 0x18u,
               sizeof face.packed_control);
        memcpy(face.packed_a, raw + pos + 0x1cu,
               sizeof face.packed_a);
        memcpy(face.packed_b, raw + pos + 0x20u,
               sizeof face.packed_b);
        face.subtexture = raw[pos + 0x1cu];
        face.scale = raw[pos + 0x0fu];
        face.shade_seed = raw[pos + (expl ? 0x0du : 0x0eu)];
        face.extent_u = hwr_mesh_u16(raw + pos + 0x24u);
        face.extent_v = hwr_mesh_u16(raw + pos + 0x26u);
        face.kind = edge_count == 0 ? HWR_MESH_FACE_PIN
                  : edge_count == 3 ? HWR_MESH_FACE_TRIANGLE
                                    : HWR_MESH_FACE_QUAD;
        for (edge = 0; edge < (edge_count != 0 ? edge_count : 1u); edge++) {
            uint16_t encoded = hwr_mesh_u16(
                raw + pos + HWR_MESH_FACE_HEADER_SIZE + edge * 2u);
            uint16_t index;
            if ((encoded & 0x0fu) != 0)
                goto invalid;
            index = (uint16_t)(encoded >> 4);
            if (index >= vertex_count)
                goto invalid;
            face.vertex[edge] = index;
        }
        if (edge_count != 0) {
            uint16_t closing = hwr_mesh_u16(
                raw + pos + HWR_MESH_FACE_HEADER_SIZE + edge_count * 2u);
            if ((closing & 0x0fu) != 0 ||
                (uint16_t)(closing >> 4) != face.vertex[0])
                goto invalid;
        }
        face.material_index = HWR_MESH_NO_MATERIAL;
        if ((texture & 0xff00u) != 0xff00u) {
            uint16_t effective = edge_count == 0
                               ? (uint16_t)(texture + 0x1000u) : texture;
            face.material_index = hwr_mesh_pack_material(
                meshes, effective, face.subtexture, bytes, size, capacity,
                &fatal);
            if (fatal)
                return hwr_mesh_fail(meshes,
                                     "out of memory packing mesh texture %u",
                                     effective);
        }
        if (hwr_mesh_reserve_faces(meshes, meshes->face_count + 1u) != 0)
            return hwr_mesh_fail(meshes, "out of memory copying mesh faces");
        meshes->faces[meshes->face_count++] = face;
        pos += record_advance;
    }
    resource->first_vertex = first_vertex;
    resource->vertex_count = vertex_count;
    resource->first_face = first_face;
    resource->face_count = meshes->face_count - first_face;
    resource->present = resource->face_count != 0;
    if (resource->present)
        meshes->stat_resources++;
    return 0;

invalid:
    meshes->vertex_count = first_vertex;
    meshes->face_count = first_face;
    memset(resource, 0, sizeof *resource);
    meshes->stat_invalid++;
    return 0;
}

static int hwr_mesh_scan_archive(struct hwr_meshes *meshes,
                                 struct hwr_das_archive *archive,
                                 struct hwr_mesh_resource *directory,
                                 uint8_t **bytes, size_t *size,
                                 size_t *capacity)
{
    size_t id;
    if (archive == NULL)
        return 0;
    for (id = 0; id < archive->entry_count; id++) {
        struct hwr_das_entry *entry;
        if (hwr_das_decode(archive, id) != 0)
            continue;
        entry = &archive->entries[id];
        if (!hwr_mesh_is_resource(entry))
            continue;
        if (hwr_mesh_parse_resource(meshes, entry, &directory[id],
                                    bytes, size, capacity) != 0)
            return -1;
    }
    return 0;
}

int hwr_meshes_pack(struct hwr_meshes *meshes,
                    struct hwr_das_archive *map_archive,
                    struct hwr_das_archive *shared_archive,
                    uint8_t **bytes, size_t *size, size_t *capacity)
{
    if (meshes == NULL || bytes == NULL || size == NULL || capacity == NULL)
        return -1;
    hwr_meshes_clear(meshes);
    if (!hwr_meshes_enabled())
        return 0;
    meshes->map_archive = map_archive;
    meshes->shared_archive = shared_archive;
    meshes->map_dir_count = map_archive != NULL ? map_archive->entry_count : 0;
    meshes->shared_dir_count =
        shared_archive != NULL ? shared_archive->entry_count : 0;
    if (meshes->map_dir_count != 0) {
        meshes->map_dir = (struct hwr_mesh_resource *)calloc(
            meshes->map_dir_count, sizeof *meshes->map_dir);
        if (meshes->map_dir == NULL)
            return hwr_mesh_fail(meshes,
                                 "out of memory creating map mesh directory");
    }
    if (meshes->shared_dir_count != 0) {
        meshes->shared_dir = (struct hwr_mesh_resource *)calloc(
            meshes->shared_dir_count, sizeof *meshes->shared_dir);
        if (meshes->shared_dir == NULL)
            return hwr_mesh_fail(meshes,
                                 "out of memory creating shared mesh directory");
    }
    if (hwr_mesh_scan_archive(meshes, map_archive, meshes->map_dir,
                              bytes, size, capacity) != 0 ||
        hwr_mesh_scan_archive(meshes, shared_archive, meshes->shared_dir,
                              bytes, size, capacity) != 0)
        return -1;
    meshes->built = 1;
    return 0;
}

static int32_t hwr_mesh_sincos(const struct roth_api_v1 *api, uint32_t angle)
{
    return (int16_t)api->game_ram->u16(
        VA_G_SINCOS_TABLE + (angle & 0x1ffu) * 2u);
}

static int32_t hwr_mesh_sar14(int64_t value)
{
    return value >= 0 ? (int32_t)(value >> 14)
                      : -(int32_t)(((uint64_t)(-value) + 0x3fffu) >> 14);
}

static int16_t hwr_mesh_clamp_i16(int32_t value)
{
    if (value < INT16_MIN)
        return INT16_MIN;
    if (value > INT16_MAX)
        return INT16_MAX;
    return (int16_t)value;
}

static void hwr_mesh_world_vertex(const struct hwr_object *object,
                                  const struct hwr_mesh_local_vertex *local,
                                  int32_t facing_sin, int32_t facing_cos,
                                  struct hwr_gl_vertex *vertex)
{
    int32_t ox = hwr_mesh_sar14((int64_t)facing_cos * local->x +
                                (int64_t)facing_sin * local->y);
    int32_t oy = hwr_mesh_sar14((int64_t)facing_cos * local->y -
                                (int64_t)facing_sin * local->x);
    memset(vertex, 0, sizeof *vertex);
    vertex->x = (int16_t)((uint16_t)object->world_x + (uint16_t)ox);
    vertex->y = (int16_t)((uint16_t)object->world_y + (uint16_t)oy);
    vertex->z = (int16_t)((uint16_t)object->world_z +
                          (uint16_t)local->vertical);
}

static double hwr_mesh_depth(const struct hwr_gl_camera *camera,
                             const struct hwr_gl_vertex *vertex,
                             double *view_x)
{
    int32_t dx = (int16_t)((uint16_t)vertex->x +
                           (uint16_t)camera->view_offset_x);
    int32_t dy = (int16_t)((uint16_t)vertex->y +
                           (uint16_t)camera->view_offset_y);
    *view_x = ((double)camera->yaw_sin * dx +
               (double)camera->yaw_cos * dy) / 16384.0;
    return ((double)camera->yaw_sin * dy -
            (double)camera->yaw_cos * dx) / 16384.0;
}

static double hwr_mesh_screen_cross(const double *sx, const double *sy,
                                    size_t a, size_t b, size_t c)
{
    return (sx[b] - sx[a]) * (sy[c] - sy[a]) -
           (sy[b] - sy[a]) * (sx[c] - sx[a]);
}

static int hwr_mesh_face_visible(const struct hwr_gl_vertex *vertex,
                                 size_t count, uint16_t flags,
                                 const struct hwr_gl_camera *camera)
{
    double sx[4], sy[4], depth[4];
    size_t in_front = 0;
    size_t i;
    double cross;
    for (i = 0; i < count; i++) {
        double vx;
        depth[i] = hwr_mesh_depth(camera, &vertex[i], &vx);
        if (depth[i] >= HWR_MESH_NEAR_DEPTH)
            in_front++;
        if (depth[i] == 0.0) {
            sx[i] = sy[i] = 0.0;
        } else {
            sx[i] = vx / depth[i];
            sy[i] = ((double)camera->eye_z - vertex[i].z) / depth[i];
        }
    }
    if (in_front == 0)
        return 0;
    if (in_front != count)
        return 1;
    if ((flags & 0x10u) != 0 && (flags & 0x200u) == 0) {
        if ((flags & 0x20u) != 0) {
            int32_t vertical = (int32_t)vertex[1].z - camera->eye_z;
            if ((flags & 0x80u) != 0)
                vertical = -vertical;
            return vertical > 0;
        }
        if (sx[0] < sx[1])
            return 1;
        if (sx[0] > sx[1])
            return 0;
        return count >= 4u ? sx[3] < sx[2] : sx[0] < sx[2];
    }
    cross = count >= 4u ? hwr_mesh_screen_cross(sx, sy, 0u, 1u, 3u)
                        : hwr_mesh_screen_cross(sx, sy, 0u, 1u, 2u);
    /* bbox_winding_cross stores the negative of this conventional cross and
     * admits its negative result, hence a positive conventional cross. */
    if (cross != 0.0)
        return cross > 0.0;
    if (count < 4u)
        return 0;
    cross = hwr_mesh_screen_cross(sx, sy, 0u, 2u, 3u);
    if (cross != 0.0)
        return cross > 0.0;
    cross = hwr_mesh_screen_cross(sx, sy, 1u, 2u, 3u);
    return cross > 0.0;
}

static int hwr_mesh_reserve_output(struct hwr_gl_vertex **vertices,
                                   size_t count, size_t *capacity, size_t add)
{
    struct hwr_gl_vertex *grown;
    size_t wanted;
    size_t next;
    if (add > SIZE_MAX - count)
        return -1;
    wanted = count + add;
    if (wanted <= *capacity)
        return 0;
    next = *capacity != 0 ? *capacity : 512u;
    while (next < wanted) {
        if (next > SIZE_MAX / 2u)
            return -1;
        next *= 2u;
    }
    grown = (struct hwr_gl_vertex *)realloc(*vertices, next * sizeof *grown);
    if (grown == NULL)
        return -1;
    *vertices = grown;
    *capacity = next;
    return 0;
}

static int hwr_mesh_push_pending(struct hwr_mesh_pending **pending,
                                 size_t *count, size_t *capacity,
                                 const struct hwr_gl_vertex *vertices,
                                 size_t vertex_count, double depth)
{
    struct hwr_mesh_pending *grown;
    struct hwr_mesh_pending *item;
    size_t next;
    if (*count == *capacity) {
        next = *capacity != 0 ? *capacity * 2u : 128u;
        grown = (struct hwr_mesh_pending *)realloc(*pending,
                                                   next * sizeof *grown);
        if (grown == NULL)
            return -1;
        *pending = grown;
        *capacity = next;
    }
    item = &(*pending)[(*count)++];
    memcpy(item->vertices, vertices, vertex_count * sizeof *vertices);
    item->count = (uint8_t)vertex_count;
    item->depth = depth;
    return 0;
}

static int hwr_mesh_pending_compare(const void *left, const void *right)
{
    const struct hwr_mesh_pending *a = (const struct hwr_mesh_pending *)left;
    const struct hwr_mesh_pending *b = (const struct hwr_mesh_pending *)right;
    return a->depth < b->depth ? 1 : a->depth > b->depth ? -1 : 0;
}

static int hwr_mesh_finish_pending(struct hwr_meshes *meshes,
                                   struct hwr_mesh_pending *pending,
                                   size_t pending_count)
{
    size_t i;
    if (pending_count == 0)
        return 0;
    qsort(pending, pending_count, sizeof *pending, hwr_mesh_pending_compare);
    if (hwr_mesh_reserve_output(&meshes->translucent,
                                meshes->translucent_count,
                                &meshes->translucent_capacity,
                                pending_count * 6u) != 0)
        return -1;
    if (pending_count > meshes->run_capacity) {
        uint32_t *grown = (uint32_t *)realloc(
            meshes->runs, pending_count * sizeof *grown);
        if (grown == NULL)
            return -1;
        meshes->runs = grown;
        meshes->run_capacity = pending_count;
    }
    for (i = 0; i < pending_count; i++) {
        memcpy(meshes->translucent + meshes->translucent_count,
               pending[i].vertices,
               (size_t)pending[i].count * sizeof *meshes->translucent);
        meshes->translucent_count += pending[i].count;
        meshes->runs[meshes->run_count++] = pending[i].count;
    }
    return 0;
}

static uint32_t hwr_mesh_material_offset(const struct hwr_mesh_material *material)
{
    return material->base_offset +
           (uint32_t)material->frame_index * material->frame_bytes;
}

static int hwr_mesh_animations_enabled(void)
{
    static int state = -1;
    if (state < 0) {
        const char *setting = getenv("HWR_ANIMATE");
        state = setting != NULL && setting[0] == '0' ? 0 : 1;
    }
    return state;
}

/* The software face resolver advances a delta animation only when that face is
 * drawn. Mesh replacement suppresses that call, so retain the same per-texture
 * timer here and step each deduplicated material at most once per render tick. */
static void hwr_mesh_step_animations(struct hwr_meshes *meshes,
                                     const struct roth_api_v1 *api)
{
    uint16_t tick;
    uint8_t scale;
    size_t i;
    if (!hwr_mesh_animations_enabled())
        return;
    tick = api->game_ram->u16(VA_G_RENDER_TICK);
    scale = (uint8_t)api->game_ram->u32(VA_G_FRAME_TIME_SCALE);
    for (i = 0; i < meshes->material_count; i++) {
        struct hwr_mesh_material *material = &meshes->materials[i];
        uint8_t step;
        if (material->frame_count <= 1u)
            continue;
        if (!material->animation_started) {
            material->animation_started = 1;
            material->frame_index = 0;
            material->timer = material->rate;
            material->last_tick = tick;
            continue;
        }
        if (material->last_tick == tick)
            continue;
        material->last_tick = tick;
        if (material->rate != 0xffu) {
            material->timer = (uint8_t)(material->timer - scale);
            if (material->timer < 0x10u)
                continue;
            step = (uint8_t)((material->rate & 0x1fu) + 1u);
            material->timer = (uint8_t)(material->timer + step);
            if ((material->timer & 0x80u) != 0)
                material->timer = 0;
        }
        material->frame_index++;
        if (material->frame_index >= material->frame_count)
            material->frame_index = 0;
    }
}

static uint8_t hwr_mesh_light(uint8_t shade_seed,
                              const struct hwr_object *object,
                              const struct hwr_world *world,
                              uint8_t *sector_flags)
{
    uint8_t light = 0;
    *sector_flags = 0;
    if (object->sector_index != HWR_OBJECT_NO_SECTOR &&
        object->sector_index < world->sector_count) {
        light = world->sectors[object->sector_index].light;
        *sector_flags = world->sectors[object->sector_index].flags;
    }
    return shade_seed != 0 && light != 0
         ? (uint8_t)((uint8_t)(shade_seed - 0x80u) + light)
         : shade_seed;
}

static void hwr_mesh_finish_vertex(struct hwr_gl_vertex *vertex,
                                   const struct hwr_mesh_face *face,
                                   const struct hwr_mesh_material *material,
                                   uint8_t light, uint8_t sector_flags,
                                   float s, float t, int use_face_flip)
{
    uint8_t flags = 0;
    vertex->wall_s = s;
    vertex->wall_top = t;
    vertex->light = light;
    vertex->sector_flags = sector_flags;
    vertex->flat_mapping_flags = HWR_GL_MAPPING_MESH;
    if ((face->texture_id & 0xff00u) == 0xff00u) {
        flags |= HWR_GL_MATERIAL_DIRECT;
        vertex->direct_index = (uint8_t)face->texture_id;
        vertex->texture_width = 1;
        vertex->texture_height = 1;
    } else if (material != NULL) {
        vertex->texture_offset = hwr_mesh_material_offset(material);
        vertex->texture_width = material->width;
        vertex->texture_height = material->height;
        if ((face->flags & 1u) != 0)
            flags |= HWR_GL_MATERIAL_MASKED;
        if (((use_face_flip && (face->flags & 2u) != 0) !=
             (material->child_flip != 0)))
            flags |= HWR_GL_MATERIAL_HFLIP;
    }
    if ((face->flags & 0x200u) != 0)
        flags |= HWR_GL_MATERIAL_FLAT;
    vertex->material_flags = flags;
}

static int hwr_mesh_emit_face(struct hwr_meshes *meshes,
                              const struct hwr_mesh_resource *resource,
                              const struct hwr_mesh_face *face,
                              const struct hwr_object *object,
                              const struct hwr_world *world,
                              const struct hwr_gl_camera *camera,
                              int32_t facing_sin, int32_t facing_cos,
                              struct hwr_mesh_pending **pending,
                              size_t *pending_count,
                              size_t *pending_capacity)
{
    struct hwr_gl_vertex corner[4];
    struct hwr_gl_vertex triangles[6];
    const struct hwr_mesh_material *material = NULL;
    float s[4], t[4];
    uint8_t light;
    uint8_t sector_flags;
    size_t count = face->kind == HWR_MESH_FACE_TRIANGLE ? 3u : 4u;
    size_t output_count = count == 3u ? 3u : 6u;
    int translucent = 0;
    size_t i;

    if ((face->texture_id & 0xff00u) != 0xff00u) {
        if ((face->flags & 8u) == 0)
            return 0;
        if (face->material_index == HWR_MESH_NO_MATERIAL ||
            face->material_index >= meshes->material_count) {
            meshes->stat_atlas_miss++;
            return 0;
        }
        material = &meshes->materials[face->material_index];
    }
    for (i = 0; i < count; i++) {
        const struct hwr_mesh_local_vertex *local =
            &meshes->vertices[resource->first_vertex + face->vertex[i]];
        hwr_mesh_world_vertex(object, local, facing_sin, facing_cos, &corner[i]);
    }
    if (!hwr_mesh_face_visible(corner, count, face->flags, camera)) {
        meshes->stat_culled++;
        return 0;
    }
    light = hwr_mesh_light(face->shade_seed, object, world, &sector_flags);
    if ((face->flags & 0x1000u) != 0) {
        for (i = 0; i < count; i++) {
            s[i] = (float)(((uint16_t)(face->packed_control[i] & 0x80u)
                            << 1u) | face->packed_a[i]);
            t[i] = (float)(((uint16_t)(face->packed_control[i] & 0x40u)
                            << 2u) | face->packed_b[i]);
        }
    } else if ((face->flags & 0x20u) != 0) {
        uint8_t scale_h = (uint8_t)(face->scale & 0x0fu);
        uint8_t scale_w = (uint8_t)(face->scale >> 4);
        float u = material != NULL ? (float)material->height : 1.0f;
        float v = material != NULL ? (float)material->width : 1.0f;
        if (scale_h != 0)
            u *= (float)(scale_h + 1u);
        if (scale_w != 0)
            v *= (float)(scale_w + 1u);
        s[0] = 0.0f; s[1] = u; s[2] = u; s[3] = 0.0f;
        t[0] = 0.0f; t[1] = 0.0f; t[2] = v; t[3] = v;
    } else if ((face->flags & 0x200u) != 0) {
        float u = material != NULL ? (float)material->height : 1.0f;
        float v = material != NULL ? (float)material->width : 1.0f;
        s[0] = u; s[1] = 0.0f; s[2] = 0.0f; s[3] = u;
        t[0] = 0.0f; t[1] = 0.0f; t[2] = v; t[3] = v;
    } else {
        float row = material != NULL ? (float)material->height : 1.0f;
        float column = material != NULL ? (float)material->width : 1.0f;
        if ((face->flags & 0x100u) != 0) {
            row = 0.5f * face->extent_v;
            column = 0.5f * face->extent_u;
        }
        /*
         * Ordinary faces take the wall driver, whose left edge starts at
         * stored-image row zero.  The row coordinate therefore follows
         * {0,max,max,0}, while the column follows {0,0,max,max}.  DAS face
         * textures are authored transposed, so preserving those axes (rather
         * than conventional image UVs) turns TREE/TREE1 and the other wall
         * faces upright.  RENDER_FLAG_1 bit 1 is applied below as the wall
         * driver's row-coordinate reversal.
         */
        s[0] = 0.0f; s[1] = row;  s[2] = row;    s[3] = 0.0f;
        t[0] = 0.0f; t[1] = 0.0f; t[2] = column; t[3] = column;
    }
    for (i = 0; i < count; i++)
        hwr_mesh_finish_vertex(&corner[i], face, material, light,
                               sector_flags, s[i], t[i],
                               (face->flags & 0x1000u) == 0);
    triangles[0] = corner[0];
    triangles[1] = corner[1];
    triangles[2] = corner[2];
    if (count == 4u) {
        triangles[3] = corner[0];
        triangles[4] = corner[2];
        triangles[5] = corner[3];
    }
    translucent = material != NULL && (face->flags & 1u) != 0 &&
                  (material->qflags & 0x400u) != 0;
    if (translucent) {
        double depth_sum = 0.0;
        double ignored;
        for (i = 0; i < count; i++) {
            triangles[i].material_flags |= HWR_GL_MATERIAL_TRANSLUCENT;
            depth_sum += hwr_mesh_depth(camera, &corner[i], &ignored);
        }
        if (count == 4u) {
            triangles[3].material_flags |= HWR_GL_MATERIAL_TRANSLUCENT;
            triangles[4].material_flags |= HWR_GL_MATERIAL_TRANSLUCENT;
            triangles[5].material_flags |= HWR_GL_MATERIAL_TRANSLUCENT;
        }
        if (hwr_mesh_push_pending(pending, pending_count, pending_capacity,
                                  triangles, output_count,
                                  depth_sum / (double)count) != 0)
            return -1;
    } else {
        if (hwr_mesh_reserve_output(&meshes->opaque, meshes->opaque_count,
                                    &meshes->opaque_capacity,
                                    output_count) != 0)
            return -1;
        memcpy(meshes->opaque + meshes->opaque_count, triangles,
               output_count * sizeof *triangles);
        meshes->opaque_count += output_count;
    }
    meshes->stat_faces++;
    return 0;
}

static int hwr_mesh_emit_pin(struct hwr_meshes *meshes,
                             const struct hwr_mesh_resource *resource,
                             const struct hwr_mesh_face *face,
                             const struct hwr_object *object,
                             const struct hwr_world *world,
                             const struct hwr_gl_camera *camera,
                             int32_t facing_sin, int32_t facing_cos,
                             double hscale, double vscale)
{
    const struct hwr_mesh_material *material;
    const struct hwr_mesh_local_vertex *local;
    struct hwr_gl_vertex pin;
    struct hwr_gl_vertex tl, tr, bl, br;
    uint8_t light, sector_flags;
    double famod = 1.0;
    double half_w;
    double vext;
    double cx, cy, rx, ry;
    int cl = 7;
    int32_t disp;
    int32_t z_top, z_bottom;
    double depth, view_x;

    if (face->material_index == HWR_MESH_NO_MATERIAL ||
        face->material_index >= meshes->material_count) {
        meshes->stat_atlas_miss++;
        return 0;
    }
    material = &meshes->materials[face->material_index];
    local = &meshes->vertices[resource->first_vertex + face->vertex[0]];
    hwr_mesh_world_vertex(object, local, facing_sin, facing_cos, &pin);
    depth = hwr_mesh_depth(camera, &pin, &view_x);
    (void)view_x;
    if (depth < HWR_MESH_NEAR_DEPTH) {
        meshes->stat_culled++;
        return 0;
    }
    if ((material->qflags & 0x80u) != 0) {
        if ((material->qflags & 0x6000u) != 0) {
            int tier = (int)(((uint32_t)(material->qflags << 3) |
                              (material->qflags >> 13)) & 3u);
            famod = (double)(1u << tier);
            cl = 7 + tier;
        } else {
            famod = 0.5;
            cl = 6;
        }
    }
    half_w = (double)((uint32_t)((uint32_t)material->height * 2u) << cl) *
             hscale;
    vext = 2.0 * material->width * famod * vscale;
    disp = (int32_t)((2.0 * (material->qflags & 0x0fu) +
                      material->origin_y) * vscale);
    if ((material->qflags & 0x10u) != 0) {
        z_top = (int32_t)pin.z + disp;
        z_bottom = z_top - (int32_t)(vext + 0.5);
    } else {
        z_bottom = (int32_t)pin.z - disp;
        z_top = z_bottom + (int32_t)(vext + 0.5);
    }
    cx = (double)pin.x +
         (material->child_flip ? -material->origin_x : material->origin_x) *
         (double)camera->yaw_sin / 16384.0;
    cy = (double)pin.y +
         (material->child_flip ? -material->origin_x : material->origin_x) *
         (double)camera->yaw_cos / 16384.0;
    rx = half_w * camera->yaw_sin / 16384.0;
    ry = half_w * camera->yaw_cos / 16384.0;
    light = hwr_mesh_light(face->shade_seed, object, world, &sector_flags);
    memset(&tl, 0, sizeof tl);
    tl.x = hwr_mesh_clamp_i16((int32_t)(cx - rx));
    tl.y = hwr_mesh_clamp_i16((int32_t)(cy - ry));
    tl.z = hwr_mesh_clamp_i16(z_top);
    tr = bl = br = tl;
    tr.x = hwr_mesh_clamp_i16((int32_t)(cx + rx));
    tr.y = hwr_mesh_clamp_i16((int32_t)(cy + ry));
    br.x = tr.x; br.y = tr.y; br.z = hwr_mesh_clamp_i16(z_bottom);
    bl.z = hwr_mesh_clamp_i16(z_bottom);
    hwr_mesh_finish_vertex(&tl, face, material, light, sector_flags,
                           0.0f, 0.0f, 0);
    hwr_mesh_finish_vertex(&tr, face, material, light, sector_flags,
                           (float)material->height, 0.0f, 0);
    hwr_mesh_finish_vertex(&bl, face, material, light, sector_flags,
                           0.0f, (float)material->width, 0);
    hwr_mesh_finish_vertex(&br, face, material, light, sector_flags,
                           (float)material->height, (float)material->width,
                           0);
    tl.material_flags |= HWR_GL_MATERIAL_MASKED;
    tr.material_flags |= HWR_GL_MATERIAL_MASKED;
    bl.material_flags |= HWR_GL_MATERIAL_MASKED;
    br.material_flags |= HWR_GL_MATERIAL_MASKED;
    if (hwr_mesh_reserve_output(&meshes->opaque, meshes->opaque_count,
                                &meshes->opaque_capacity, 6u) != 0)
        return -1;
    meshes->opaque[meshes->opaque_count++] = tl;
    meshes->opaque[meshes->opaque_count++] = tr;
    meshes->opaque[meshes->opaque_count++] = br;
    meshes->opaque[meshes->opaque_count++] = tl;
    meshes->opaque[meshes->opaque_count++] = br;
    meshes->opaque[meshes->opaque_count++] = bl;
    meshes->stat_pins++;
    return 0;
}

int hwr_meshes_emit(struct hwr_meshes *meshes,
                    const struct hwr_objects *objects,
                    const struct hwr_world *world,
                    const struct hwr_gl_camera *camera,
                    const uint8_t *visible, size_t visible_count,
                    int pvs_valid,
                    const struct roth_api_v1 *api)
{
    double hscale = 0.0;
    double vscale = 0.0;
    double focal_x, focal_y, xscale, pscale;
    struct hwr_mesh_pending *pending = NULL;
    size_t pending_count = 0;
    size_t pending_capacity = 0;
    uint16_t yaw;
    size_t i;
    int log = getenv("HWR_MESH_LOG") != NULL;
    static uint32_t frame_no;

    if (meshes == NULL)
        return 0;
    meshes->opaque_count = 0;
    meshes->translucent_count = 0;
    meshes->run_count = 0;
    meshes->stat_objects = 0;
    meshes->stat_faces = 0;
    meshes->stat_pins = 0;
    meshes->stat_culled = 0;
    meshes->stat_atlas_miss = 0;
    if (!meshes->built || objects == NULL || !objects->valid ||
        world == NULL || camera == NULL || api == NULL || api->game_ram == NULL)
        return 0;
    hwr_mesh_step_animations(meshes, api);
    focal_x = camera->focal_x;
    focal_y = camera->focal_y;
    xscale = (double)(int32_t)api->game_ram->u32(VA_G_VIEW_PARAMS_XSCALE);
    pscale = (double)(int32_t)api->game_ram->u32(VA_G_PERSPECTIVE_SCALE);
    if (focal_x > 0.0 && focal_y > 0.0 && xscale > 0.0 && pscale > 0.0) {
        hscale = xscale / (focal_x * 256.0);
        vscale = pscale / (focal_y * 256.0);
    }
    yaw = api->game_ram->u16(VA_G_SPRITE_VIEW_ANGLE);

    for (i = 0; i < objects->count; i++) {
        const struct hwr_object *object = &objects->items[i];
        const struct hwr_mesh_resource *resource = NULL;
        struct hwr_sprite_lookup lookup;
        struct hwr_gl_vertex center;
        double center_view_x;
        double center_depth;
        int32_t correction;
        int32_t facing_sin, facing_cos;
        size_t face_index;
        if (object->source != HWR_OBJECT_MAP)
            continue;
        if (pvs_valid && object->sector_index != HWR_OBJECT_NO_SECTOR &&
            object->sector_index < visible_count &&
            !visible[object->sector_index])
            continue;
        memset(&center, 0, sizeof center);
        center.x = object->world_x;
        center.y = object->world_y;
        center.z = object->world_z;
        center_depth = hwr_mesh_depth(camera, &center, &center_view_x);
        correction = center_depth != 0.0
                   ? (int32_t)(center_view_x * 64.0 / center_depth) : 0;
        if (hwr_sprites_resolve(api, meshes->map_archive,
                                meshes->shared_archive, object->graphic_id,
                                object->facing, yaw, correction,
                                object->backref, &lookup) != 0)
            continue;
        if (lookup.archive == meshes->map_archive &&
            lookup.entry_id < meshes->map_dir_count &&
            meshes->map_dir[lookup.entry_id].present)
            resource = &meshes->map_dir[lookup.entry_id];
        else if (lookup.archive == meshes->shared_archive &&
                 lookup.entry_id < meshes->shared_dir_count &&
                 meshes->shared_dir[lookup.entry_id].present)
            resource = &meshes->shared_dir[lookup.entry_id];
        if (resource == NULL)
            continue;
        meshes->stat_objects++;
        facing_sin = hwr_mesh_sincos(api, 2u * object->facing);
        facing_cos = hwr_mesh_sincos(api, 2u * object->facing + 0x80u);
        for (face_index = 0; face_index < resource->face_count; face_index++) {
            const struct hwr_mesh_face *face =
                &meshes->faces[resource->first_face + face_index];
            int result;
            if (face->kind == HWR_MESH_FACE_PIN) {
                if (hscale == 0.0 || vscale == 0.0)
                    continue;
                result = hwr_mesh_emit_pin(meshes, resource, face, object,
                                           world, camera, facing_sin, facing_cos,
                                           hscale, vscale);
            } else {
                result = hwr_mesh_emit_face(meshes, resource, face, object,
                                            world, camera, facing_sin, facing_cos,
                                            &pending, &pending_count,
                                            &pending_capacity);
            }
            if (result != 0) {
                free(pending);
                return hwr_mesh_fail(meshes,
                                     "out of memory streaming mesh faces");
            }
        }
    }
    if (hwr_mesh_finish_pending(meshes, pending, pending_count) != 0) {
        free(pending);
        return hwr_mesh_fail(meshes,
                             "out of memory sorting translucent mesh faces");
    }
    free(pending);
    if (log && (frame_no++ % 120u) == 0u)
        fprintf(stderr,
                "[renderer_overhaul] meshes: objects=%u faces=%u pins=%u culled=%u atlas-miss=%u opaque-v=%u trans-v=%u runs=%u\n",
                (unsigned)meshes->stat_objects, (unsigned)meshes->stat_faces,
                (unsigned)meshes->stat_pins, (unsigned)meshes->stat_culled,
                (unsigned)meshes->stat_atlas_miss,
                (unsigned)meshes->opaque_count,
                (unsigned)meshes->translucent_count,
                (unsigned)meshes->run_count);
    return 0;
}
