#include "hwr_visibility.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The software renderer walks the world every frame into a scratch arena that
 * the engine allocates once and publishes as a stored pointer. The upper half
 * of that arena is the node window the renderer's face pass addresses through a
 * selector; the mod reads it through the same window base. */
#define VA_G_RENDER_ARENA_PTR 0x8498cu
#define HWR_RENDER_ARENA_WINDOW 0x8000u

/* Within the node window, the renderer's per-frame ordered draw list is a u16
 * entry count here, then that many u16 node offsets immediately after. Each
 * entry points at a node in the same window. */
#define HWR_DRAW_LIST_COUNT   0xfc00u
#define HWR_DRAW_LIST_ENTRIES 0xfc02u

/* The node window is this many bytes; every offset the list yields is validated
 * against it before being dereferenced. */
#define HWR_SECTION_EXTENT 0x10000u

/* A node holds its face chain at +0x06; each chain link carries the drawn
 * sector's record offset at +0x02 and the next link at +0x00. A link whose
 * record offset is this value was coalesced away by the renderer's interval
 * merge -- its sector still draws through the surviving link that absorbed it,
 * so the link is skipped, not treated as a sector. */
#define HWR_NODE_FACE_HEAD 0x06u
#define HWR_NODE_LINK_NEXT 0x00u
#define HWR_NODE_LINK_REC  0x02u
#define HWR_NODE_REMOVED   0xffffu

/* A face chain cannot be longer than the node arena; this caps the walk so a
 * malformed link can never spin. */
#define HWR_CHAIN_LIMIT 0x2000u

/* The per-frame visibility gate is on by default. HWR_PVS=0 is the kill-switch:
 * it submits every sector's surfaces each frame instead of reading the
 * renderer's own drawn-sector set. */
int hwr_pvs_enabled(void)
{
    static int state = -1;
    if (state < 0) {
        const char *setting = getenv("HWR_PVS");
        state = setting != NULL && setting[0] == '0' ? 0 : 1;
    }
    return state;
}

/* Debug: keep the gate active but mark every sector visible, so the culling
 * plumbing can be exercised without changing what is drawn. */
static int hwr_pvs_all(void)
{
    static int state = -1;
    if (state < 0) {
        const char *setting = getenv("HWR_PVS_ALL");
        state = setting != NULL && setting[0] == '1' ? 1 : 0;
    }
    return state;
}

static uint16_t hwr_pvs_u16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
}

/* Reads a u16 from the section at `offset`; clears *ok and returns 0 when the
 * read would run past the section, so a stray offset can never dereference out
 * of bounds. */
static uint16_t hwr_pvs_section_u16(const uint8_t *section, uint32_t offset,
                                    int *ok)
{
    if (offset > HWR_SECTION_EXTENT - 2u) {
        *ok = 0;
        return 0;
    }
    return hwr_pvs_u16(section + offset);
}

int hwr_compute_pvs(const struct hwr_world *world,
                    const struct hwr_gl_camera *camera,
                    const struct roth_api_v1 *api,
                    uint8_t *visible)
{
    const uint8_t *section;
    uintptr_t arena;
    size_t sector_count;
    size_t marked = 0;
    uint32_t count;
    uint32_t entries_end;
    uint32_t k;
    int ok = 1;

    (void)camera;   /* the renderer's own walk supplies the set now */

    if (world == NULL || api == NULL || api->game_ram == NULL || visible == NULL)
        return 0;
    sector_count = world->sector_count;
    if (sector_count == 0)
        return 0;
    if (!hwr_pvs_enabled())
        return 0;                                /* kill-switch: all visible */
    if (hwr_pvs_all()) {
        memset(visible, 1, sector_count);
        return 1;
    }
    if (world->sector_offset_to_index == NULL)
        return 0;                                /* no map staged: all visible */
    /* The arena lives in engine heap outside the bounded accessor window; its
     * address is published in a pointer global, and the plugin shares the
     * process space, so the node window is read directly from base+window --
     * exactly how the world sections are reached. */
    arena = (uintptr_t)api->game_ram->u32(VA_G_RENDER_ARENA_PTR);
    if (arena < 0x1000u)
        return 0;                                /* arena not allocated yet */
    section = (const uint8_t *)(arena + HWR_RENDER_ARENA_WINDOW);

    count = hwr_pvs_section_u16(section, HWR_DRAW_LIST_COUNT, &ok);
    entries_end = HWR_DRAW_LIST_ENTRIES + count * 2u;
    /* No list, or one that cannot fit the section, means the renderer drew
     * nothing through the ordered path this frame -- a menu, a transition, or a
     * frame captured before the walk ran. Fall back to all-visible so no
     * geometry is dropped. */
    if (!ok || count == 0 || entries_end > HWR_SECTION_EXTENT)
        return 0;

    memset(visible, 0, sector_count);
    for (k = 0; k < count; k++) {
        uint32_t node = hwr_pvs_section_u16(section, HWR_DRAW_LIST_ENTRIES + k * 2u, &ok);
        uint32_t chain = hwr_pvs_section_u16(section, node + HWR_NODE_FACE_HEAD, &ok);
        uint32_t guard = 0;
        if (!ok)
            return 0;                            /* truncated list: fail open */
        /* Each node hangs a chain of face records; every surviving record names
         * the drawn sector at +0x02, and a coalesced-away record is skipped. */
        while (chain != 0 && guard++ < HWR_CHAIN_LIMIT) {
            uint16_t recid = hwr_pvs_section_u16(section, chain + HWR_NODE_LINK_REC, &ok);
            if (!ok)
                return 0;
            if (recid != HWR_NODE_REMOVED) {
                uint16_t index = world->sector_offset_to_index[recid];
                if (index == UINT16_MAX || index >= sector_count)
                    return 0;                    /* unmappable offset: fail open */
                if (!visible[index]) {
                    visible[index] = 1;
                    marked++;
                }
            }
            chain = hwr_pvs_section_u16(section, chain + HWR_NODE_LINK_NEXT, &ok);
            if (!ok)
                return 0;
        }
        if (guard >= HWR_CHAIN_LIMIT)
            return 0;                            /* runaway chain: fail open */
    }

    /* A list that produced no drawn sector (every chain empty) is not a real
     * cull result; fall back to all-visible rather than blank the world. */
    if (marked == 0)
        return 0;
    return 1;
}
