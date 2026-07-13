/* doc_viewer.c — a full-function-REPLACEMENT example plugin. Compiles against ONLY <roth_sdk.h> —
 * no engine.h, no repo headers — exactly like item_grabber.
 *
 * It replaces one verified engine function, render_inspect_popup_window (canon 0x18e9e), with a
 * modified copy of its real C source. Registration is a runtime override (on_register_overrides):
 *
 *     roth_override(reg, ROTH_FN_render_inspect_popup_window, dv_render_inspect_popup_window, 0)
 *
 * The host builds the immutable chain and patches the function's 5-NOP entry pad at boot; call-next
 * re-enters the recorded original body. This is a FULL replacement — the override callback never
 * calls roth_next while the feature is ACTIVE. The clean off-mode idiom: when disabled it just PASSES
 * THROUGH to roth_next (the pristine engine function), so off == byte-for-byte pristine.
 *
 * THE FEATURE: the inventory "inspect document" popup is a hardcoded 0x9f-tall box from the 320x200
 * era, so on a 640x400/640x480 screen it reads tiny. Three gated deltas [DV-A/-B/-C], all behind the
 * hires gate (g_screen_pitch >= 0x280):
 *   (A) enlarge the document popup (125% wide x 225% tall by default), clamped to the live view;
 *   (B) re-center the enlarged box vertically;
 *   (C) THE FRAME FIX — the native side-sprite height-patch over-reads ICONS.ALL past the art's true
 *       rows once the box grows -> garbage; instead cache each ornate column once at a safe height
 *       and pixel-copy it down the band with mirrored ping-pong tiling (item_grabber's spine trick).
 * Lowres / 320x400 double-scan (pitch < 0x280) run the original logic byte-for-byte.
 *
 * The replacement body reaches state through the SDK's MEMORY-ACCESS LAYER:
 *   - engine calls        -> api->engine->fn(...)   (all exposed in the engine-call table)
 *   - named-global reads    G8/G16/G32 of a VA  -> api->game_ram->u8/u16/u32(VA)
 *   - named-global writes    "G32(VA) = x"       -> api->game_ram->set_u32(VA, x)
 *   - VA -> host pointer   (GADDR, for a fn that reads a game_ram buffer) -> api->game_ram->to_ptr(VA)
 *   - HOST-PTR derefs      (relocatable reloc/DAS descriptors + node/p4 records the engine RETURNS or
 *                           passes) are followed DIRECTLY (RB/RW/RD/WW): a native in-process plugin
 *                           dereferences an engine-provided host pointer directly; it needs no header.
 *
 * CONTEXT: the override runs wherever the engine calls render_inspect_popup_window — the GAME-thread
 * inventory-UI path (engine calls legal).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strcasecmp (POSIX) — needs _GNU_SOURCE (see Makefile) */
#include <stdint.h>

#include "roth_sdk.h"

/* ---- canonical VAs (game_ram offsets; the frozen 1996 layout — just numbers, no g_names.h) ---- */
#define VA_g_inspect_popup_state        0x810c0u
#define VA_g_inspect_page_count         0x810fcu
#define VA_g_inspect_current_page       0x81184u
#define VA_g_inspect_popup_layout       0x80b42u
#define VA_g_inventory_panel_open       0x7fec4u
#define VA_g_inventory_inspect_request  0x80b3cu
#define VA_g_inventory_ui_action        0x80b30u
#define VA_g_selected_item_primary      0x81044u
#define VA_g_choice_interaction_mode    0x83acdu
#define VA_g_ui_panel_anchor_y          0x80b28u
#define VA_g_map_menu_marker_selected   0x7675du
#define VA_g_screen_pitch               0x85498u
#define VA_g_screen_height              0x854a0u
#define VA_g_hires_line_doubling_flag   0x90cbdu

/* the host API table, shared across the callbacks (immutable). Set at each callback entry — the
 * override runs single-threaded on the game-thread UI path. */
static const struct roth_api_v1 *g_api;
#define GR (g_api->game_ram)
#define E  (g_api->engine)
/* named-global reads (G8/G16/G32 -> bounded little-endian accessors; G32 keeps the signed
 * interpretation the copy uses) and writes (the "G32(VA) = x" lvalue -> set_u32). */
#define R8(o)    (GR->u8((uint32_t)(o)))
#define R16(o)   (GR->u16((uint32_t)(o)))
#define R32(o)   ((int32_t)GR->u32((uint32_t)(o)))
#define W8(o,v)  (GR->set_u8((uint32_t)(o),  (uint8_t)(v)))
#define W16(o,v) (GR->set_u16((uint32_t)(o), (uint16_t)(v)))
#define W32(o,v) (GR->set_u32((uint32_t)(o), (uint32_t)(v)))
#define GADDR(o) (GR->to_ptr((uint32_t)(o)))

/* raw flat read/write of RUNTIME host pointers (records the engine returns/passes — reloc & DAS
 * descriptors, the node record, the double-indirect close-up-image slot p4). Deref RAW: a native
 * in-process plugin follows an engine-provided host pointer directly. */
#define RB(a)    (*(volatile uint8_t  *)(uintptr_t)(a))
#define RW(a)    (*(volatile uint16_t *)(uintptr_t)(a))
#define RD(a)    (*(volatile uint32_t *)(uintptr_t)(a))
#define WW(a,v)  (*(volatile uint16_t *)(uintptr_t)(a) = (uint16_t)(v))

/* ---- side-sprite scratch caps (the ornate document columns 0xd0 left / 0xe0 right are narrow) ---- */
#define DV_SIDE_W   64
#define DV_SIDE_H   256

/* ---- config (resolved once in on_load; cfg then env-override, env WINS; all fail-soft) ---- */
static int      dv_enabled  = 0;     /* 0 = passthrough to the pristine engine fn (default until on_load) */
static uint32_t dv_scale_w  = 125;   /* enlarged width  = clamp(box_w * pct/100); 125% */
static uint32_t dv_scale_h  = 225;   /* enlarged height = clamp(box_h * pct/100); 225% */
static uint32_t dv_side_h_l = 150;   /* safe SOURCE height for the tiled left column (item_grabber-proven) */
static uint32_t dv_side_h_r = 150;   /* ...and the right column (its mirror pair) */
static int      dv_dump     = 0;     /* ROTH_DOC_VIEWER_DUMP=1: log geometry + side-sprite dims once */

static char *dv_trim(char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return s;
}

/* parse "WxH" (single value = both) into the two out params, clamping each to [lo,hi]. cfg + env. */
static void dv_parse_pair(const char *s, uint32_t *a, uint32_t *b, int lo, int hi, const char *seps)
{
    int va = atoi(s), vb = va;
    const char *xp = strpbrk(s, seps);
    if (xp) vb = atoi(xp + 1);
    if (va >= lo && va <= hi) *a = (uint32_t)va;
    if (vb >= lo && vb <= hi) *b = (uint32_t)vb;
}

/* ================================ THE FRAME FIX ================================
 * dv_tile_side_band — replaces the native side-sprite mechanism (patch descriptor +6 to boxh-3, then
 * auto-scale-blit, which over-reads ICONS.ALL once the box is enlarged) for one ornate document
 * column: blit the sprite ONCE into a private scratch at a SAFE height (force +6=src_h, blit 1:1 via
 * blit_das_image_to_buffer mode 1, RESTORE +6 — the resting +6 is runtime-stale, never trusted), then
 * pixel-copy the scratch down the band with MIRRORED ping-pong repeats (no hard cut). Writes through
 * the engine framebuffer (hires y-doubling honored). Game-thread only (engine calls). */
static void dv_tile_side_band(uint32_t slot, int32_t x, int32_t esi, int32_t band_h, uint32_t src_h)
{
    if (band_h <= 0 || x < 0)
        return;
    uint32_t desc = E->resolve_reloc_ptr(slot);
    if (desc == 0)
        return;
    uint32_t sw = RW(desc + 4);                      /* sprite width (descriptor +4) */
    uint32_t sh = src_h;                             /* SAFE source height (never trust resting +6) */
    if (sw == 0 || sw > DV_SIDE_W || sh == 0 || sh > DV_SIDE_H)
        return;

    static uint8_t tex[DV_SIDE_W * DV_SIDE_H];       /* private scratch (game thread only) */
    memset(tex, 0, (size_t)sw * sh);
    uint16_t *hp = (uint16_t *)(uintptr_t)(desc + 6);
    uint16_t saved = *hp;
    *hp = (uint16_t)sh;
    E->blit_das_image_to_buffer(desc, (uint32_t)(uintptr_t)tex, sw, 1);   /* pitch=sw, mode 1 = 1:1 */
    *hp = saved;

    if (dv_dump) {
        fprintf(stderr, "[mods:doc_viewer] side slot=0x%x desc_w=%u resting_h=%u src_h=%u "
                "band_h=%d at x=%d y=%d\n", slot, sw, saved, sh, band_h, x, esi);
    }

    int      hires = (R8(VA_g_hires_line_doubling_flag) != 0);
    uint32_t pitch = (uint32_t)R32(VA_g_screen_pitch);
    if (pitch == 0 || (uint32_t)x >= pitch)
        return;
    uint32_t n = ((uint32_t)x + sw <= pitch) ? sw : (pitch - (uint32_t)x);   /* clamp to the row */
    uint32_t period = 2u * sh;
    for (int32_t r = 0; r < band_h; r++) {
        uint32_t p = (uint32_t)r % period;
        uint32_t srow = (p < sh) ? p : (period - 1u - p);   /* ping-pong; first segment unmirrored */
        const uint8_t *src = tex + (uintptr_t)srow * sw;
        uint8_t *dst = E->screen_xy_to_framebuffer_ptr(x, esi + r);   /* physical row (hires y-doubled) */
        memcpy(dst, src, n);
        if (hires) memcpy(dst + pitch, src, n);   /* native doubled-row blit: 1 source row -> 2 phys */
    }
}

/* ==================== render_inspect_popup_window (0x18e9e) — the override ====================
 * EAX=width, EDX=height, EBX=node record, ECX=p4 (double-indirect close-up-image slot). Returns the
 * saved-region handle. When the feature is disabled the override PASSES THROUGH to roth_next (the
 * pristine engine function); when enabled it runs the modified copy (a verbatim transcription of the
 * verified lift with the three gated [DV] changes; every engine call goes through api->engine). */
static uint32_t dv_render_inspect_popup_window(struct roth_chain *chain, const struct roth_api_v1 *api,
                                               uint32_t width, uint32_t height,
                                               uint32_t node, uint32_t p4)
{
    if (!dv_enabled)
        return roth_next_render_inspect_popup_window(chain, width, height, node, p4);
    g_api = api;

    int dv_big = 0;                                   /* [DV] set iff the enlargement grew the box ->
                                                       * the frame fix applies (a normal-size popup has
                                                       * no over-read; its side sprites use the original
                                                       * blit, pixel-identical to native). */
    int32_t name_w = 0;                               /* [ebp-0x10] */

    /* 1. reset the layout globals */
    W32(VA_g_inspect_popup_state + 0x38, 0);  W32(VA_g_inspect_page_count + 0x84, 0);
    W16(VA_g_inspect_popup_layout, 6);
    W16(VA_g_inspect_popup_layout + 0x8, 0);  W16(VA_g_inspect_popup_layout + 0x10, 0);  W16(VA_g_inspect_popup_layout + 0x18, 0);  W16(VA_g_inspect_popup_layout + 0x20, 0);  W16(VA_g_inspect_popup_layout + 0x28, 0);
    W16(VA_g_inspect_popup_layout + 0x4, 0x1c);
    W16(VA_g_inspect_popup_layout + 0xc, 0x14);  W16(VA_g_inspect_popup_layout + 0x14, 0x14);  W16(VA_g_inspect_popup_layout + 0x24, 0x14);  W16(VA_g_inspect_popup_layout + 0x2c, 0x14);
    W32(VA_g_inventory_panel_open + 0x8, 0);

    /* 2. base dims */
    int32_t h = (int32_t)height;                      /* [ebp-8] */
    W32(VA_g_inspect_popup_state + 0x14, (int32_t)width);
    W32(VA_g_inspect_popup_state + 0x18, (int32_t)height);
    int32_t w = (int32_t)width + 0x28;                /* [ebp-0x3c] */

    /* 3. widen for the item name (only when the name flag byte 0x8105c is set) */
    if (R8(VA_g_selected_item_primary + 0x18) != 0) {
        name_w = E->measure_control_text_width((const char *)GADDR(VA_g_selected_item_primary + 0x1a));
        if (name_w < 0x38) name_w = 0x38;
        name_w += 0x28;
        if (name_w > w) w = name_w;
    }

    /* 4. height baseline + pitch-dependent minimum popup width (used only by the page block) */
    int32_t min_w = 0x10e;
    h += 0x13;
    {
        uint32_t pitch = (uint32_t)R32(VA_g_screen_pitch);
        if (pitch > 0x17c)       min_w = 0x154;
        else if (pitch >= 0x168) min_w = 0x130;
    }

    /* 5. document-page count (node+4 bit 2) */
    W32(VA_g_inspect_page_count, 0);
    if (RB(node + 4) & 2) {
        uint32_t pages = E->copy_record_block_op7(node,
                            (uint32_t)(uintptr_t)GADDR(VA_g_inspect_page_count + 0x4), 0x20);
        W32(VA_g_inspect_page_count, (int32_t)pages);
        if (pages != 0) {
            if (pages <= (uint32_t)R32(VA_g_inspect_current_page))
                W32(VA_g_inspect_current_page, (int32_t)(pages - 1));
            min_w += 0x28;
            if (min_w > w) w = min_w;
            h = 0x9f;
            /* [DV-A] enlarge the document popup in wide/hi-res modes (g_screen_pitch>=640). Clamp to
             * the live logical view; only GROW; lowres/320x400 (pitch<640) untouched. */
            if ((uint32_t)R32(VA_g_screen_pitch) >= 0x280u) {
                int32_t log_w = R32(VA_g_screen_pitch);                         /* x extent */
                int32_t log_h = R32(VA_g_screen_height);                        /* physical y bound */
                if (R8(VA_g_hires_line_doubling_flag) != 0) log_h >>= 1;        /* -> logical rows */
                int32_t tw = (int32_t)((uint32_t)w * dv_scale_w / 100u);
                int32_t th = (int32_t)((uint32_t)h * dv_scale_h / 100u);
                if (tw > log_w - 0x20) tw = log_w - 0x20;                       /* keep a side margin */
                if (th > log_h - 0x10) th = log_h - 0x10;                       /* keep on-screen */
                if (tw > w) w = tw;                                             /* never shrink */
                if (th > h) { h = th; dv_big = 1; }   /* [DV] box grew taller -> the frame fix applies */
            }
        }
    }

    /* 6. choice menu -> widen to the choice bbox */
    int32_t nchoices = (int32_t)E->build_available_choice_menu(node);  /* [ebp-0x24] */
    if (nchoices != 0) {
        int32_t bx = R32(VA_g_choice_interaction_mode + 0x8) - R32(VA_g_choice_interaction_mode + 0x4) + 0x26;
        int32_t by = R32(VA_g_choice_interaction_mode + 0xc) + 0x12;
        if (bx > w) w = bx;
        if (by > h) h = by;
    }

    /* 7. center the window + publish the choice-panel origin globals */
    int32_t esi = R32(VA_g_ui_panel_anchor_y) + 0x41 - h;             /* window y [esi] */
    if (esi < 4) esi = 4;
    /* [DV-B] re-center the enlarged document popup vertically (the native placement bottom-anchors the
     * box, so a much taller box pins to the top; move it down toward center). Wide-mode document only. */
    if ((uint32_t)R32(VA_g_screen_pitch) >= 0x280u && R32(VA_g_inspect_page_count) != 0) {
        int32_t log_h = R32(VA_g_screen_height);
        if (R8(VA_g_hires_line_doubling_flag) != 0) log_h >>= 1;
        int32_t ce = (log_h - h) >> 1;                                          /* centered box top */
        if (ce > esi) esi = ce;                                                 /* move down only */
    }
    int32_t a0    = (w - 0x28 - R32(VA_g_inspect_popup_state + 0x14)) >> 1;    /* sar */
    W32(VA_g_choice_interaction_mode + 0x14, esi + 6 + (h >> 1));                  /* sar h */
    int32_t pitch = R32(VA_g_screen_pitch);
    int32_t edi   = (int32_t)((uint32_t)(pitch - w) >> 1);   /* shr; window x [edi] */
    edi += 0x24;
    edi += a0;
    int32_t l20   = ((h - 0x13 - R32(VA_g_inspect_popup_state + 0x18)) >> 1) + 0xf;  /* sar; [ebp-0x20] */
    edi &= ~3;                                          /* and di,0xfffc */
    int32_t win_x = edi;                               /* [ebp-0x1c] (pre-adjust window x) */
    int32_t ebx   = edi + (R32(VA_g_inspect_popup_state + 0x14) >> 1);         /* sar width0 */
    edi -= a0 + 0x24;                                  /* edi now = window_x - (a0+0x24) */
    ebx -= (int32_t)((uint32_t)(pitch - 0x50) >> 1);   /* shr */
    W32(VA_g_choice_interaction_mode + 0x10, ebx);

    /* 8. save the framebuffer region under the popup */
    int32_t save_w = w + 9;
    w += 1;                                            /* inc [ebp-0x3c] */
    uint32_t ret_region = E->save_framebuffer_region((uint32_t)(edi - 4), (uint32_t)(esi - 4),
                              (uint32_t)save_w, (uint32_t)(h + 8), (uint32_t *)0);  /* 0x13062 */
    if (ret_region == 0)
        return 0;

    /* 9. content pointer + geometry words */
    W32(VA_g_inspect_popup_state + 0x10, (uint32_t)(uintptr_t)E->screen_xy_to_framebuffer_ptr(win_x, l20 + esi));
    W16(VA_g_inspect_popup_state + 0x28, (uint16_t)w);
    W16(VA_g_inspect_popup_state + 0x2a, (uint16_t)h);
    W16(VA_g_inspect_popup_state + 0x1c, (uint16_t)win_x);
    W16(VA_g_inspect_popup_state + 0x1e, (uint16_t)(l20 + esi));
    W16(VA_g_inspect_popup_state + 0x20, (uint16_t)R32(VA_g_inspect_popup_state + 0x14));
    W16(VA_g_inspect_popup_state + 0x22, (uint16_t)R32(VA_g_inspect_popup_state + 0x18));
    W16(VA_g_inspect_popup_state + 0x2c, (uint16_t)(edi + 0x24));
    W16(VA_g_inspect_popup_state + 0x2e, (uint16_t)(esi + 0xf));
    int32_t l4   = w - 0x29;                            /* [ebp-4] */
    W16(VA_g_inspect_popup_state + 0x30, (uint16_t)l4);
    W16(VA_g_inspect_popup_state + 0x32, (uint16_t)(h - 0x13));
    W16(VA_g_inspect_popup_state + 0x24, (uint16_t)edi);
    W16(VA_g_inspect_popup_state + 0x26, (uint16_t)esi);
    int32_t l30  = w - 1;                               /* [ebp-0x30] */

    /* 10. frame border + interior clear */
    E->draw_popup_shadow_border_smc((int32_t)edi, (int32_t)esi, (int32_t)(w - 1), (int32_t)h);  /* 0x12dde */
    E->clear_framebuffer_rect((uint32_t)edi, (uint32_t)esi, (uint32_t)l30, (uint32_t)h);         /* 0x12cea */

    /* 11. panel content-origin globals + optional scrollbar block */
    W16(VA_g_inspect_popup_layout + 0x2, (uint16_t)(edi + 4));
    uint16_t x8  = (uint16_t)(edi + 8);
    W16(VA_g_inspect_popup_layout + 0xa, x8);  W16(VA_g_inspect_popup_layout + 0x12, x8);  W16(VA_g_inspect_popup_layout + 0x22, x8);  W16(VA_g_inspect_popup_layout + 0x2a, x8);
    if (R32(VA_g_inspect_page_count) != 0) {
        W16(VA_g_inspect_popup_layout + 0x16, (uint16_t)(esi + 0x10));
        W16(VA_g_inspect_popup_layout + 0x18, (uint16_t)(h - 0x16));
        W16(VA_g_inspect_popup_layout + 0x1a, (uint16_t)(edi + 0x23));
        W32(VA_g_inspect_popup_state, 1);
        W16(VA_g_inspect_popup_layout + 0x1c, (uint16_t)l4);
    }

    /* 12. left/right document sprites.
     * [DV-C] THE FRAME FIX: when the box was ENLARGED (dv_big — the only case that over-reads), tile
     * each ornate column with mirrored ping-pong repeats of the true art (no over-read). Otherwise run
     * the ORIGINAL height-patch + auto-scale blit verbatim — byte-for-byte the pristine path. */
    if (dv_big) {
        dv_tile_side_band(0xd0, edi,         esi, h - 3, dv_side_h_l);   /* left column  */
        dv_tile_side_band(0xe0, w + edi - 5, esi, h - 3, dv_side_h_r);   /* right column */
    } else {
        uint32_t r = E->resolve_reloc_ptr(0xd0);                         /* resolve_reloc_ptr_dup */
        if (r != 0) {
            WW(r + 6, (uint16_t)(h - 3));
            uint8_t *dst = E->screen_xy_to_framebuffer_ptr(edi, esi);
            E->blit_das_image_auto_scale((uint32_t)(uintptr_t)dst, r, (uint32_t)R32(VA_g_screen_pitch));
        }
        r = E->resolve_reloc_ptr(0xe0);
        if (r != 0) {
            WW(r + 6, (uint16_t)(h - 3));
            uint8_t *dst = E->screen_xy_to_framebuffer_ptr(w + edi - 5, esi);
            E->blit_das_image_auto_scale((uint32_t)(uintptr_t)dst, r, (uint32_t)R32(VA_g_screen_pitch));
        }
    }

    /* 13. frame chrome corner/edge tiles */
    int32_t ey = h + esi - 3;
    {
        uint8_t *dst = E->screen_xy_to_framebuffer_ptr(edi + 4, esi + 4);
        E->blit_reloc_das_image((uint32_t)(uintptr_t)dst, 0x110, (uint32_t)R32(VA_g_screen_pitch));
    }
    {
        uint8_t *dst = E->screen_xy_to_framebuffer_ptr(edi, ey);
        E->blit_reloc_das_image((uint32_t)(uintptr_t)dst, 0xd8, (uint32_t)R32(VA_g_screen_pitch));
    }
    {
        uint8_t *dst = E->screen_xy_to_framebuffer_ptr(w + edi - 5, ey);
        E->blit_reloc_das_image((uint32_t)(uintptr_t)dst, 0xe8, (uint32_t)R32(VA_g_screen_pitch));
    }
    int32_t l18    = w - 0x29;                          /* [ebp-0x18] */
    int32_t edge_x = edi + 0x23;
    {
        uint8_t *dst = E->screen_xy_to_framebuffer_ptr(edge_x, esi);
        E->blit_reloc_das_image((uint32_t)(uintptr_t)dst, 0x300, (uint32_t)R32(VA_g_screen_pitch));
    }
    edge_x += 1;
    while ((uint32_t)l18 >= 0x3au) {
        uint8_t *dst = E->screen_xy_to_framebuffer_ptr(edge_x, esi);
        l18 -= 0x36;
        E->blit_reloc_das_image((uint32_t)(uintptr_t)dst, 0x308, (uint32_t)R32(VA_g_screen_pitch));
        edge_x += 0x36;
    }
    {
        uint8_t *dst = E->screen_xy_to_framebuffer_ptr(w + edi - 0x3e, esi);
        E->blit_reloc_das_image((uint32_t)(uintptr_t)dst, 0x310, (uint32_t)R32(VA_g_screen_pitch));
    }
    {
        int32_t stretch_ebx = (int32_t)R32(VA_g_screen_pitch) + ((w - 0x28) << 16);
        uint8_t *dst = E->screen_xy_to_framebuffer_ptr(edi + 0x23, ey);
        E->blit_reloc_das_image((uint32_t)(uintptr_t)dst, 0xf8, (uint32_t)stretch_ebx);
    }

    /* 14. close-up item image (p4 -> *p4 -> **p4) */
    int32_t lc = esi + 0x24;                            /* [ebp-0xc] */
    uint32_t pp = RD(p4);
    if (pp != 0) {
        uint32_t img = RD(pp);
        if (img != 0) {
            int32_t iy    = ((0x38 - (int32_t)(int16_t)RW(img + 6)) >> 2) + esi + 4;  /* sar 2 */
            int32_t scale = (R8(VA_g_hires_line_doubling_flag) == 0) ? 1 : 0;
            int32_t l2c   = R32(VA_g_screen_pitch);
            int32_t ix    = ((0x1c - (int32_t)(int16_t)RW(img + 4)) >> 1) + edi + 4;  /* sar 1 */
            scale += 1;
            uint8_t *dst = E->screen_xy_to_framebuffer_ptr(ix, iy);
            E->blit_item_icon(img, (uint32_t)(uintptr_t)dst, (uint32_t)l2c, (uint32_t)scale);  /* 0x13544 */
            W16(VA_g_inspect_popup_layout, 0x1c);
            W16(VA_g_inventory_inspect_request + 0x4, (uint16_t)(esi + 4));
        }
    }

    /* 15. item name text */
    if (name_w != 0) {
        W8(VA_g_selected_item_primary + 0x19, R8(VA_g_map_menu_marker_selected));
        W8(VA_g_selected_item_primary + 0x18, 1);
        E->draw_text_at_screen_xy((uint32_t)(uintptr_t)GADDR(VA_g_selected_item_primary + 0x18),
                                  (uint32_t)(edi + 0x23), (uint32_t)(esi + 2), 0);   /* 0x1a079 */
    }

    /* 16. mark the popup footprint dirty */
    E->register_dirty_rect((uint32_t)(edi - 4), (int32_t)(esi - 4),
                           (uint32_t)(w + edi + 3), (uint32_t)(h + esi + 4));   /* 0x15b5b */

    /* 17. up-arrow (node+4 bit 0x40) */
    int32_t l38 = 0, l34 = 0;                           /* [ebp-0x38], [ebp-0x34] */
    W32(VA_g_inventory_ui_action + 0x4, 0);
    edi += 0x11;
    if (RB(node + 4) & 0x40) {
        int32_t sprite;
        if (RB(node + 7) & 0x80) {
            sprite = 0x98;
        } else {
            sprite = 0xa0;
            W32(VA_g_inventory_ui_action + 0x4, 1);
        }
        uint32_t rec = E->resolve_reloc_record_fields(&l34, &l38, (uint32_t)sprite, 1);
        if (rec != 0) {
            W16(VA_g_inspect_popup_layout + 0x6, (uint16_t)(lc + l38));
            E->blit_das_image_at_xy(rec, (int32_t)(l34 + edi), (int32_t)(lc + l38));   /* 0x1a10a */
            W16(VA_g_inspect_popup_layout + 0x8, (uint16_t)RW(rec + 6));
            lc += (int32_t)(int16_t)RW(rec + 6) + 4;
        }
    }

    /* 18. down-arrow (choices present) */
    if (nchoices != 0) {
        l34 = 0; l38 = 0;
        uint32_t rec = E->resolve_reloc_record_fields(&l34, &l38, 0x88, 1);
        if (rec != 0) {
            W16(VA_g_inspect_popup_layout + 0xe, (uint16_t)(lc + l38));
            E->blit_das_image_at_xy(rec, (int32_t)(l34 + edi), (int32_t)(lc + l38));
            W16(VA_g_inspect_popup_layout + 0x10, (uint16_t)RW(rec + 6));
            lc += (int32_t)(int16_t)RW(rec + 6) + 4;
        }
    }

    /* 19. multi-page scroll-thumb extent */
    if ((uint32_t)R32(VA_g_inspect_page_count) > 1) {
        W16(VA_g_inspect_popup_layout + 0x1e, (uint16_t)(lc + l38));
        W16(VA_g_inspect_popup_layout + 0x26, (uint16_t)(lc + l38 + 0x17));
    }

    /* 20. return the saved-region handle */
    return ret_region;
}

/* ============================ SDK callbacks ============================ */

/* on_register_overrides(MAIN): install the FULL replacement of render_inspect_popup_window (0x18e9e).
 * Registered unconditionally when the plugin loads; the callback itself passes through to the pristine
 * engine when the feature is disabled — so installing doc_viewer always shows as "1 override written",
 * and enabled/disabled only changes whether the chain runs the modified copy or roth_next. */
static void on_register_overrides(const struct roth_api_v1 *api, struct roth_registrar_v1 *reg)
{
    (void)api;
    int r = roth_override(reg, ROTH_FN_render_inspect_popup_window, dv_render_inspect_popup_window, 0);
    fprintf(stderr, "[mods:doc_viewer] override register: render_inspect_popup_window (0x18e9e) = %d "
            "(0 = accepted)%s\n", r, dv_enabled ? "" : " — currently passthrough (disabled)");
}

/* on_load(MAIN): read doc_viewer.cfg beside plugin.so (api->plugin_dir()), then apply env overrides
 * (env WINS over cfg). cfg keys: enabled, scale="WxH", side_h="L,R". */
static void on_load(const struct roth_api_v1 *api)
{
    g_api = api;
    char path[2600];
    snprintf(path, sizeof path, "%s/doc_viewer.cfg", api->plugin_dir());

    int enabled = 1;                 /* cfg default = on */
    FILE *f = fopen(path, "r");
    if (f) {
        int lineno = 0;
        char line[256];
        while (fgets(line, sizeof line, f)) {
            lineno++;
            char *hsh = strchr(line, '#'); if (hsh) *hsh = 0;
            char *eq = strchr(line, '=');
            if (!eq) { if (*dv_trim(line)) fprintf(stderr, "[mods:doc_viewer] %s:%d: no '=' — skipped\n", path, lineno); continue; }
            *eq = 0;
            char *k = dv_trim(line), *v = dv_trim(eq + 1);
            if (!*k) continue;
            if (!strcasecmp(k, "enabled"))     enabled = atoi(v) != 0;
            else if (!strcasecmp(k, "scale"))  dv_parse_pair(v, &dv_scale_w, &dv_scale_h, 100, 400, "xX*");
            else if (!strcasecmp(k, "side_h")) dv_parse_pair(v, &dv_side_h_l, &dv_side_h_r, 1, DV_SIDE_H, ",xX*");
            else fprintf(stderr, "[mods:doc_viewer] %s:%d: unknown setting '%s' — skipped\n", path, lineno, k);
        }
        fclose(f);
    }

    /* env overrides (env WINS over cfg) */
    { const char *s = getenv("ROTH_DOC_VIEWER");       if (s) enabled = s[0] != '0'; }
    { const char *s = getenv("ROTH_DOC_VIEWER_SCALE"); if (s) dv_parse_pair(s, &dv_scale_w, &dv_scale_h, 100, 400, "xX*"); }
    { const char *s = getenv("ROTH_DOC_VIEWER_SIDE_H"); if (s) dv_parse_pair(s, &dv_side_h_l, &dv_side_h_r, 1, DV_SIDE_H, ",xX*"); }
    dv_dump = getenv("ROTH_DOC_VIEWER_DUMP") ? 1 : 0;

    dv_enabled = enabled ? 1 : 0;
    if (dv_enabled)
        fprintf(stderr, "[mods:doc_viewer] enabled (full replacement of render_inspect_popup_window "
                "0x18e9e; scale %u%%x%u%%, side-src %u/%u)\n",
                dv_scale_w, dv_scale_h, dv_side_h_l, dv_side_h_r);
    else
        fprintf(stderr, "[mods:doc_viewer] loaded but disabled (cfg enabled=0 / ROTH_DOC_VIEWER=0) — "
                "the override passes through to the pristine engine\n");
}

static const struct roth_plugin_info_v1 DOC_VIEWER = {
    .abi_major     = ROTH_ABI_MAJOR,
    .abi_minor     = ROTH_ABI_MINOR,
    .struct_size   = sizeof(struct roth_plugin_info_v1),
    .id            = "ire.roth.doc_viewer",
    .name          = "Document Viewer (enlarged inspect-modal + mirror-tiled frame)",
    .version       = "1.0.0",
    .sdk_req_major = ROTH_SDK_MAJOR,
    .sdk_req_minor = ROTH_SDK_MINOR,
    .api_use       = ROTH_API_USE_GAME_RAM | ROTH_API_USE_ENGINE,
    .on_load               = on_load,
    .on_game_ram_ready     = NULL,
    .on_frame_game         = NULL,
    .on_compose_tick       = NULL,
    .on_audio              = NULL,
    .on_unload             = NULL,
    .on_register_overrides = on_register_overrides,
    .on_scancode           = NULL,
    .on_mouse_poll         = NULL,
};

ROTH_PLUGIN_EXPORT const struct roth_plugin_info_v1 *roth_plugin_query_v1(void)
{
    return &DOC_VIEWER;
}
