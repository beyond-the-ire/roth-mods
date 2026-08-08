#include "hwr_sun.h"

#include "hwr_gl.h"
#include "hwr_look.h"
#include "hwr_schema.h"
#include "hwr_settings.h"
#include "hwr_sprites.h"
#include "hwr_sun_math.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The world side of the sun/moon key light.
 *
 * Three jobs, each with one owner here: the per-map sky census (whether the
 * feature exists on this map at all), the caster geometry (a static slice
 * built from the map's own mesh, and a dynamic slice of object cards and
 * decoded mesh props rebuilt every frame), and the camera-centred
 * texel-snapped moving window whose matrices the GL layer executes. The
 * policy arithmetic itself -- direction, window, matrices, the card law --
 * lives in hwr_sun_math.h so the offline twin compiles the same lines. */

/* Engine globals read the same way the billboard and mesh emitters read
 * them; the values are documented beside those emitters. */
#define VA_G_SINCOS_TABLE 0x72080u
#define VA_G_VIEW_PARAMS_XSCALE 0x8527cu
#define VA_G_PERSPECTIVE_SCALE 0x85288u

/* Texture-mapping flag bits, as hwr_scene.c names them; duplicated because
 * they are byte facts about the map format rather than anybody's API. */
#define HWR_SUN_TEX_TRANSPARENT 0x01u
#define HWR_SUN_TEX_IMAGE_FIT 0x04u
#define HWR_SUN_TEX_HALF_PIXEL 0x20u

/* The resolved DAS block's class bits the emitters key on. */
#define HWR_SUN_MESH_FLAG 0x8000u
#define HWR_SUN_DRAW_NOTHING_FLAG 0x20u

/* What the rows have worked out, held whole between applies exactly as the
 * light rig is: the settings layer answers where a change is safe and the
 * frame reads one object. */
struct hwr_sun_rows {
    int modern;
    int enabled;
    float color[3];
    float intensity;
    float elevation_deg;
    float yaw_deg;
    int object_shadows;
    int quality;
    float softness;
    float span;
};

static struct hwr_sun_rows g_rows;
static int g_rows_valid;

/* The staged map: its census, and the generation counter that says which map
 * the static slice describes. */
static size_t g_sky_faces;
static int g_staged;
static uint32_t g_map_generation;

/* The static caster slice: its own mesh build, the converted vertices, and
 * the currency stamp (map generation + a hash of every mutable field the
 * casters are built from -- door poses, lift heights, live material ids). */
static struct hwr_scene_mesh g_mesh;
static struct hwr_gl_sun_vertex *g_static;
static size_t g_static_count;
static size_t g_static_capacity;
static int g_static_built;
static uint32_t g_static_generation;
static uint32_t g_static_hash;
static int g_static_upload_due;
static unsigned long g_static_builds;

/* The dynamic slice, rebuilt every frame the sun is live. */
static struct hwr_gl_sun_vertex *g_dynamic;
static size_t g_dynamic_count;
static size_t g_dynamic_capacity;

/* The moving window and the direction it was built for. */
static struct hwr_sunm_window g_window;
static float g_dir[3];
static float g_right[3];
static float g_up[3];
static int g_dir_valid;

/* HWR_SUN_DEBUG=1 readout; -1 until first read. */
static int g_debug = -1;

static int hwr_sun_debug(void)
{
    if (g_debug < 0) {
        const char *value = getenv("HWR_SUN_DEBUG");

        g_debug = value != NULL && value[0] != '0' && value[0] != 0;
    }
    return g_debug;
}

/* ------------------------------------------------------------------ rows */

static int hwr_sun_row_bool(const char *key,
                            const struct hwr_settings *settings, int fallback)
{
    const struct hwr_setting_desc *desc = hwr_schema_find(key);

    return desc != NULL ? hwr_look_effective(desc, settings).b != 0 : fallback;
}

static float hwr_sun_row_float(const char *key,
                               const struct hwr_settings *settings,
                               float fallback)
{
    const struct hwr_setting_desc *desc = hwr_schema_find(key);

    return desc != NULL ? hwr_look_effective(desc, settings).f : fallback;
}

static int32_t hwr_sun_row_enum(const char *key,
                                const struct hwr_settings *settings,
                                int32_t fallback)
{
    const struct hwr_setting_desc *desc = hwr_schema_find(key);

    return desc != NULL ? hwr_look_effective(desc, settings).e : fallback;
}

static void hwr_sun_row_color(const char *key,
                              const struct hwr_settings *settings,
                              uint32_t fallback, float out[3])
{
    const struct hwr_setting_desc *desc = hwr_schema_find(key);
    uint32_t rgb = desc != NULL ? hwr_look_effective(desc, settings).rgb
                                : fallback;

    out[0] = (float)((rgb >> 16) & 0xffu) / 255.0f;
    out[1] = (float)((rgb >> 8) & 0xffu) / 255.0f;
    out[2] = (float)(rgb & 0xffu) / 255.0f;
}

static void hwr_sun_apply(uint32_t mask, const struct hwr_settings *settings,
                          void *userdata)
{
    (void)userdata;
    if ((mask & HWR_CHANGE_TRUECOLOR) == 0)
        return;
    g_rows.modern =
        hwr_sun_row_enum("shading_mode", settings, HWR_SHADING_AUTHENTIC) ==
        HWR_SHADING_MODERN;
    g_rows.enabled = hwr_sun_row_bool("sun_light", settings, 1);
    hwr_sun_row_color("sun_color", settings, 0x00b9c8ffu, g_rows.color);
    g_rows.intensity = hwr_sun_row_float("sun_intensity", settings, 0.8f);
    g_rows.elevation_deg =
        hwr_sun_row_float("sun_elevation_deg", settings, 45.0f);
    g_rows.yaw_deg = hwr_sun_row_float("sun_yaw_deg", settings, 0.0f);
    g_rows.object_shadows =
        hwr_sun_row_bool("sun_object_shadows", settings, 1);
    g_rows.quality = (int)hwr_sun_row_enum("sun_shadow_quality", settings,
                                           HWR_SUN_QUALITY_MEDIUM);
    g_rows.softness = hwr_sun_row_float("sun_shadow_softness", settings, 0.1f);
    g_rows.span = hwr_sun_row_float("sun_shadow_span", settings, 4096.0f);
    if (!(g_rows.span >= 1024.0f))
        g_rows.span = 4096.0f;
    g_rows_valid = 1;
}

int hwr_sun_register(void)
{
    return hwr_settings_register_apply(hwr_sun_apply, NULL);
}

/* -------------------------------------------------------------- staging */

void hwr_sun_stage_map(const struct hwr_world *world)
{
    size_t i;

    g_sky_faces = 0;
    g_staged = 0;
    g_static_built = 0;
    g_static_count = 0;
    g_static_upload_due = 0;
    g_window.valid = 0;
    g_dir_valid = 0;
    g_map_generation++;
    if (world == NULL || !world->valid)
        return;
    /* The face census IS the auto-enable: wherever the wall driver would fill
     * sky (the per-face EDGE_MAP flag), light can enter. A map with none is
     * sealed and the machinery below never runs on it. */
    for (i = 0; i < world->wall_count; i++) {
        if ((world->walls[i].mapping_flags & 0x40u) != 0)
            g_sky_faces++;
    }
    g_staged = 1;
}

void hwr_sun_reset(void)
{
    g_sky_faces = 0;
    g_staged = 0;
    g_static_built = 0;
    g_static_count = 0;
    g_static_upload_due = 0;
    g_window.valid = 0;
    g_dir_valid = 0;
    g_map_generation++;
    hwr_scene_mesh_clear(&g_mesh);
    free(g_static);
    g_static = NULL;
    g_static_capacity = 0;
    free(g_dynamic);
    g_dynamic = NULL;
    g_dynamic_capacity = 0;
    g_dynamic_count = 0;
    hwr_gl_set_sun(NULL);
    hwr_gl_sun_set_dynamic(NULL, 0);
}

size_t hwr_sun_sky_faces(void)
{
    return g_staged ? g_sky_faces : 0;
}

unsigned long hwr_sun_static_builds(void)
{
    return g_static_builds;
}

/* ------------------------------------------------------ change tracking */

static uint32_t hwr_sun_mix(uint32_t hash, const void *bytes, size_t size)
{
    const uint8_t *p = (const uint8_t *)bytes;
    size_t i;

    for (i = 0; i < size; i++) {
        hash ^= p[i];
        hash *= 16777619u;
    }
    return hash;
}

/* A hash of every mutable world fact the static slice is built from: sector
 * heights and flat ids (lifts, scripted floors), wall texture ids and
 * mapping flags (live material commands), slab state, vertex positions
 * (scripted movers), and the door pool (pose and leaf textures). Pans and
 * animation frames are deliberately outside it: a scrolling or animating
 * texture moves its texels, not its geometry, and the texel store is shared
 * with the eye pass, so cut-outs follow without a rebuild. */
static uint32_t hwr_sun_world_hash(const struct hwr_world *world)
{
    uint32_t hash = 2166136261u;
    size_t i;

    for (i = 0; i < world->sector_count; i++) {
        const struct hwr_sector *sector = &world->sectors[i];
        int16_t values[6] = {
            sector->floor_z, sector->ceiling_z, sector->ceiling_flat_id,
            sector->floor_flat_id, (int16_t)sector->texture_flip_flags,
            sector->texture_map_override
        };

        hash = hwr_sun_mix(hash, values, sizeof values);
    }
    for (i = 0; i < world->wall_count; i++) {
        const struct hwr_wall *wall = &world->walls[i];
        uint16_t values[5] = {
            wall->middle_id, wall->upper_id, wall->lower_id,
            wall->mapping_flags, wall->scale
        };

        hash = hwr_sun_mix(hash, values, sizeof values);
    }
    for (i = 0; i < world->slab_count; i++) {
        const struct hwr_slab *slab = &world->slabs[i];
        uint16_t values[5] = {
            slab->lower_surface_id, (uint16_t)slab->lower_z,
            slab->upper_surface_id, (uint16_t)slab->upper_z, slab->flags
        };

        hash = hwr_sun_mix(hash, values, sizeof values);
    }
    for (i = 0; i < world->vertex_count; i++) {
        int16_t values[2] = {
            world->vertices[i].world_x, world->vertices[i].world_y
        };

        hash = hwr_sun_mix(hash, values, sizeof values);
    }
    hash = hwr_sun_mix(hash, &world->door_count, sizeof world->door_count);
    for (i = 0; i < world->door_count; i++) {
        const struct hwr_door *door = &world->doors[i];

        hash = hwr_sun_mix(hash, door->corner_x, sizeof door->corner_x);
        hash = hwr_sun_mix(hash, door->corner_y, sizeof door->corner_y);
        hash = hwr_sun_mix(hash, door->surfaces, sizeof door->surfaces);
        hash = hwr_sun_mix(hash, &door->state_flags,
                           sizeof door->state_flags);
        hash = hwr_sun_mix(hash, &door->angle, sizeof door->angle);
    }
    return hash;
}

/* ------------------------------------------------------- vertex streams */

static struct hwr_gl_sun_vertex *hwr_sun_grow(struct hwr_gl_sun_vertex **list,
                                              size_t *count, size_t *capacity,
                                              size_t extra)
{
    struct hwr_gl_sun_vertex *slot;

    if (*count + extra > *capacity) {
        size_t next = *capacity != 0 ? *capacity * 2u : 4096u;
        struct hwr_gl_sun_vertex *grown;

        while (next < *count + extra)
            next *= 2u;
        grown = (struct hwr_gl_sun_vertex *)realloc(*list,
                                                    next * sizeof *grown);
        if (grown == NULL)
            return NULL;
        *list = grown;
        *capacity = next;
    }
    slot = *list + *count;
    *count += extra;
    return slot;
}

/* ------------------------------------------------------ the static slice */

/* Which of the frame's casters a surface of the map mesh is, if any.
 * Returns 0 to skip it, 1 to cast solid, 2 to cast with the masked-texel
 * cut-out. The exclusions are the design's own: sky caps are screen-space
 * synthetics; the open-sky ceiling family (flat id 0 or 1) is the sky being
 * open by the map's own statement; translucent surfaces do not block the
 * beam in this round -- dimming them is the gobo pass's job, and until it
 * ships the light passes whole rather than being wrongly walled out. */
static int hwr_sun_classify(const struct hwr_scene *scene,
                            const struct hwr_world *world,
                            const struct hwr_mesh_surface *surface)
{
    int16_t id = surface->texture_id;
    uint16_t qflags = 0;
    uint16_t width = 0;
    uint16_t height = 0;

    if ((surface->flags & HWR_SURFACE_SKY_CAP) != 0)
        return 0;
    if (surface->kind == HWR_SURFACE_CEILING &&
        surface->sector_index < world->sector_count) {
        int16_t ceiling =
            world->sectors[surface->sector_index].ceiling_flat_id;

        if (ceiling == 0 || ceiling == 1)
            return 0;
    }
    if (id >= 0 && id < 0x1600) {
        if (scene->textures[id].skip)
            return 0;
        qflags = scene->textures[id].qflags;
        width = scene->textures[id].width;
        height = scene->textures[id].height;
    }
    if ((surface->flags & HWR_SURFACE_DIRECT) != 0) {
        /* A palette-colour wall part on the solid-colour BLEND writer is
         * see-through; every other direct surface is paint on solid rock. */
        if ((surface->flags & HWR_SURFACE_FLAT) == 0 &&
            surface->kind != HWR_SURFACE_DOOR &&
            (surface->mapping_flags & 0x03u) == 0x03u)
            return 0;
        return 1;
    }
    if ((qflags & 0x400u) != 0) {
        /* The translucent range: masked panes, transparent flats, and the
         * blend-capable portal bands. None of them blocks the beam here. */
        return 0;
    }
    if ((surface->flags & HWR_SURFACE_MASKED) != 0 && width != 0 &&
        height != 0)
        return 2;
    return 1;
}

/* One mesh surface into caster vertices. The texel coordinates follow the
 * wall driver's own densities -- along-wall texels from the mapping extent
 * (half-texel units, halved), vertical at half a texel per world unit,
 * HALF_PIXEL doubling both, image-fit stretching the picture across the band
 * -- computed per vertex and interpolated, which is exact for a planar quad
 * under an orthographic light. A solid caster carries no texels at all. */
static int hwr_sun_emit_surface(const struct hwr_scene *scene,
                                const struct hwr_mesh_surface *surface,
                                const struct hwr_mesh_vertex *vertices,
                                int cutout)
{
    struct hwr_gl_sun_vertex *out;
    uint32_t offset = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t flags = 0;
    size_t i;

    if (cutout == 2) {
        int16_t id = surface->texture_id;

        offset = scene->textures[id].offset;
        width = scene->textures[id].width;
        height = scene->textures[id].height;
        flags = HWR_GL_SUN_MASKED;
    }
    out = hwr_sun_grow(&g_static, &g_static_count, &g_static_capacity,
                       surface->vertex_count);
    if (out == NULL)
        return -1;
    for (i = 0; i < surface->vertex_count; i++) {
        const struct hwr_mesh_vertex *source = &vertices[i];
        float u = 0.0f;
        float v = 0.0f;

        if (flags != 0) {
            if (surface->kind == HWR_SURFACE_DOOR) {
                /* Door leaves carry direct 0..1 coordinates; the eye emit
                 * scales them by the texture just under its edge. */
                u = source->wall_s * ((float)height - 1.0f / 64.0f);
                v = source->wall_top * ((float)width - 1.0f / 64.0f);
            } else if ((surface->flags & HWR_SURFACE_FLAT) == 0) {
                float top = source->wall_top;
                int fit = (surface->mapping_flags &
                           HWR_SUN_TEX_IMAGE_FIT) != 0 &&
                          ((surface->mapping_flags &
                            HWR_SUN_TEX_TRANSPARENT) == 0 ||
                           surface->kind == HWR_SURFACE_SOLID_MIDDLE ||
                           surface->kind == HWR_SURFACE_PORTAL_MIDDLE);

                if (fit && surface->extent != 0) {
                    /* The picture stretched across the band: the along axis
                     * spans the rows once, the vertical the columns once.
                     * The band's own height is not in the vertex, so the
                     * vertical stretch reads the leaf's full texel count per
                     * world unit of the tiled law instead -- a fit wall's
                     * cut-out lands within a texel of the eye's. */
                    u = source->wall_s / (float)surface->extent *
                        ((float)height - 1.0f / 64.0f);
                    v = (top - (float)source->z) * 0.5f;
                } else {
                    float rate = (surface->mapping_flags &
                                  HWR_SUN_TEX_HALF_PIXEL) != 0 ? 2.0f : 1.0f;

                    u = (source->wall_s * rate +
                         (float)surface->pan_u * 2.0f) * 0.5f;
                    v = (top - (float)source->z) * rate * 0.5f +
                        (float)surface->pan_v;
                }
            }
        }
        out[i].x = (float)source->x;
        out[i].y = (float)source->y;
        out[i].z = (float)source->z;
        out[i].u = u;
        out[i].v = v;
        out[i].offset = offset;
        out[i].width = width;
        out[i].height = height;
        out[i].flags = flags;
        out[i].pad0 = 0;
        out[i].pad1 = 0;
    }
    return 0;
}

/* Builds the static slice whole: the map's own mesh -- every sector, wall,
 * slab and door, visibility-gated by nothing -- classified and converted.
 * The roof above a window is exactly the point: the eye's drawn set rarely
 * holds it, and shadows must not depend on where the eye walked. */
static int hwr_sun_build_static(const struct hwr_scene *scene,
                                const struct hwr_world *world)
{
    size_t i;

    g_static_count = 0;
    if (hwr_scene_build_mesh(&g_mesh, world, 0) != 0)
        return -1;
    for (i = 0; i < g_mesh.surface_count; i++) {
        const struct hwr_mesh_surface *surface = &g_mesh.surfaces[i];
        int class = hwr_sun_classify(scene, world, surface);

        if (class == 0)
            continue;
        if (hwr_sun_emit_surface(scene, surface,
                                 g_mesh.vertices + surface->first_vertex,
                                 class) != 0)
            return -1;
    }
    g_static_builds++;
    return 0;
}

/* ----------------------------------------------------- the dynamic slice */

static int32_t hwr_sun_sincos(const struct roth_api_v1 *api, uint32_t angle)
{
    return (int16_t)api->game_ram->u16(VA_G_SINCOS_TABLE +
                                       (angle & 0x1ffu) * 2u);
}

static int32_t hwr_sun_sar14(int64_t value)
{
    return value >= 0 ? (int32_t)(value >> 14)
                      : -(int32_t)(((uint64_t)(-value) + 0x3fffu) >> 14);
}

/* The light's own view yaw in the sprite resolver's domain, derived from the
 * frame's camera: the relative angle between the light's horizontal travel
 * and the camera's look direction, in the 0x200-per-turn unit the resolver
 * takes, added to the camera's own yaw. Derived RELATIVELY so the engine's
 * yaw zero never has to be pinned here; a directional sprite then shows the
 * light the side the light sees. */
static uint16_t hwr_sun_light_yaw(const struct hwr_gl_camera *camera)
{
    double look_x = -(double)camera->yaw_cos;
    double look_y = (double)camera->yaw_sin;
    double light_angle = atan2((double)g_dir[0], (double)g_dir[1]);
    double look_angle = atan2(look_x, look_y);
    double delta = (light_angle - look_angle) * 512.0 /
                   (2.0 * 3.14159265358979323846);

    return (uint16_t)(((long)lround((double)camera->yaw + delta)) & 0x1ff);
}

/* One decoded 3D prop into the dynamic slice, as its real geometry: the mesh
 * emitter's own placement law (facing rotation in the engine's table, the
 * modular position add), faces emitted solid. Pins are camera-facing
 * decorations and are skipped. */
static int hwr_sun_emit_mesh(const struct hwr_scene *scene,
                             const struct hwr_object *object,
                             const struct hwr_mesh_resource *resource,
                             const struct roth_api_v1 *api)
{
    const struct hwr_meshes *meshes = &scene->object_meshes;
    int32_t facing_sin = hwr_sun_sincos(api, 2u * object->facing);
    int32_t facing_cos = hwr_sun_sincos(api, 2u * object->facing + 0x80u);
    size_t face_index;

    for (face_index = 0; face_index < resource->face_count; face_index++) {
        const struct hwr_mesh_face *face =
            &meshes->faces[resource->first_face + face_index];
        float corner_x[4];
        float corner_y[4];
        float corner_z[4];
        size_t corner_count = face->kind == HWR_MESH_FACE_QUAD ? 4u : 3u;
        size_t triangles;
        struct hwr_gl_sun_vertex *out;
        size_t t;

        if (face->kind == HWR_MESH_FACE_PIN)
            continue;
        for (t = 0; t < corner_count; t++) {
            const struct hwr_mesh_local_vertex *local =
                &meshes->vertices[resource->first_vertex + face->vertex[t]];
            int32_t ox = hwr_sun_sar14((int64_t)facing_cos * local->x +
                                       (int64_t)facing_sin * local->y);
            int32_t oy = hwr_sun_sar14((int64_t)facing_cos * local->y -
                                       (int64_t)facing_sin * local->x);

            corner_x[t] = (float)(int16_t)((uint16_t)object->world_x +
                                           (uint16_t)ox);
            corner_y[t] = (float)(int16_t)((uint16_t)object->world_y +
                                           (uint16_t)oy);
            corner_z[t] = (float)(int16_t)((uint16_t)object->world_z +
                                           (uint16_t)local->vertical);
        }
        triangles = corner_count - 2u;
        out = hwr_sun_grow(&g_dynamic, &g_dynamic_count, &g_dynamic_capacity,
                           triangles * 3u);
        if (out == NULL)
            return -1;
        for (t = 0; t < triangles; t++) {
            size_t corners[3];
            size_t k;

            corners[0] = 0;
            corners[1] = t + 1u;
            corners[2] = t + 2u;
            for (k = 0; k < 3u; k++) {
                struct hwr_gl_sun_vertex *vertex = &out[t * 3u + k];

                vertex->x = corner_x[corners[k]];
                vertex->y = corner_y[corners[k]];
                vertex->z = corner_z[corners[k]];
                vertex->u = 0.0f;
                vertex->v = 0.0f;
                vertex->offset = 0;
                vertex->width = 0;
                vertex->height = 0;
                vertex->flags = 0;
                vertex->pad0 = 0;
                vertex->pad1 = 0;
            }
        }
    }
    return 0;
}

/* The object cards: the full object table -- a tree behind the player still
 * shadows the window in front of him -- minus the exclusions the light
 * seeder already names (nothing without an owner sector, no reflection-only
 * synthetics). Each becomes one upright quad faced to the light, sized by
 * the billboard emitter's own world-space law, its sprite frame resolved
 * with the light's yaw standing in for the view's, pushed half a light-space
 * texel toward the light so its own drawn sprite never darkens itself. */
static void hwr_sun_build_dynamic(const struct hwr_scene *scene,
                                  const struct hwr_world *world,
                                  const struct hwr_objects *objects,
                                  const struct roth_api_v1 *api,
                                  const struct hwr_gl_camera *camera)
{
    const struct hwr_billboards *bb = &scene->billboards;
    double xscale;
    double pscale;
    double hscale;
    double vscale;
    uint16_t light_yaw;
    float push = 0.5f * g_window.texel;
    size_t i;

    g_dynamic_count = 0;
    if (!g_rows.object_shadows || objects == NULL || !objects->valid ||
        api == NULL || api->game_ram == NULL)
        return;
    xscale = (double)(int32_t)api->game_ram->u32(VA_G_VIEW_PARAMS_XSCALE);
    pscale = (double)(int32_t)api->game_ram->u32(VA_G_PERSPECTIVE_SCALE);
    if (camera->source_focal_x == 0 || camera->source_focal_y == 0 ||
        xscale <= 0.0 || pscale <= 0.0)
        return;
    hscale = xscale / ((double)camera->source_focal_x * 256.0);
    vscale = pscale / ((double)camera->source_focal_y * 256.0);
    light_yaw = hwr_sun_light_yaw(camera);
    for (i = 0; i < objects->count; i++) {
        const struct hwr_object *object = &objects->items[i];
        struct hwr_sprite_lookup lookup;
        struct hwr_sprite_frame frame;
        const struct hwr_bb_slot *slot = NULL;
        uint32_t offset;
        uint16_t tw;
        uint16_t th;
        uint16_t fa;
        int16_t origin_x = 0;
        double famod;
        unsigned cl;
        double half_w;
        double vext_d;
        double disp_d;
        float z_top;
        float z_bottom;
        float axis[2];
        float center_x;
        float center_y;
        struct hwr_gl_sun_vertex *out;

        if (object->sector_index == (uint16_t)HWR_OBJECT_NO_SECTOR)
            continue;
        if ((object->flags & HWR_OBJECT_FLAG_REFLECTION_ONLY) != 0)
            continue;
        if (hwr_sprites_resolve(api, scene->map_archive,
                                scene->shared_archive, object->graphic_id,
                                object->facing, light_yaw, 0,
                                object->backref, &lookup) != 0)
            continue;
        if (lookup.archive == NULL ||
            lookup.entry_id >= lookup.archive->entry_count)
            continue;
        if (lookup.klass == HWR_DAS_CLASS_ANIMATED) {
            int live = hwr_sprites_live_frame_index(api, lookup.archive_id);

            if (live >= 0)
                lookup.frame_index = (uint16_t)live;
        }
        if ((lookup.archive->entries[lookup.entry_id].payload_flags &
             0xff00u) == HWR_SUN_MESH_FLAG) {
            const struct hwr_mesh_resource *resource = NULL;

            if (lookup.archive == scene->object_meshes.map_archive &&
                lookup.entry_id < scene->object_meshes.map_dir_count &&
                scene->object_meshes.map_dir[lookup.entry_id].present)
                resource = &scene->object_meshes.map_dir[lookup.entry_id];
            else if (lookup.archive == scene->object_meshes.shared_archive &&
                     lookup.entry_id < scene->object_meshes.shared_dir_count &&
                     scene->object_meshes.shared_dir[lookup.entry_id].present)
                resource =
                    &scene->object_meshes.shared_dir[lookup.entry_id];
            if (resource != NULL &&
                hwr_sun_emit_mesh(scene, object, resource, api) != 0)
                return;
            continue;
        }
        if (hwr_sprites_frame(&lookup, &frame) != 0)
            continue;
        if ((frame.flags & (uint16_t)HWR_SUN_MESH_FLAG) != 0)
            continue;
        if ((frame.flags & (uint16_t)HWR_SUN_DRAW_NOTHING_FLAG) != 0)
            continue;
        /* The atlas slot, exactly as the billboard emitter finds it: the
         * packed directory of the archive the lookup resolved into, the
         * animated frame at its stride, the grouped child by its payload
         * offset. */
        if (!bb->built)
            continue;
        if (lookup.archive == bb->map_archive &&
            lookup.entry_id < bb->map_dir_count)
            slot = &bb->map_dir[lookup.entry_id];
        else if (lookup.archive == bb->shared_archive &&
                 lookup.entry_id < bb->shared_dir_count)
            slot = &bb->shared_dir[lookup.entry_id];
        if (slot == NULL || !slot->present)
            continue;
        if (slot->klass == HWR_DAS_CLASS_ANIMATED) {
            uint16_t index = lookup.frame_index < slot->frame_count
                           ? lookup.frame_index
                           : (uint16_t)(slot->frame_count != 0
                                            ? slot->frame_count - 1u : 0u);

            offset = slot->base_offset + (uint32_t)index * slot->frame_bytes;
            tw = slot->width;
            th = slot->height;
            fa = slot->flags;
        } else if (slot->klass == HWR_DAS_CLASS_GROUPED) {
            const struct hwr_bb_child *child = NULL;
            uint16_t c;

            for (c = 0; c < slot->child_count; c++) {
                const struct hwr_bb_child *candidate =
                    &bb->children[slot->child_first + c];

                if (candidate->payload_offset ==
                    (uint32_t)lookup.child_offset) {
                    child = candidate;
                    break;
                }
            }
            if (child == NULL) {
                offset = slot->base_offset;
                tw = slot->width;
                th = slot->height;
                fa = slot->flags;
            } else {
                offset = child->atlas_offset;
                tw = child->width;
                th = child->height;
                fa = child->flags;
            }
        } else {
            offset = slot->base_offset;
            tw = slot->width;
            th = slot->height;
            fa = slot->flags;
        }
        if (tw == 0 || th == 0)
            continue;
        /* The world-space sizing law, line for line the billboard emitter's:
         * the block-modifier scale nibble, the signed origin prefix of plain
         * and animated leaves, and the two vertical anchors. */
        if (lookup.klass == HWR_DAS_CLASS_PLAIN ||
            lookup.klass == HWR_DAS_CLASS_ANIMATED) {
            const struct hwr_das_entry *entry =
                &lookup.archive->entries[lookup.entry_id];

            origin_x = (int16_t)(uint16_t)(entry->size_prefix & 0xffffu);
        }
        if (lookup.flip)
            origin_x = (int16_t)(-origin_x);
        famod = 1.0;
        cl = 7;
        if ((fa & 0x80u) != 0) {
            if ((fa & 0x6000u) != 0) {
                unsigned t = (unsigned)((((uint32_t)fa << 3) |
                                         ((uint32_t)fa >> 13)) & 3u);

                famod = (double)(1u << t);
                cl = 7u + t;
            } else {
                famod = 0.5;
                cl = 6;
            }
        }
        half_w = (double)((uint32_t)((uint32_t)th * 2u) << cl) * hscale;
        vext_d = 2.0 * (double)tw * famod * vscale;
        {
            int16_t origin_y = 0;

            if ((lookup.klass == HWR_DAS_CLASS_PLAIN ||
                 lookup.klass == HWR_DAS_CLASS_ANIMATED) &&
                (object->flags9 & 0x80u) == 0)
                origin_y = (int16_t)(uint16_t)(
                    lookup.archive->entries[lookup.entry_id].size_prefix >>
                    16);
            disp_d = (2.0 * (double)(fa & 0xfu) + (double)origin_y) * vscale;
        }
        if ((fa & 0x10u) != 0) {
            z_top = (float)((double)object->world_z + disp_d);
            z_bottom = (float)((double)z_top - vext_d);
        } else {
            z_bottom = (float)((double)object->world_z - disp_d);
            z_top = (float)((double)z_bottom + vext_d);
        }
        /* Fixed-orientation wall art keeps its authored axis -- it is pinned
         * to its wall in the world -- and every camera-facing object turns
         * to the light instead of the eye: the billboard law with the light
         * standing in. */
        if ((object->flags9 & 0x80u) != 0) {
            uint32_t phi = 2u * (uint32_t)object->facing + 0x80u;

            axis[0] = (float)hwr_sun_sincos(api, phi) / 16384.0f;
            axis[1] = (float)hwr_sun_sincos(api, phi + 0x80u) / 16384.0f;
        } else {
            axis[0] = g_right[0];
            axis[1] = g_right[1];
        }
        center_x = (float)object->world_x + (float)origin_x * axis[0];
        center_y = (float)object->world_y + (float)origin_x * axis[1];
        out = hwr_sun_grow(&g_dynamic, &g_dynamic_count, &g_dynamic_capacity,
                           6u);
        if (out == NULL)
            return;
        hwr_sunm_card(center_x, center_y, axis, (float)half_w, z_top,
                      z_bottom, g_dir, push, offset, tw, th,
                      lookup.flip != 0, out);
    }
    (void)world;
}

/* ---------------------------------------------------------------- frame */

static void hwr_sun_publish_off(void)
{
    hwr_gl_set_sun(NULL);
    hwr_gl_sun_set_dynamic(NULL, 0);
}

void hwr_sun_frame(struct hwr_scene *scene, const struct hwr_world *world,
                   const struct hwr_objects *objects,
                   const struct roth_api_v1 *api,
                   const struct hwr_gl_camera *camera)
{
    struct hwr_gl_sun sun;
    float dir[3];
    int force = 0;
    int moved;
    int resolution;
    uint32_t hash;
    float cam_x;
    float cam_y;

    if (!g_rows_valid || !g_rows.modern || !g_rows.enabled ||
        !(g_rows.intensity > 0.0f) || !g_staged || g_sky_faces == 0 ||
        scene == NULL || !scene->valid || !scene->uploaded || world == NULL ||
        !world->valid || camera == NULL) {
        hwr_sun_publish_off();
        return;
    }
    hwr_sunm_direction(g_rows.yaw_deg, g_rows.elevation_deg, dir);
    if (!g_dir_valid || dir[0] != g_dir[0] || dir[1] != g_dir[1] ||
        dir[2] != g_dir[2]) {
        g_dir[0] = dir[0];
        g_dir[1] = dir[1];
        g_dir[2] = dir[2];
        hwr_sunm_basis(g_dir, g_right, g_up);
        g_dir_valid = 1;
        force = 1;
    }
    /* The static slice, current to the map's own mutable facts: door poses,
     * lift heights, live material ids. The hash walk is a few thousand FNV
     * steps a frame; the rebuild runs only on the frames something moved. */
    hash = hwr_sun_world_hash(world);
    if (!g_static_built || g_static_generation != g_map_generation ||
        g_static_hash != hash) {
        if (hwr_sun_build_static(scene, world) != 0) {
            hwr_sun_publish_off();
            return;
        }
        g_static_built = 1;
        g_static_generation = g_map_generation;
        g_static_hash = hash;
        g_static_upload_due = 1;
        force = 1;
    }
    if (g_static_upload_due &&
        hwr_gl_sun_upload_static(g_static, g_static_count))
        g_static_upload_due = 0;
    switch (g_rows.quality) {
    case HWR_SUN_QUALITY_LOW:
        resolution = 1024;
        break;
    case HWR_SUN_QUALITY_HIGH:
        resolution = 4096;
        break;
    case HWR_SUN_QUALITY_MEDIUM:
    default:
        resolution = 2048;
        break;
    }
    cam_x = (float)(int16_t)((uint16_t)(-(int32_t)camera->view_offset_x));
    cam_y = (float)(int16_t)((uint16_t)(-(int32_t)camera->view_offset_y));
    moved = hwr_sunm_window_update(&g_window, g_dir, g_right, g_up, cam_x,
                                   cam_y, (float)camera->eye_z,
                                   g_rows.span, resolution, force);
    hwr_sun_build_dynamic(scene, world, objects, api, camera);
    memset(&sun, 0, sizeof sun);
    sun.active = 1;
    sun.dir[0] = g_dir[0];
    sun.dir[1] = g_dir[1];
    sun.dir[2] = g_dir[2];
    sun.color[0] = g_rows.color[0];
    sun.color[1] = g_rows.color[1];
    sun.color[2] = g_rows.color[2];
    sun.intensity = g_rows.intensity;
    hwr_sunm_matrices(&g_window, g_dir, g_right, g_up, sun.clip, sun.lookup);
    sun.resolution = resolution;
    sun.cutouts = g_rows.quality != HWR_SUN_QUALITY_LOW;
    sun.soft = g_rows.quality != HWR_SUN_QUALITY_LOW &&
               g_rows.softness > 0.0f;
    sun.bias = hwr_sunm_bias(&g_window);
    hwr_gl_set_sun(&sun);
    hwr_gl_sun_set_dynamic(g_dynamic, g_dynamic_count);
    if (hwr_sun_debug() && (moved || force)) {
        fprintf(stderr,
                "[renderer_overhaul] sun: dir=(%.4f %.4f %.4f) "
                "yaw=%.1f elev=%.1f window a=%.0f b=%.0f d=%.0f span=%.0f "
                "texel=%.2f res=%d casters static=%lu dynamic=%lu "
                "sky-faces=%lu\n",
                (double)g_dir[0], (double)g_dir[1], (double)g_dir[2],
                (double)g_rows.yaw_deg, (double)g_rows.elevation_deg,
                (double)g_window.center_a, (double)g_window.center_b,
                (double)g_window.center_d, (double)g_window.span,
                (double)g_window.texel, resolution,
                (unsigned long)g_static_count,
                (unsigned long)g_dynamic_count,
                (unsigned long)g_sky_faces);
    }
}
