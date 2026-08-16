#include "debug_state.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

#define DEBUG_VA_RAW_MAP_NAME         0x701ecu
#define DEBUG_VA_DAS_NAME             0x7023cu
#define DEBUG_VA_CURSOR_TYPE          0x7e932u
#define DEBUG_VA_SCREEN_WIDTH         0x85498u
#define DEBUG_VA_SCREEN_HEIGHT        0x854a0u
#define DEBUG_VA_APPLIED_PITCH        0x8c108u
#define DEBUG_VA_CURSOR_RESULT        0x90a48u
#define DEBUG_VA_CURSOR_SURFACE_REF   0x90a50u
#define DEBUG_VA_REQUESTED_PITCH      0x90a74u
#define DEBUG_VA_PLAYER_YAW           0x90a8au
#define DEBUG_VA_PLAYER_X             0x90a8eu
#define DEBUG_VA_PLAYER_Z             0x90a92u
#define DEBUG_VA_PLAYER_Y             0x90a96u
#define DEBUG_VA_PLAYER_SECTOR        0x90c12u
#define DEBUG_VA_MAP_OBJECTS_BUFFER   0x90aa4u
#define DEBUG_VA_MAP_GEOMETRY_BUFFER  0x90aa8u
#define DEBUG_VA_SECTOR_COUNT         0x91df8u
#define DEBUG_VA_SECTOR_SECTION       0x91dfcu
#define DEBUG_SECTOR_STRIDE           0x1au
#define DEBUG_FACE_STRIDE             0x0cu
#define DEBUG_OBJECT_STRIDE           0x10u
#define DEBUG_GEOMETRY_EXTENT         0x10000u
#define DEBUG_OBJECT_EXTENT           0x10000u

/* The engine's own result kinds in the cursor record's type byte: 3 is a wall
 * or sector face, 4 is a placed object. The blocks below are gated on these
 * exactly as the interaction code itself is; nothing is reported off a kind the
 * record was not written for. */
#define DEBUG_CURSOR_KIND_FACE        3u
#define DEBUG_CURSOR_KIND_OBJECT      4u

/* Fields of one placed-object record, which is the same sixteen-byte record the
 * map file holds and the engine renders from. */
#define DEBUG_OBJECT_OFF_GRAPHIC      0x04u
#define DEBUG_OBJECT_OFF_ID           0x0eu

static struct roth_video_event_v1 debug_video;
static int debug_have_video;

static uint16_t debug_read_u16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t debug_read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static const uint8_t *debug_geometry_base(const struct roth_api_v1 *api)
{
    uintptr_t stored =
        (uintptr_t)api->game_ram->u32(DEBUG_VA_MAP_GEOMETRY_BUFFER);

    return stored >= 0x1000u ? (const uint8_t *)stored : NULL;
}

static int debug_geometry_read(const uint8_t *geometry, uint16_t offset,
                               void *destination, uint32_t size)
{
    if (geometry == NULL ||
        size > DEBUG_GEOMETRY_EXTENT - (uint32_t)offset)
        return -1;
    memcpy(destination, geometry + offset, size);
    return 0;
}

void debug_state_reset(void)
{
    memset(&debug_video, 0, sizeof(debug_video));
    debug_have_video = 0;
}

void debug_state_on_video_mode(const struct roth_video_event_v1 *event)
{
    if (event == NULL ||
        event->struct_size < offsetof(struct roth_video_event_v1, aspect_h) +
                             sizeof(event->aspect_h))
        return;
    debug_video = *event;
    debug_have_video = 1;
}

static void debug_sample_name(const struct roth_api_v1 *api,
                              uint32_t offset,
                              char output[81],
                              int *valid)
{
    uint8_t raw[80];
    size_t i;

    if (api->game_ram->read_block(offset, raw, sizeof(raw)) != 0)
        return;
    for (i = 0; i < sizeof(raw); i++) {
        unsigned char ch = raw[i];

        if (ch == '\0')
            break;
        output[i] = isprint(ch) ? (char)ch : '?';
    }
    output[i] = '\0';
    *valid = i != 0;
}

static void debug_sample_cursor(const struct roth_api_v1 *api,
                                struct debug_state_view *view)
{
    uint8_t result[8] = {0};
    uint8_t reference[10] = {0};

    if (api->game_ram->read_block(DEBUG_VA_CURSOR_RESULT,
                                  result, sizeof(result)) != 0)
        return;
    if (api->game_ram->read_block(DEBUG_VA_CURSOR_SURFACE_REF,
                                  reference, sizeof(reference)) != 0)
        return;

    view->cursor_surface_kind = result[1];
    view->cursor_das_entry = debug_read_u16(result + 2);
    view->cursor_texture_v = debug_read_u16(result + 4);
    view->cursor_texture_u = debug_read_u16(result + 6);
    view->cursor_face_record_offset = debug_read_u16(reference);
    view->cursor_sector_record_offset = debug_read_u16(reference + 2);
    view->cursor_draw_flags = debug_read_u16(reference + 4);
    view->cursor_render_record_ref = debug_read_u32(reference + 6);
}

static const uint8_t *debug_objects_base(const struct roth_api_v1 *api)
{
    uintptr_t stored =
        (uintptr_t)api->game_ram->u32(DEBUG_VA_MAP_OBJECTS_BUFFER);

    return stored >= 0x1000u ? (const uint8_t *)stored : NULL;
}

static int debug_objects_fit(uint32_t offset, uint32_t size)
{
    return offset <= DEBUG_OBJECT_EXTENT &&
           size <= DEBUG_OBJECT_EXTENT - offset;
}

/* WHICH PLACED OBJECT THE CURSOR RESULT POINTS AT.
 *
 * The engine's cursor record carries, for an object result, the address of the
 * record it was rendering from. That address alone names nothing a person can
 * type, so it is resolved against the loaded map's OWN object lists: the walk
 * below is the map's per-sector lists flattened with the sectors in ascending
 * order, which is the enumeration the community map editor prints as
 * "Object: N", and the record is only reported when its address lands exactly on
 * one of those records. An address that does not is left unreported -- the mod
 * does not identify objects from guesses.
 *
 * The running number counts EVERY record the map holds, hidden ones included:
 * what numbers an object is the map's own list, not this walk's willingness to
 * draw it.
 *
 * The record's own texture index and texture source live at +4 as one word, and
 * that pair is the object's OWN picture -- a different number from the DAS entry
 * the cursor sampled, which for a 3D object is the material of the mesh face
 * under the cursor and not the object at all. */
static void debug_sample_object(const struct roth_api_v1 *api,
                                struct debug_state_view *view,
                                uint32_t sector_count)
{
    const uint8_t *section = debug_objects_base(api);
    uintptr_t want = (uintptr_t)view->cursor_render_record_ref;
    uint32_t number = 0;
    uint32_t target;
    uint32_t sector;

    if (view->cursor_surface_kind != DEBUG_CURSOR_KIND_OBJECT ||
        section == NULL || want < (uintptr_t)section)
        return;
    if ((uintptr_t)(want - (uintptr_t)section) >= DEBUG_OBJECT_EXTENT)
        return;
    target = (uint32_t)(want - (uintptr_t)section);
    for (sector = 0; sector < sector_count; sector++) {
        uint32_t directory = 2u + sector * 2u;
        uint32_t list;
        uint32_t record;
        uint32_t count;
        uint32_t i;

        if (!debug_objects_fit(directory, 2u))
            break;                           /* the directory only grows */
        list = (uint32_t)debug_read_u16(section + directory);
        if (list < 2u || !debug_objects_fit(list, 1u))
            continue;
        count = section[list];
        if (count == 0u)
            continue;
        if (!debug_objects_fit(list + 2u, count * DEBUG_OBJECT_STRIDE)) {
            number += count;                 /* still numbered by the map */
            continue;
        }
        record = list + 2u;
        for (i = 0; i < count; i++, record += DEBUG_OBJECT_STRIDE) {
            if (record != target) {
                number++;
                continue;
            }
            view->cursor_object_valid = 1;
            view->cursor_object_index = number;
            view->cursor_object_graphic =
                debug_read_u16(section + record + DEBUG_OBJECT_OFF_GRAPHIC);
            view->cursor_object_id =
                debug_read_u16(section + record + DEBUG_OBJECT_OFF_ID);
            return;
        }
    }
}

static int debug_sector_index(uint16_t offset, uint32_t first,
                              uint32_t count, uint32_t *index)
{
    uint32_t delta;

    if ((uint32_t)offset < first)
        return 0;
    delta = (uint32_t)offset - first;
    if (delta % DEBUG_SECTOR_STRIDE != 0 ||
        delta / DEBUG_SECTOR_STRIDE >= count)
        return 0;
    *index = delta / DEBUG_SECTOR_STRIDE;
    return 1;
}

static void debug_sample_geometry(const struct roth_api_v1 *api,
                                  struct debug_state_view *view,
                                  uint32_t sector_section,
                                  uint32_t sector_count)
{
    const uint8_t *geometry = debug_geometry_base(api);
    uint8_t sector[DEBUG_SECTOR_STRIDE];
    uint8_t slab[0x0d];
    uint8_t face[DEBUG_FACE_STRIDE];
    uint8_t mapping[DEBUG_FACE_STRIDE];
    uint8_t header[2];
    uint16_t slab_offset;
    uint16_t first_face;
    uint16_t face_count;
    uint16_t mapping_offset;
    uint32_t delta;

    if (geometry == NULL)
        return;

    if (view->sector_index_valid &&
        debug_geometry_read(geometry, view->player_sector_offset,
                            sector, sizeof(sector)) == 0) {
        view->sector_roof_height = (int16_t)debug_read_u16(sector);
        view->sector_floor_height = (int16_t)debug_read_u16(sector + 2);
        view->sector_texture_top = debug_read_u16(sector + 6);
        view->sector_texture_bottom = debug_read_u16(sector + 8);
        view->sector_glow = sector[0x0b];
        view->sector_id = (int16_t)debug_read_u16(sector + 0x14);
        view->sector_details_valid = 1;

        slab_offset = debug_read_u16(sector + 0x18);
        if (slab_offset != 0 &&
            debug_geometry_read(geometry, slab_offset,
                                slab, sizeof(slab)) == 0) {
            view->mid_platform_roof_texture = debug_read_u16(slab);
            view->mid_platform_roof_height =
                (int16_t)debug_read_u16(slab + 2);
            view->mid_platform_floor_texture = debug_read_u16(slab + 6);
            view->mid_platform_floor_height =
                (int16_t)debug_read_u16(slab + 8);
            view->mid_platform_valid = 1;
        }
    }

    if (view->cursor_surface_kind != DEBUG_CURSOR_KIND_FACE ||
        debug_geometry_read(geometry, 6, header, sizeof(header)) != 0)
        return;
    first_face = debug_read_u16(header);
    if (first_face < 2 ||
        debug_geometry_read(geometry, (uint16_t)(first_face - 2u),
                            header, sizeof(header)) != 0)
        return;
    face_count = debug_read_u16(header);
    if (view->cursor_face_record_offset < first_face)
        return;
    delta = (uint32_t)view->cursor_face_record_offset - first_face;
    if (delta % DEBUG_FACE_STRIDE != 0 ||
        delta / DEBUG_FACE_STRIDE >= face_count)
        return;

    view->cursor_face_index_valid = 1;
    view->cursor_face_index = delta / DEBUG_FACE_STRIDE;
    view->cursor_sector_index_valid =
        debug_sector_index(view->cursor_sector_record_offset,
                           sector_section, sector_count,
                           &view->cursor_sector_index);
    if (debug_geometry_read(geometry, view->cursor_face_record_offset,
                            face, sizeof(face)) != 0)
        return;
    mapping_offset = debug_read_u16(face + 4);
    if (debug_geometry_read(geometry, mapping_offset,
                            mapping, sizeof(mapping)) != 0)
        return;
    view->cursor_face_texture_middle = debug_read_u16(mapping + 2);
    view->cursor_face_texture_top = debug_read_u16(mapping + 4);
    view->cursor_face_texture_bottom = debug_read_u16(mapping + 6);
    view->cursor_face_textures_valid = 1;
}

void debug_state_sample(const struct roth_api_v1 *api,
                        struct debug_state_view *view)
{
    uint32_t sector_count;
    uint32_t sector_section;
    uint32_t screen_w;
    uint32_t screen_h;

    memset(view, 0, sizeof(*view));
    if (api == NULL || api->game_ram == NULL)
        return;

    debug_sample_name(api, DEBUG_VA_RAW_MAP_NAME,
                      view->raw_map_name, &view->raw_map_valid);
    debug_sample_name(api, DEBUG_VA_DAS_NAME,
                      view->das_name, &view->das_valid);
    sector_count = api->game_ram->u32(DEBUG_VA_SECTOR_COUNT);
    sector_section = api->game_ram->u32(DEBUG_VA_SECTOR_SECTION);
    view->player_sector_offset = api->game_ram->u16(DEBUG_VA_PLAYER_SECTOR);
    view->player_valid =
        view->raw_map_valid && view->player_sector_offset != 0;
    if (view->player_valid) {
        view->player_x = (int16_t)api->game_ram->u16(DEBUG_VA_PLAYER_X);
        view->player_y = (int16_t)api->game_ram->u16(DEBUG_VA_PLAYER_Y);
        view->player_z = (int16_t)api->game_ram->u16(DEBUG_VA_PLAYER_Z);
        view->player_yaw = api->game_ram->u16(DEBUG_VA_PLAYER_YAW);
        view->requested_pitch =
            (int32_t)api->game_ram->u32(DEBUG_VA_REQUESTED_PITCH);
        view->applied_pitch =
            (int32_t)api->game_ram->u32(DEBUG_VA_APPLIED_PITCH);

        view->sector_index_valid =
            debug_sector_index(view->player_sector_offset,
                               sector_section, sector_count,
                               &view->player_sector_index);
    }
    view->cursor_type = api->game_ram->u8(DEBUG_VA_CURSOR_TYPE);
    debug_sample_cursor(api, view);
    debug_sample_geometry(api, view, sector_section, sector_count);
    debug_sample_object(api, view, sector_count);

    if (debug_have_video) {
        view->video_valid = 1;
        view->pixel_w = debug_video.pixel_w;
        view->pixel_h = debug_video.pixel_h;
        view->aspect_w = debug_video.aspect_w;
        view->aspect_h = debug_video.aspect_h;
    } else {
        screen_w = api->game_ram->u32(DEBUG_VA_SCREEN_WIDTH);
        screen_h = api->game_ram->u32(DEBUG_VA_SCREEN_HEIGHT);
        if (screen_w != 0 && screen_h != 0) {
            view->video_valid = 1;
            view->pixel_w = screen_w;
            view->pixel_h = screen_h;
            view->aspect_w = screen_w;
            view->aspect_h = screen_h;
        }
    }

    if (api->window != NULL &&
        api->window->struct_size >=
            offsetof(struct roth_window_v1, get_window_state) +
            sizeof(api->window->get_window_state)) {
        view->window.struct_size = sizeof(view->window);
        if (api->window->get_window_state(api, &view->window) == ROTH_OK)
            view->window_valid = 1;
    }
}
