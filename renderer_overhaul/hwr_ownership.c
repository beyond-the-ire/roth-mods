#include "hwr_ownership.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Cleared at the top of the map loader and set as the first statement of the
 * world render pass, so a wrap around that pass reads the pre-render value: a
 * zero means a load has happened since the mod last drew (map_load.c:422,
 * render_world.c:337 -- the only two writers). */
#define VA_G_RENDER_ACTIVE          0x89f38u
/* Published addresses of the two map geometry sections a snapshot points at. */
#define VA_G_MAP_GEOMETRY_BUFFER    0x90aa8u
#define VA_G_VERTEX_GEOMETRY_BASE   0x90aacu
/* Non-zero while a cutscene sequence holds a backed-up gameplay screen: taken
 * before the first screen slide and released after the last one
 * (gdv_cutscene.c:2818/2465, menu_hud_ui.c:1200, blit_2d.c:765). */
#define VA_G_SCREEN_BACKUP_HANDLE   0x83d18u
/* Raised at the end of video playback and cleared when the overlay is torn
 * down, so it also covers the option-list state between two cutscenes
 * (gdv_cutscene.c:2735/2440). */
#define VA_G_CUTSCENE_OVERLAY       0x83c70u
/* The pain accumulator the palette upload turns into a screen-wide tint. */
#define VA_G_DAMAGE_FLASH_LEVEL     0x89f3bu
/* The engine's desaturation table and the flag that says it has been built. */
#define VA_G_VIEW_GRAYSCALE_LUT     0x7f254u
#define VA_G_VIEW_GRAYSCALE_VALID   0x7f354u

void hwr_ownership_init(struct hwr_ownership *own)
{
    const char *setting;
    if (own == NULL)
        return;
    memset(own, 0, sizeof *own);
    setting = getenv("HWR_STATE_LOG");
    own->log = setting != NULL && setting[0] != 0 && setting[0] != '0';
}

void hwr_ownership_clear(struct hwr_ownership *own)
{
    if (own == NULL)
        return;
    free(own->blank);
    own->blank = NULL;
    own->blank_pixels = 0;
}

const char *hwr_ownership_name(int state)
{
    switch (state) {
    case HWR_OWNER_SOFTWARE:      return "software";
    case HWR_OWNER_PASSTHROUGH:   return "passthrough";
    case HWR_OWNER_HARDWARE:      return "hardware";
    case HWR_OWNER_FROZEN:        return "frozen";
    case HWR_OWNER_ENGINE_SCREEN: return "engine-screen";
    default:                      return "unknown";
    }
}

void hwr_ownership_enter(struct hwr_ownership *own, int state,
                         const char *reason)
{
    if (own == NULL || own->state == state)
        return;
    if (own->log)
        fprintf(stderr, "[renderer_overhaul] ownership %s -> %s (%s)\n",
                hwr_ownership_name(own->state), hwr_ownership_name(state),
                reason != NULL ? reason : "");
    own->state = state;
    own->transitions++;
}

static const void *hwr_ownership_stored_pointer(const struct roth_api_v1 *api,
                                                uint32_t pointer_global)
{
    uintptr_t stored = (uintptr_t)api->game_ram->u32(pointer_global);
    return stored >= 0x1000u ? (const void *)stored : NULL;
}

int hwr_ownership_world_stale(const struct roth_api_v1 *api,
                              const void *sector_section,
                              const void *vertex_section)
{
    if (api == NULL || api->game_ram == NULL)
        return 1;
    if (sector_section == NULL || vertex_section == NULL)
        return 1;
    if (api->game_ram->u8(VA_G_RENDER_ACTIVE) == 0)
        return 1;
    return hwr_ownership_stored_pointer(api, VA_G_MAP_GEOMETRY_BUFFER) !=
               sector_section ||
           hwr_ownership_stored_pointer(api, VA_G_VERTEX_GEOMETRY_BASE) !=
               vertex_section;
}

int hwr_ownership_engine_sequence(const struct roth_api_v1 *api)
{
    if (api == NULL || api->game_ram == NULL)
        return 0;
    return api->game_ram->u32(VA_G_SCREEN_BACKUP_HANDLE) != 0 ||
           api->game_ram->u32(VA_G_CUTSCENE_OVERLAY) != 0;
}

int32_t hwr_ownership_damage_level(const struct roth_api_v1 *api)
{
    if (api == NULL || api->game_ram == NULL)
        return 0;
    return (int32_t)api->game_ram->u32(VA_G_DAMAGE_FLASH_LEVEL);
}

const uint8_t *hwr_ownership_grayscale_lut(const struct roth_api_v1 *api)
{
    if (api == NULL || api->game_ram == NULL)
        return NULL;
    if (api->game_ram->u8(VA_G_VIEW_GRAYSCALE_VALID) == 0)
        return NULL;
    return (const uint8_t *)api->game_ram->to_ptr(VA_G_VIEW_GRAYSCALE_LUT);
}

const uint32_t *hwr_ownership_blank_canvas(struct hwr_ownership *own,
                                           uint32_t width, uint32_t height)
{
    size_t pixels;
    size_t i;

    if (own == NULL || width == 0 || height == 0)
        return NULL;
    pixels = (size_t)width * height;
    if (pixels > own->blank_pixels) {
        uint32_t *next = (uint32_t *)realloc(own->blank,
                                             pixels * sizeof(uint32_t));
        if (next == NULL)
            return NULL;
        own->blank = next;
        for (i = 0; i < pixels; i++)
            own->blank[i] = 0xff000000u;
        own->blank_pixels = pixels;
    }
    return own->blank;
}
