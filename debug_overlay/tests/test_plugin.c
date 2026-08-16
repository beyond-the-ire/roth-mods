#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "roth_sdk.h"
#include "../debug_input.h"
#include "../debug_state.h"

#define TEST_RAM_SIZE 0xa0000u
#define TEST_GUARD_SIZE 16u

static uint8_t test_ram[TEST_RAM_SIZE];
static uint8_t test_geometry[0x10000];
static uint8_t test_objects[0x10000];

static uint8_t test_u8(uint32_t offset)
{
    return offset < TEST_RAM_SIZE ? test_ram[offset] : 0;
}

static uint16_t test_u16(uint32_t offset)
{
    if (offset > TEST_RAM_SIZE - 2u)
        return 0;
    return (uint16_t)((uint16_t)test_ram[offset] |
                      ((uint16_t)test_ram[offset + 1u] << 8));
}

static uint32_t test_u32(uint32_t offset)
{
    if (offset > TEST_RAM_SIZE - 4u)
        return 0;
    return (uint32_t)test_ram[offset] |
           ((uint32_t)test_ram[offset + 1u] << 8) |
           ((uint32_t)test_ram[offset + 2u] << 16) |
           ((uint32_t)test_ram[offset + 3u] << 24);
}

static int test_read_block(uint32_t offset, void *destination, uint32_t size)
{
    if (offset > TEST_RAM_SIZE || size > TEST_RAM_SIZE - offset)
        return -1;
    memcpy(destination, test_ram + offset, size);
    return 0;
}

static uint32_t test_ram_size(void)
{
    return TEST_RAM_SIZE;
}

static void *test_to_ptr(uint32_t offset)
{
    return offset < TEST_RAM_SIZE ? test_ram + offset : NULL;
}

static const char *test_plugin_dir(void)
{
    return TEST_PLUGIN_DIR;
}

static int32_t test_get_window_state(
    const struct roth_api_v1 *api,
    struct roth_window_state_v1 *state)
{
    (void)api;
    state->generation = 3;
    state->window_mode = ROTH_WINMODE_WINDOWED;
    state->present_mode = ROTH_PRESENT_FIFO;
    state->min_present_interval_us = 0;
    state->last_request_result = ROTH_OK;
    return ROTH_OK;
}

static void test_put_u16(uint32_t offset, uint16_t value)
{
    test_ram[offset] = (uint8_t)value;
    test_ram[offset + 1u] = (uint8_t)(value >> 8);
}

static void test_put_u32(uint32_t offset, uint32_t value)
{
    test_ram[offset] = (uint8_t)value;
    test_ram[offset + 1u] = (uint8_t)(value >> 8);
    test_ram[offset + 2u] = (uint8_t)(value >> 16);
    test_ram[offset + 3u] = (uint8_t)(value >> 24);
}

static void test_geometry_put_u16(uint16_t offset, uint16_t value)
{
    test_geometry[offset] = (uint8_t)value;
    test_geometry[(uint16_t)(offset + 1u)] = (uint8_t)(value >> 8);
}

static void test_objects_put_u16(uint16_t offset, uint16_t value)
{
    test_objects[offset] = (uint8_t)value;
    test_objects[(uint16_t)(offset + 1u)] = (uint8_t)(value >> 8);
}

static void test_present(const struct roth_plugin_info_v1 *plugin,
                         const struct roth_api_v1 *api,
                         uint32_t serial,
                         uint32_t tick,
                         uint64_t now_ns)
{
    struct roth_present_timing_v1 timing;

    memset(&timing, 0, sizeof(timing));
    timing.struct_size = sizeof(timing);
    timing.present_serial = serial;
    timing.tick_serial = tick;
    timing.now_ns_lo = (uint32_t)now_ns;
    timing.now_ns_hi = (uint32_t)(now_ns >> 32);
    plugin->on_present_pace(api, &timing);
}

static void test_compose_size(const struct roth_plugin_info_v1 *plugin,
                              const struct roth_api_v1 *api,
                              uint32_t width,
                              uint32_t height,
                              int expect_change)
{
    size_t bytes = (size_t)width * height;
    uint8_t *allocation = (uint8_t *)malloc(bytes + TEST_GUARD_SIZE * 2u);
    uint8_t *pixels;
    size_t i;
    int changed = 0;

    assert(allocation != NULL);
    memset(allocation, 0x5a, bytes + TEST_GUARD_SIZE * 2u);
    pixels = allocation + TEST_GUARD_SIZE;
    memset(pixels, 0xa5, bytes);
    plugin->on_compose_tick(api, pixels, width, height);
    for (i = 0; i < bytes; i++) {
        if (pixels[i] != 0xa5) {
            changed = 1;
            break;
        }
    }
    assert(changed == expect_change);
    for (i = 0; i < TEST_GUARD_SIZE; i++) {
        assert(allocation[i] == 0x5a);
        assert(allocation[TEST_GUARD_SIZE + bytes + i] == 0x5a);
    }
    free(allocation);
}

int main(void)
{
    static const struct roth_game_ram_api_v1 game_ram = {
        .u8 = test_u8,
        .u16 = test_u16,
        .u32 = test_u32,
        .set_u8 = NULL,
        .set_u16 = NULL,
        .set_u32 = NULL,
        .read_block = test_read_block,
        .write_block = NULL,
        .size = test_ram_size,
        .to_ptr = test_to_ptr
    };
    static const struct roth_window_v1 window = {
        .struct_size = sizeof(struct roth_window_v1),
        .set_window_mode = NULL,
        .set_present_mode = NULL,
        .set_min_present_interval_us = NULL,
        .get_window_state = test_get_window_state,
        .get_display_mode_count = NULL,
        .get_display_mode = NULL
    };
    static const struct roth_api_v1 api = {
        .abi_major = ROTH_ABI_MAJOR,
        .abi_minor = ROTH_ABI_MINOR,
        .struct_size = sizeof(struct roth_api_v1),
        .engine = NULL,
        .game_ram = &game_ram,
        .plugin_dir = test_plugin_dir,
        .present = NULL,
        .window = &window
    };
    static const uint32_t sizes[][2] = {
        {320, 200}, {640, 480}, {1280, 720}, {3440, 1440}
    };
    const struct roth_plugin_info_v1 *plugin = roth_plugin_query_v1();
    struct debug_state_view sampled;
    struct roth_video_event_v1 video;
    struct roth_map_event_v1 map;
    size_t i;

    assert(plugin != NULL);
    assert(strcmp(plugin->id, "ire.roth.debug_overlay") == 0);
    assert(plugin->api_use ==
           (ROTH_API_USE_GAME_RAM | ROTH_API_USE_COMPOSE |
            ROTH_API_USE_INPUT));
    assert(plugin->on_register_overrides == NULL);
    assert(plugin->on_mouse_poll == NULL);

    memcpy(test_ram + 0x701ecu, "STUDY1.RAW", sizeof("STUDY1.RAW"));
    memcpy(test_ram + 0x7023cu, "M/DEMO.DAS", sizeof("M/DEMO.DAS"));
    test_put_u32(0x90aa8u, (uint32_t)(uintptr_t)test_geometry);
    test_put_u32(0x90aa4u, (uint32_t)(uintptr_t)test_objects);
    test_put_u16(0x90c12u, 0x0168);
    test_put_u32(0x91df8u, 10);
    test_put_u32(0x91dfcu, 0x0100);
    test_put_u16(0x90a8eu, (uint16_t)-100);
    test_put_u16(0x90a92u, 200);
    test_put_u16(0x90a96u, 300);
    test_put_u16(0x90a8au, 0x4000);
    test_put_u32(0x90a74u, (uint32_t)-20);
    test_put_u32(0x8c108u, (uint32_t)-18);
    test_ram[0x90a49u] = 3;
    test_put_u16(0x90a4au, 42);
    test_put_u16(0x90a4cu, 19);
    test_put_u16(0x90a4eu, 7);
    test_put_u16(0x90a50u, 0x0324);
    test_put_u16(0x90a52u, 0x0168);
    test_put_u16(0x90a54u, 0x5566);
    test_put_u32(0x90a56u, 0x778899aa);

    test_geometry_put_u16(4, 0x0100);
    test_geometry_put_u16(6, 0x0300);
    test_geometry_put_u16(0x00fe, 10);
    test_geometry_put_u16(0x02fe, 20);
    test_geometry_put_u16(0x0168, (uint16_t)-64);
    test_geometry_put_u16(0x016a, 128);
    test_geometry_put_u16(0x016e, 10);
    test_geometry_put_u16(0x0170, 11);
    test_geometry[0x0173] = 7;
    test_geometry_put_u16(0x017c, 123);
    test_geometry_put_u16(0x0180, 0x0500);
    test_geometry_put_u16(0x0500, 20);
    test_geometry_put_u16(0x0502, 32);
    test_geometry_put_u16(0x0506, 21);
    test_geometry_put_u16(0x0508, 64);
    test_geometry_put_u16(0x0328, 0x0600);
    test_geometry_put_u16(0x0602, 30);
    test_geometry_put_u16(0x0604, 31);
    test_geometry_put_u16(0x0606, 32);

    debug_state_reset();
    debug_state_sample(&api, &sampled);
    assert(sampled.raw_map_valid);
    assert(strcmp(sampled.raw_map_name, "STUDY1.RAW") == 0);
    assert(sampled.das_valid);
    assert(strcmp(sampled.das_name, "M/DEMO.DAS") == 0);
    assert(sampled.sector_index_valid);
    assert(sampled.player_sector_index == 4);
    assert(sampled.sector_details_valid);
    assert(sampled.sector_roof_height == -64);
    assert(sampled.sector_floor_height == 128);
    assert(sampled.sector_glow == 7);
    assert(sampled.sector_id == 123);
    assert(sampled.sector_texture_top == 10);
    assert(sampled.sector_texture_bottom == 11);
    assert(sampled.mid_platform_valid);
    assert(sampled.mid_platform_roof_height == 32);
    assert(sampled.mid_platform_floor_height == 64);
    assert(sampled.mid_platform_roof_texture == 20);
    assert(sampled.mid_platform_floor_texture == 21);
    assert(sampled.cursor_das_entry == 42);
    assert(sampled.cursor_texture_v == 19);
    assert(sampled.cursor_texture_u == 7);
    assert(sampled.cursor_face_record_offset == 0x0324);
    assert(sampled.cursor_sector_record_offset == 0x0168);
    assert(sampled.cursor_draw_flags == 0x5566);
    assert(sampled.cursor_render_record_ref == 0x778899aa);
    assert(sampled.cursor_face_index_valid);
    assert(sampled.cursor_face_index == 3);
    assert(sampled.cursor_sector_index_valid);
    assert(sampled.cursor_sector_index == 4);
    assert(sampled.cursor_face_textures_valid);
    assert(sampled.cursor_face_texture_middle == 30);
    assert(sampled.cursor_face_texture_top == 31);
    assert(sampled.cursor_face_texture_bottom == 32);
    /* A wall result names no object, and the object block says so rather than
     * reading a record out of a pointer that was never an object's. */
    assert(!sampled.cursor_object_valid);

    /* THE PLACED OBJECT UNDER THE CURSOR. Two sectors carry object lists: the
     * first holds two records and the second three, so the record staged in the
     * second is the map's own object number 3 -- the fourth record of the walk
     * -- and a fixture that only ever numbered one list could not tell that
     * from 0. The picture word 0x0013 is texture index 19 of source 0, which
     * the same editor's picking window prints as 4115. */
    test_objects_put_u16(2u, 0x0040);                 /* sector 0's list */
    test_objects_put_u16(4u, 0x0100);                 /* sector 1's list */
    test_objects[0x0040] = 2;                         /* two records */
    test_objects[0x0100] = 3;                         /* three records */
    test_objects_put_u16(0x0112u + 0x04u, 0x0013);    /* picture 19/0 = 4115 */
    test_objects_put_u16(0x0112u + 0x0eu, 36);        /* the record's ID tag */
    test_ram[0x90a49u] = 4;                           /* an object result */
    test_put_u32(0x90a56u, (uint32_t)(uintptr_t)(test_objects + 0x0112u));
    debug_state_sample(&api, &sampled);
    assert(sampled.cursor_object_valid);
    assert(sampled.cursor_object_index == 3);
    assert(sampled.cursor_object_graphic == 0x0013);
    assert(sampled.cursor_object_id == 36);
    /* And the face block is silent, because a face is not what was picked. */
    assert(!sampled.cursor_face_index_valid);

    /* An address inside the section that lands between records names nothing,
     * and nothing is what is reported. */
    test_put_u32(0x90a56u, (uint32_t)(uintptr_t)(test_objects + 0x0118u));
    debug_state_sample(&api, &sampled);
    assert(!sampled.cursor_object_valid);

    /* Nor does a record of the right shape under a result kind that was never
     * written for one. */
    test_ram[0x90a49u] = 3;
    test_put_u32(0x90a56u, (uint32_t)(uintptr_t)(test_objects + 0x0112u));
    debug_state_sample(&api, &sampled);
    assert(!sampled.cursor_object_valid);
    /* Left standing on the object for the drawing passes below, so the line
     * that prints it is one the layouts really compose. */
    test_ram[0x90a49u] = 4;

    plugin->on_load(&api);
    assert(debug_input_mode() == DEBUG_LAYOUT_PERFORMANCE);
    memset(&map, 0, sizeof(map));
    map.struct_size = sizeof(map);
    plugin->on_map_loaded(&api, &map);

    test_present(plugin, &api, 1, 1, 1000000000ull);
    test_present(plugin, &api, 2, 1, 1010000000ull);
    plugin->on_frame_game(&api);

    memset(&video, 0, sizeof(video));
    video.struct_size = sizeof(video);
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        video.pixel_w = sizes[i][0];
        video.pixel_h = sizes[i][1];
        video.aspect_w = 4;
        video.aspect_h = 3;
        plugin->on_video_mode_changed(&api, &video);
        plugin->on_frame_game(&api);
        test_compose_size(plugin, &api, sizes[i][0], sizes[i][1], 1);
    }

    assert(plugin->on_scancode(&api, 0x58) == 0x58);
    assert(debug_input_mode() == DEBUG_LAYOUT_PERFORMANCE);
    assert(plugin->on_scancode(&api, 0x57) == 0);
    assert(debug_input_mode() == DEBUG_LAYOUT_INSPECTOR);
    assert(plugin->on_scancode(&api, 0xd7) == 0xd7);
    plugin->on_frame_game(&api);
    test_compose_size(plugin, &api, 320, 200, 1);

    assert(plugin->on_scancode(&api, 0x57) == 0);
    assert(debug_input_mode() == DEBUG_LAYOUT_DETAILED);
    plugin->on_frame_game(&api);
    test_compose_size(plugin, &api, 320, 200, 1);
    assert(plugin->on_scancode(&api, 0x57) == 0);
    assert(debug_input_mode() == DEBUG_LAYOUT_ALL);
    plugin->on_frame_game(&api);
    test_compose_size(plugin, &api, 640, 480, 1);
    assert(plugin->on_scancode(&api, 0x57) == 0);
    assert(debug_input_mode() == DEBUG_LAYOUT_OFF);
    plugin->on_frame_game(&api);
    test_compose_size(plugin, &api, 320, 200, 0);
    assert(plugin->on_scancode(&api, 0x57) == 0);
    assert(debug_input_mode() == DEBUG_LAYOUT_PERFORMANCE);
    plugin->on_frame_game(&api);
    test_compose_size(plugin, &api, 320, 200, 1);

    plugin->on_unload(&api);
    return 0;
}
