#include "hwr_chrome.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VA_G_SCREEN_PITCH       0x85498u
#define VA_G_SCREEN_HEIGHT      0x854a0u
#define VA_G_FRAMEBUFFER_PTR    0x90a98u
#define VA_G_LINE_DOUBLING_FLAG 0x90cbdu

/* The engine's own screen bound, shared with the rest of the composite path. */
#define HWR_CHROME_MAX_SCREEN 4096u

int hwr_chrome_enabled(void)
{
    static int state = -1;
    if (state < 0) {
        const char *setting = getenv("HWR_CHROME_RECTS");
        state = setting != NULL && setting[0] == '0' ? 0 : 1;
    }
    return state;
}

static struct hwr_chrome_rect *hwr_chrome_find(struct hwr_chrome *chrome,
                                               uint32_t handle)
{
    unsigned i;
    if (handle == 0)
        return NULL;
    for (i = 0; i < chrome->count; i++)
        if (chrome->rects[i].handle == handle)
            return &chrome->rects[i];
    return NULL;
}

static void hwr_chrome_drop(struct hwr_chrome *chrome, unsigned index)
{
    if (index >= chrome->count)
        return;
    chrome->rects[index] = chrome->rects[chrome->count - 1u];
    chrome->count--;
}

/* A freed save-under has its handle slot zeroed by the pool
 * (renderer.c:10447), so a handle that still resolves to a block is still
 * live. The check is a net under the lifecycle hooks, not a substitute for
 * them: a handle outside the accessor's range is left to the hooks. */
static int hwr_chrome_handle_live(const struct roth_api_v1 *api,
                                  uint32_t handle)
{
    uintptr_t arena;
    uint32_t offset;

    if (api == NULL || api->game_ram == NULL)
        return 1;
    arena = (uintptr_t)api->game_ram->to_ptr(0);
    if (arena == 0 || (uintptr_t)handle < arena)
        return 1;
    offset = (uint32_t)((uintptr_t)handle - arena);
    if (offset > api->game_ram->size() - 4u)
        return 1;
    return api->game_ram->u32(offset) != 0;
}

static void hwr_chrome_sweep(struct hwr_chrome *chrome,
                             const struct roth_api_v1 *api)
{
    unsigned i = 0;
    while (i < chrome->count) {
        if (!hwr_chrome_handle_live(api, chrome->rects[i].handle))
            hwr_chrome_drop(chrome, i);
        else
            i++;
    }
}

void hwr_chrome_record(struct hwr_chrome *chrome, const struct roth_api_v1 *api,
                       uint32_t handle, uint32_t x, uint32_t y,
                       uint32_t width, uint32_t height)
{
    struct hwr_chrome_rect *rect;
    uint32_t pitch;
    uint32_t screen_height;
    uint32_t left;
    uint32_t right;
    uint32_t bottom;

    if (chrome == NULL || api == NULL || api->game_ram == NULL || handle == 0 ||
        !hwr_chrome_enabled())
        return;
    pitch = api->game_ram->u32(VA_G_SCREEN_PITCH);
    screen_height = api->game_ram->u32(VA_G_SCREEN_HEIGHT);
    if (pitch == 0 || pitch > HWR_CHROME_MAX_SCREEN ||
        screen_height == 0 || screen_height > HWR_CHROME_MAX_SCREEN)
        return;
    /* The save routine doubles y and height in the line-doubled modes and
     * aligns the horizontal edges out to dwords (blit_2d.c:1658-1661), so the
     * rect it really covers is this one. */
    if (api->game_ram->u8(VA_G_LINE_DOUBLING_FLAG) != 0) {
        y += y;
        height += height;
    }
    if (width == 0 || height == 0 || width > pitch || height > screen_height ||
        x >= pitch || y >= screen_height)
        return;
    left = x & ~3u;
    right = (x + width + 3u) & ~3u;
    if (right > pitch)
        right = pitch;
    bottom = y + height;
    if (bottom > screen_height)
        bottom = screen_height;
    if (left >= right || y >= bottom)
        return;

    rect = hwr_chrome_find(chrome, handle);
    if (rect == NULL) {
        if (chrome->count >= HWR_CHROME_MAX_RECTS)
            hwr_chrome_sweep(chrome, api);
        if (chrome->count >= HWR_CHROME_MAX_RECTS) {
            if (!chrome->overflow_logged) {
                fprintf(stderr,
                        "[renderer_overhaul] more open window regions than the chrome set holds; the oldest are no longer closed\n");
                chrome->overflow_logged = 1;
            }
            return;
        }
        rect = &chrome->rects[chrome->count++];
    }
    rect->handle = handle;
    rect->x = left;
    rect->y = y;
    rect->w = right - left;
    rect->h = bottom - y;
    rect->covered = 1;
}

void hwr_chrome_cover(struct hwr_chrome *chrome, uint32_t handle)
{
    struct hwr_chrome_rect *rect = chrome != NULL
                                 ? hwr_chrome_find(chrome, handle) : NULL;
    if (rect != NULL)
        rect->covered = 1;
}

void hwr_chrome_cover_draw(struct hwr_chrome *chrome,
                           const struct roth_api_v1 *api, int32_t x, int32_t y,
                           int32_t width, int32_t height)
{
    unsigned i;
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;

    if (chrome == NULL || api == NULL || api->game_ram == NULL ||
        chrome->count == 0 || width <= 0 || height <= 0)
        return;
    /* The band is drawn around the frame, four pixels out on every side
     * (blit_2d.c:1996-2008), in the routine's own logical rows. */
    left = (int64_t)x - 4;
    top = (int64_t)y - 4;
    right = (int64_t)x + width + 4;
    bottom = (int64_t)y + height + 4;
    /* The border routine takes a logical y and doubles it itself in the
     * line-doubled modes (blit_2d.c:182), and the recorded rects are the save
     * routine's already-doubled ones, so the extent doubles as a whole. */
    if (api->game_ram->u8(VA_G_LINE_DOUBLING_FLAG) != 0) {
        top += top;
        bottom += bottom;
    }
    for (i = 0; i < chrome->count; i++) {
        struct hwr_chrome_rect *rect = &chrome->rects[i];
        /* Containment, not overlap: a frame drawn from a given save-under
         * always fits inside the rect that save-under covers, so requiring it
         * keeps one window's frame from vouching for another window's rect. */
        if (left < (int64_t)rect->x || top < (int64_t)rect->y ||
            right > (int64_t)rect->x + rect->w ||
            bottom > (int64_t)rect->y + rect->h)
            continue;
        rect->covered = 1;
    }
}

void hwr_chrome_uncover(struct hwr_chrome *chrome, uint32_t handle)
{
    struct hwr_chrome_rect *rect = chrome != NULL
                                 ? hwr_chrome_find(chrome, handle) : NULL;
    if (rect != NULL)
        rect->covered = 0;
}

void hwr_chrome_release(struct hwr_chrome *chrome, uint32_t handle)
{
    unsigned i;
    if (chrome == NULL || handle == 0)
        return;
    for (i = 0; i < chrome->count; i++) {
        if (chrome->rects[i].handle == handle) {
            hwr_chrome_drop(chrome, i);
            return;
        }
    }
}

void hwr_chrome_reset(struct hwr_chrome *chrome)
{
    if (chrome != NULL)
        chrome->count = 0;
}

void hwr_chrome_clear(struct hwr_chrome *chrome)
{
    if (chrome == NULL)
        return;
    free(chrome->plane);
    chrome->plane = NULL;
    chrome->plane_capacity = 0;
    chrome->count = 0;
    chrome->overflow_logged = 0;
}

const uint8_t *hwr_chrome_baseline(struct hwr_chrome *chrome,
                                   const struct roth_api_v1 *api,
                                   const struct roth_world_frame_v1 *frame,
                                   const uint8_t *base)
{
    const uint8_t *framebuffer;
    uint8_t *plane = NULL;
    uint32_t pitch;
    uint32_t screen_height;
    uint32_t base_pitch;
    size_t required;
    unsigned i;

    if (chrome == NULL || api == NULL || api->game_ram == NULL ||
        frame == NULL || base == NULL || chrome->count == 0 ||
        !hwr_chrome_enabled())
        return base;
    if (frame->vx < 0 || frame->vy < 0 || frame->vw == 0 || frame->vh == 0)
        return base;
    pitch = api->game_ram->u32(VA_G_SCREEN_PITCH);
    screen_height = api->game_ram->u32(VA_G_SCREEN_HEIGHT);
    if (pitch == 0 || pitch > HWR_CHROME_MAX_SCREEN ||
        screen_height == 0 || screen_height > HWR_CHROME_MAX_SCREEN ||
        (uint32_t)frame->vx >= pitch ||
        frame->vw > pitch - (uint32_t)frame->vx ||
        (uint32_t)frame->vy >= screen_height ||
        frame->vh > screen_height - (uint32_t)frame->vy)
        return base;
    /* The engine's linear back buffer lives outside the accessor's range and
     * both the engine and the host present path address it through this stored
     * pointer, exactly as the baseline seed does. */
    framebuffer = (const uint8_t *)(uintptr_t)
        api->game_ram->u32(VA_G_FRAMEBUFFER_PTR);
    if (framebuffer == NULL)
        return base;
    base_pitch = frame->engine_base_pitch_bytes != 0
               ? frame->engine_base_pitch_bytes : frame->vw;
    if (base_pitch < frame->vw)
        return base;
    required = (size_t)frame->vh * base_pitch;

    hwr_chrome_sweep(chrome, api);
    for (i = 0; i < chrome->count; i++) {
        const struct hwr_chrome_rect *rect = &chrome->rects[i];
        uint32_t left;
        uint32_t top;
        uint32_t right;
        uint32_t bottom;
        uint32_t y;

        if (!rect->covered)
            continue;
        left = rect->x > (uint32_t)frame->vx ? rect->x : (uint32_t)frame->vx;
        top = rect->y > (uint32_t)frame->vy ? rect->y : (uint32_t)frame->vy;
        right = rect->x + rect->w;
        bottom = rect->y + rect->h;
        if (right > (uint32_t)frame->vx + frame->vw)
            right = (uint32_t)frame->vx + frame->vw;
        if (bottom > (uint32_t)frame->vy + frame->vh)
            bottom = (uint32_t)frame->vy + frame->vh;
        if (left >= right || top >= bottom)
            continue;
        for (y = top; y < bottom; y++) {
            const uint8_t *live = framebuffer + (size_t)y * pitch;
            size_t row = (size_t)(y - (uint32_t)frame->vy) * base_pitch;
            uint32_t x;
            for (x = left; x < right; x++) {
                size_t index = row + (x - (uint32_t)frame->vx);
                if (live[x] != base[index])
                    continue;
                if (plane == NULL) {
                    if (required > chrome->plane_capacity) {
                        uint8_t *grown = (uint8_t *)realloc(chrome->plane,
                                                            required);
                        if (grown == NULL)
                            return base;
                        chrome->plane = grown;
                        chrome->plane_capacity = required;
                    }
                    memcpy(chrome->plane, base, required);
                    plane = chrome->plane;
                }
                /* Derived from the live pixel, so it cannot equal it. */
                plane[index] = (uint8_t)(live[x] ^ 1u);
            }
        }
    }
    return plane != NULL ? plane : base;
}
