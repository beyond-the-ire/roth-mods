/* item_grabber.c — the F7 item-grabber overlay plugin. Compiles against ONLY <roth_sdk.h> — no
 * engine.h, no repo headers. A cheat/dev tool: it opens an item-browser UI built entirely from the
 * host seams and grants any inventory item on click; the engine underneath is never modified.
 *
 * All state lives behind the SDK's MEMORY-ACCESS LAYER:
 *   - engine calls        -> api->engine->fn(...)   (all exposed in the engine-call table)
 *   - named-global reads   (G8/G16/G32 of a VA)  -> api->game_ram->u8/u16/u32(VA)
 *   - VA -> host pointer   (GADDR, for dos_open_file's filename)  -> api->game_ram->to_ptr(VA)
 *   - HOST-PTR derefs      (dbase100 records, DAS-pool descriptors, reloc descriptors, palette RGB,
 *                           shade table) — flat runtime pointers the engine RETURNS or stores in a
 *                           global — are followed DIRECTLY, exactly as the engine follows them: a
 *                           native in-process plugin dereferences an engine-provided host pointer
 *                           directly; it needs no header.
 *
 * CONTEXT SPLIT across three SDK callbacks:
 *   - on_compose_tick (TICK_ISR): COPIES the pre-rendered surface onto the published frame + cursor.
 *     No engine calls (game_ram reads only — a shade-table lookup for the world dim, tick-safe).
 *   - on_mouse_poll   (GAME):     every engine call lives here (list build, name fetch, DAS icon loads,
 *     give_item, surface render) + cursor + click handling.
 *   - on_scancode     (TICK_ISR): flag flips + search-text edit only (no engine calls).
 *   The surface is double-buffered so the tick never reads a half-drawn frame.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strcasecmp / strncasecmp (POSIX) — needs _GNU_SOURCE (see Makefile) */
#include <stdint.h>
#include <ctype.h>

#include "roth_sdk.h"

/* ---- canonical VAs (game_ram offsets; the frozen 1996 layout — just numbers, no g_names.h) ---- */
#define VA_g_dbase100_base             0x81e1cu
#define VA_g_player_movement_enabled   0x7674au
#define VA_g_dbase100_inventory_table  0x81e20u
#define VA_g_inventory_tab_context_map 0x7123cu
#define VA_g_das_cache_heap_handle     0x85c3cu
#define VA_g_dbase200_filename         0x81f06u
#define VA_g_reloc_base                0x7f56cu
#define VA_g_hires_line_doubling_flag  0x90cbdu
#define VA_g_palette_rgb_ptr           0x85488u
#define VA_g_world_shading_table_ptr   0x86d28u
#define VA_g_screen_pitch              0x85498u
#define VA_g_screen_height             0x854a0u
#define VA_g_frame_tick_counter        0x90bccu
#define VA_g_default_message_color     0x7675eu
#define VA_g_map_menu_marker_selected  0x7675du
#define VA_g_timed_message_color       0x82040u
#define VA_g_rect_fill_color           0x7f355u
#define VA_g_cursor_sprite_header      0x7e950u   /* software-cursor sprite: {u8 w, u8 h, pixels[w*h]} */

/* the host API table, shared across all callbacks (immutable). Set at the top of each callback. */
static const struct roth_api_v1 *g_api;
#define GR (g_api->game_ram)
#define E  (g_api->engine)
#define G8(off)  (GR->u8((uint32_t)(off)))
#define G16(off) (GR->u16((uint32_t)(off)))
#define G32(off) ((int32_t)GR->u32((uint32_t)(off)))

/* framebuffer max (was shared_fb.h ROTH_FB_MAX): 640x480 VESA hi-res */
#define IG_FB_MAX (640u * 480u)

/* ---- tunables (ALL layout math lives in ig_layout(); these are its named inputs) ---- */
#define IG_MAX_ITEMS   1024
#define IG_NAME_MAX    0x61      /* engine uses 0x61 for read_next_dialogue_line */
#define IG_SEARCH_MAX  32
#define IG_FLASH_TICKS 120       /* grant-feedback linger (~1.7s at 70Hz game tick) */
#define IG_NTABS       5         /* the 5 inventory tabs (context map at 0x7123c) */
#define IG_SCROLL_PREV 0x1au     /* '[' scancode: scroll UP one full screen */
#define IG_SCROLL_NEXT 0x1bu     /* ']' scancode: scroll DOWN one full screen */
#define IG_SC_UP       0x48u     /* up-arrow / numpad-8 make: scroll up one row (may follow an 0xe0 prefix) */
#define IG_SC_DOWN     0x50u     /* down-arrow / numpad-2 make: scroll down one row */
#define IG_SC_E0       0xe0u     /* extended-key prefix (arrow keys arrive as e0 48 / e0 50) */
#define IG_SC_BACKSPACE 0x0eu

/* Native ROTH scroll-arrow tile slots (draw_scroll_indicators). ACTIVE tile when scrolling that way is
 * possible, GREYED tile otherwise (the arrows are always shown). Same reloc pool + blitter the mod uses. */
#define IG_ARROW_UP_ON   0xa8u
#define IG_ARROW_UP_OFF  0xb0u
#define IG_ARROW_DN_ON   0xb8u
#define IG_ARROW_DN_OFF  0xc0u
#define IG_ARROW_DELAY   28      /* hold-to-repeat: initial delay (~0.4s at the 70Hz game tick) */
#define IG_ARROW_REPEAT  6       /* hold-to-repeat: interval (game ticks) */

/* The native scroll arrows blit OVER the save/load parchment; on our dark panel each otherwise shows a
 * foreign parchment rectangle. Fix: paint the reserved arrow strip with the SAME bg (reloc tile 0x328,
 * the 288x80 message-box bg), phase-locked to the arrow column so the baked parchment lines up. */
#define IG_DIALOG_BG_SLOT 0x328u /* fixed save/load box parchment bg */
#define IG_DIALOG_ARROW_X 0x116  /* arrow column x-offset into the bg tile */
#define IG_DIALOG_ARROW_Y 0x12   /* up-arrow y-offset into the bg tile */
#define IG_BG_BORDER_LUM  48     /* 6-bit RGB-sum below this = the tile's baked box border */

/* --- panel geometry: a centered percentage of the screen (ROTH_ITEM_GRABBER_PANEL=WxH) --- */
#define IG_PANEL_PCT_W   50
#define IG_PANEL_PCT_H   50
#define IG_COLS_TARGET   8
#define IG_CELL_MIN      16
#define IG_CELL_MAX      44

/* --- native ROTH 9-slice frame band widths (from show_message_box) --- */
#define IG_FRAME_L    0x22
#define IG_FRAME_T    0x0f
#define IG_FRAME_R    8
#define IG_FRAME_B    8
#define IG_FRAME_BG_DEF   1u
#define IG_FRAME_STEP 0xb        /* right-column tile pitch; native boxh = count*0xb + 0x17 */
#define IG_FRAME_BASE 0x17

/* Left ornate spine (ICONS.ALL slot 0xd0): a self-contained Gothic column, ~150 px of RLE rows. Its
 * descriptor height field (+6) is a STALE leftover; the true encoded row count is IG_SPINE_H. Blit
 * taller and it over-reads the reloc pool. We blit the column ONCE at its true height, then pixel-copy
 * it down the left band — mirrored (ping-pong) on alternate repeats. Index-0 -> the dark frame-bg. */
#define IG_SPINE_SLOT 0xd0u
#define IG_SPINE_W    40
#define IG_SPINE_H    150

/* --- colours (all GAME-SOURCED) --- */
#define IG_TEXT_COLOR_DEF 0x20u   /* tan/parchment default body text */
#define IG_GOLD_HI_DEF    0x70u   /* bright frame gold — accent/hover/active/page */
#define IG_GOLD_MID_DEF   0x75u   /* dominant frame gold-tan — thin rules/borders */
#define IG_GOLD_LO_DEF    0x9eu   /* dark frame gold-brown — fills / empty cells */

/* --- world dim behind the transparent interior (engine shade table; higher=darker) --- */
#define IG_DIM_ROW_DEF    0x1b
#define IG_DIM_OFF        (-1)

/* --- icon scale scratch --- */
#define IG_SCRATCH_W   128
#define IG_SCRATCH_H   128

/* ---- one catalog entry ---- */
typedef struct {
    uint32_t dbase_id;
    uint16_t entry;
    uint8_t  cat;
    uint32_t icon_res;
    uint32_t name_id;
    uint32_t icon_h;             /* cached DAS pool handle (0 = not loaded); runtime host-ptr */
    uint8_t  icon_bad;
    char     name[IG_NAME_MAX];
    char     lname[IG_NAME_MAX];
} ig_item_t;

/* ---- state ---- */
static int      ig_enabled;
static int      ig_force;                  /* ROTH_ITEM_GRABBER_OPEN=1: open outside gameplay */
static uint8_t  ig_key = 0x41;             /* toggle make scancode (default F7) */
static volatile int ig_open;

static ig_item_t ig_items[IG_MAX_ITEMS];
static int       ig_nitems;
static uint32_t  ig_built_base;            /* g_dbase100_base the catalog was built from (0 = none) */

static int       ig_view[IG_MAX_ITEMS];
static int       ig_nview;
static volatile int ig_view_dirty = 1;

static int       ig_tab = -1;
static char      ig_search[IG_SEARCH_MAX];
static int       ig_search_len;

static volatile int ig_scroll;
static volatile int ig_rows = 1;
static volatile int ig_max_scroll;
static int      ig_arrow_held;
static uint16_t ig_arrow_press_tick;
static uint16_t ig_arrow_last_tick;
static uint8_t  ig_arrow_w, ig_arrow_h;
static int      ig_hover = -1;

static volatile int ig_mx = 160, ig_my = 100;
static double  ig_fx = 160, ig_fy = 100;
static double  ig_sens = 0.5;
static uint32_t ig_buttons, ig_buttons_prev;

static char     ig_flash[128];   /* grant-feedback line: "<verb>: <name>" (fits IG_NAME_MAX under -Werror) */
static int      ig_flash_ticks;

static uint32_t ig_panel_pw = IG_PANEL_PCT_W;
static uint32_t ig_panel_ph = IG_PANEL_PCT_H;
static int      ig_dim_row  = IG_DIM_ROW_DEF;
static int      ig_dim_passes = 1;
static int      ig_frame_enable = 1;
static uint32_t ig_frame_bg = IG_FRAME_BG_DEF;
static uint32_t ig_strip_cap;
static int      ig_spine_mirror = 1;
static int      ig_text_color   = IG_TEXT_COLOR_DEF;
static int      ig_accent_color = -1;
static uint8_t  ig_gold_hi, ig_gold_mid, ig_gold_lo;
static uint8_t  ig_scratch[IG_SCRATCH_W * IG_SCRATCH_H];

#define IG_BG_W 320
#define IG_BG_H 200
static uint8_t  ig_bg_tex[IG_BG_W * IG_BG_H];
static uint32_t ig_bg_w, ig_bg_h;
static uint32_t ig_bg_cached_base;
static uint32_t ig_bg_top, ig_bg_bot;

static uint8_t  ig_spine_tex[IG_SPINE_W * IG_SPINE_H];
static uint32_t ig_spine_w, ig_spine_h;
static uint32_t ig_spine_base;

/* double-buffered overlay surface */
static uint8_t  ig_surf[2][IG_FB_MAX];
static volatile int ig_pub = -1;
static volatile uint32_t ig_surf_w, ig_surf_h;
static volatile uint32_t ig_cx0, ig_cy0, ig_cx1, ig_cy1;

static const char *ig_tab_label[1 + IG_NTABS] = { "All", "General", "Weapons", "Special", "Docs", "People" };
static const char *ig_tab_full[1 + IG_NTABS]  = { "All Items", "General Items", "Weapons",
                                                  "Special Items", "Documents", "People & Concepts" };

/* ---- key names -> set-1 make scancodes (keybinds.cfg vocabulary) ---- */
static const struct { const char *name; uint8_t sc; } ig_keys[] = {
    {"esc",0x01},
    {"1",0x02},{"2",0x03},{"3",0x04},{"4",0x05},{"5",0x06},{"6",0x07},{"7",0x08},{"8",0x09},{"9",0x0a},{"0",0x0b},
    {"minus",0x0c},{"equals",0x0d},{"backspace",0x0e},{"tab",0x0f},
    {"q",0x10},{"w",0x11},{"e",0x12},{"r",0x13},{"t",0x14},{"y",0x15},{"u",0x16},{"i",0x17},{"o",0x18},{"p",0x19},
    {"lbracket",0x1a},{"rbracket",0x1b},{"enter",0x1c},{"ctrl",0x1d},
    {"a",0x1e},{"s",0x1f},{"d",0x20},{"f",0x21},{"g",0x22},{"h",0x23},{"j",0x24},{"k",0x25},{"l",0x26},
    {"semicolon",0x27},{"quote",0x28},{"grave",0x29},{"lshift",0x2a},{"backslash",0x2b},
    {"z",0x2c},{"x",0x2d},{"c",0x2e},{"v",0x2f},{"b",0x30},{"n",0x31},{"m",0x32},
    {"comma",0x33},{"period",0x34},{"slash",0x35},{"rshift",0x36},
    {"alt",0x38},{"space",0x39},{"capslock",0x3a},
    {"f1",0x3b},{"f2",0x3c},{"f3",0x3d},{"f4",0x3e},{"f5",0x3f},
    {"f6",0x40},{"f7",0x41},{"f8",0x42},{"f9",0x43},{"f10",0x44},
};

static char ig_sc_to_char(uint8_t make)
{
    static const char row1[] = "1234567890";
    static const char rowq[] = "qwertyuiop";
    static const char rowa[] = "asdfghjkl";
    static const char rowz[] = "zxcvbnm";
    if (make >= 0x02 && make <= 0x0b) return row1[make - 0x02];
    if (make >= 0x10 && make <= 0x19) return rowq[make - 0x10];
    if (make >= 0x1e && make <= 0x26) return rowa[make - 0x1e];
    if (make >= 0x2c && make <= 0x32) return rowz[make - 0x2c];
    if (make == 0x39) return ' ';
    return 0;
}

static int ig_gameplay_live(void)
{
    if (ig_force)
        return 1;
    if (G32(VA_g_dbase100_base) == 0)
        return 0;
    return G8(VA_g_player_movement_enabled) == 1;
}

/* ============================ catalog (game thread) ============================ */

static void ig_read_name(uint32_t name_id, char *out)
{
    char tmp[IG_NAME_MAX];
    memset(tmp, 0, sizeof tmp);
    if (name_id != 0)
        E->read_next_dialogue_line((uint32_t)(uintptr_t)tmp, sizeof tmp, name_id, 0);
    int j = 0;
    for (int k = 0; k < IG_NAME_MAX - 1 && tmp[k]; k++) {
        unsigned char c = (unsigned char)tmp[k];
        if (c >= 0x20 && c < 0x7f) out[j++] = (char)c;
    }
    out[j] = 0;
    if (j == 0) { out[0] = '?'; out[1] = 0; }
}

/* Build the grantable-item catalog from the loaded DBASE100 inventory table. `base`/`table` are runtime
 * host pointers stored in game_ram globals (read via game_ram), then the records are followed directly. */
static void ig_build_list(void)
{
    uint32_t base  = (uint32_t)G32(VA_g_dbase100_base);
    uint32_t table = (uint32_t)G32(VA_g_dbase100_inventory_table);
    if (base == 0 || table == 0) { ig_nitems = 0; return; }
    int32_t count = *(int32_t *)(uintptr_t)(base + 0x10);
    if (count < 0) count = 0;

    if (getenv("ROTH_ITEM_GRABBER_DUMP")) {   /* diagnostic: every table entry + its fields */
        for (int32_t n = 1; n <= count; n++) {
            uint32_t off = *(uint32_t *)(uintptr_t)(table + (uint32_t)n * 4);
            if (off == 0) continue;
            uint32_t rec = base + off;
            int32_t  id  = (int16_t)*(uint16_t *)(uintptr_t)(rec + 2);
            uint32_t fl  = *(uint32_t *)(uintptr_t)(rec + 4);
            uint32_t icon = *(uint32_t *)(uintptr_t)(rec + 0xc);
            uint32_t name = *(uint32_t *)(uintptr_t)(rec + 0x10);
            uint8_t  b5  = *(uint8_t *)(uintptr_t)(rec + 5);
            char nm[IG_NAME_MAX]; memset(nm, 0, sizeof nm);
            if (name) E->read_next_dialogue_line((uint32_t)(uintptr_t)nm, sizeof nm, name, 0);
            for (int k = 0; k < IG_NAME_MAX; k++) { unsigned char c=(unsigned char)nm[k]; if(c&&(c<0x20||c>=0x7f)) nm[k]='.'; }
            fprintf(stderr, "[ig-dump] n=%d id=0x%x fl=0x%08x cat=%u b5=0x%02x pick80=%d icon=0x%x name=0x%x '%s'\n",
                    n, id, fl, (unsigned)(fl>>8)&0xf, b5, (fl&0x80)?1:0, icon, name, nm);
        }
    }

    ig_nitems = 0;
    for (int32_t n = 1; n <= count && ig_nitems < IG_MAX_ITEMS; n++) {
        uint32_t off = *(uint32_t *)(uintptr_t)(table + (uint32_t)n * 4);
        if (off == 0) continue;
        uint32_t rec = base + off;
        uint32_t icon = *(uint32_t *)(uintptr_t)(rec + 0xc);
        if (icon == 0) continue;                         /* no inventory art */
        uint32_t name = *(uint32_t *)(uintptr_t)(rec + 0x10);
        char nm[IG_NAME_MAX];
        ig_read_name(name, nm);
        if (!strcmp(nm, "Monster") || !strcmp(nm, "Bullet"))
            continue;                                    /* monster / projectile definition, not an item */

        ig_item_t *it = &ig_items[ig_nitems++];
        memset(it, 0, sizeof *it);
        it->dbase_id = (uint32_t)(uint16_t)*(uint16_t *)(uintptr_t)(rec + 2);
        it->entry    = (uint16_t)n;
        it->cat      = (uint8_t)(*(uint8_t *)(uintptr_t)(rec + 5) & 0xf);
        it->icon_res = icon;
        it->name_id  = name;
        snprintf(it->name, sizeof it->name, "%s", nm);
        for (int k = 0; it->name[k]; k++) it->lname[k] = (char)tolower((unsigned char)it->name[k]);
    }
    ig_built_base = base;
    ig_view_dirty = 1;
    fprintf(stderr, "[mods:item_grabber] catalog: %d grantable items (of %d records)\n",
            ig_nitems, (int)count);
}

static void ig_rebuild_view(void)
{
    uint8_t want_cat = 0xff;
    if (ig_tab >= 0 && ig_tab < IG_NTABS)
        want_cat = (uint8_t)G8(VA_g_inventory_tab_context_map + (uint32_t)ig_tab);
    ig_nview = 0;
    for (int i = 0; i < ig_nitems; i++) {
        ig_item_t *it = &ig_items[i];
        if (want_cat != 0xff && it->cat != want_cat) continue;
        if (ig_search_len > 0 && !strstr(it->lname, ig_search)) continue;
        ig_view[ig_nview++] = i;
    }
    ig_scroll = 0;
    if (getenv("ROTH_ITEM_GRABBER_DEBUG"))
        fprintf(stderr, "[mods:item_grabber] view: tab=%d search='%s' -> %d of %d "
                "(item0='%s')\n", ig_tab, ig_search, ig_nview, ig_nitems,
                ig_nitems ? ig_items[0].name : "");
}

/* icons: DAS load/free (game thread). the filename lives in game_ram -> to_ptr gives the host pointer
 * dos_open_file reads; the returned handle + descriptor are runtime host-ptrs. */
static uint32_t ig_load_icon(uint16_t entry)
{
    uint32_t fh = E->dos_open_file((uint32_t)(uintptr_t)GR->to_ptr(VA_g_dbase200_filename), 0);
    if (fh == 0) return 0;
    uint32_t h = E->load_item_icon_resource((uint32_t)entry, fh);
    E->dos_close_handle(fh);
    return h;
}
static void ig_free_icon(ig_item_t *it)
{
    if (it->icon_h) {
        E->pool_free_handle((uint32_t *)(uintptr_t)(uint32_t)G32(VA_g_das_cache_heap_handle),
                            (uint32_t *)(uintptr_t)it->icon_h);
        it->icon_h = 0;
    }
}
static void ig_free_all_icons(void)
{
    for (int i = 0; i < ig_nitems; i++)
        ig_free_icon(&ig_items[i]);
}

static void ig_sync_visible_icons(int start, int per)
{
    uint8_t onpage[IG_MAX_ITEMS] = {0};
    for (int k = 0; k < per; k++) {
        int v = start + k;
        if (v >= ig_nview) break;
        onpage[ig_view[v]] = 1;
    }
    for (int i = 0; i < ig_nitems; i++) {
        ig_item_t *it = &ig_items[i];
        if (onpage[i]) {
            if (it->icon_h == 0 && !it->icon_bad) {
                it->icon_h = ig_load_icon(it->entry);
                if (it->icon_h == 0 || *(uint32_t *)(uintptr_t)it->icon_h == 0) {
                    ig_free_icon(it);
                    it->icon_bad = 1;
                }
            }
        } else if (it->icon_h) {
            ig_free_icon(it);
        }
    }
}

/* ============================ layout ============================ */

typedef struct {
    uint32_t x0, y0, x1, y1;
    uint32_t cx0, cy0, cx1, cy1;
    uint32_t tabs_y0, tabs_y1;
    uint32_t srch_y0, srch_y1;
    uint32_t gx0, gy0;
    uint32_t gut_y0;
    uint32_t footy;
    uint32_t tabw;
    uint32_t tabx0;
    uint32_t cell;
    int cols, rows, per;
    int frame;
    uint32_t astrip_x, astrip_w;
    uint32_t aw, ah;
    uint32_t up_y, dn_y;
} ig_layout_t;

static void ig_layout(uint32_t w, uint32_t h, ig_layout_t *L)
{
    uint32_t pw = w * ig_panel_pw / 100;
    uint32_t ph = h * ig_panel_ph / 100;
    if (pw > w) pw = w;
    if (ph > h) ph = h;
    if (pw < 64 && w >= 64) pw = 64;
    if (ph < 48 && h >= 48) ph = 48;
    uint32_t maxw = (w > 48) ? w - 48 : w;
    uint32_t maxh = (h > 32) ? h - 32 : h;
    if (pw > maxw) pw = maxw;
    if (ph > maxh) ph = maxh;

    int hires = (G8(VA_g_hires_line_doubling_flag) != 0);
    L->frame = (ig_frame_enable && !hires && (uint32_t)G32(VA_g_reloc_base) != 0
                && pw > (uint32_t)(IG_FRAME_L + IG_FRAME_R + 32)
                && ph > (uint32_t)(IG_FRAME_T + IG_FRAME_B + 32));

    if (L->frame) {
        int32_t count = ((int32_t)ph - IG_FRAME_BASE) / IG_FRAME_STEP;
        if (count < 1) count = 1;
        uint32_t snapped = (uint32_t)(count * IG_FRAME_STEP + IG_FRAME_BASE);
        if (snapped >= 0x1f && snapped <= ph) ph = snapped;
    }

    L->x0 = (w - pw) / 2; L->x1 = L->x0 + pw;
    L->y0 = (h - ph) / 2; L->y1 = L->y0 + ph;

    if (L->frame) {
        L->cx0 = L->x0 + IG_FRAME_L; L->cy0 = L->y0 + IG_FRAME_T;
        L->cx1 = L->x1 - IG_FRAME_R; L->cy1 = L->y1 - IG_FRAME_B;
    } else {
        L->cx0 = L->x0 + 3; L->cy0 = L->y0 + 3;
        L->cx1 = L->x1 - 3; L->cy1 = L->y1 - 3;
    }

    uint32_t innerw = (L->cx1 > L->cx0) ? L->cx1 - L->cx0 : 0;
    uint32_t rowh = h / 22; if (rowh < 11) rowh = 11;
    uint32_t fh   = h / 20; if (fh < 12) fh = 12;
    L->tabs_y0 = L->cy0 + 2;
    L->tabs_y1 = L->tabs_y0 + rowh;
    L->srch_y0 = L->tabs_y1 + 2;
    L->srch_y1 = L->srch_y0 + rowh;
    L->footy   = (L->cy1 > L->cy0 + fh) ? L->cy1 - fh : L->cy0;
    L->gx0 = L->cx0 + 2;
    L->gy0 = L->srch_y1 + 3;
    L->gut_y0 = L->srch_y1;
    L->tabw = innerw / (1 + IG_NTABS);
    L->tabx0 = L->cx0 + (innerw - (L->tabw * (1 + IG_NTABS) - 2)) / 2;

    uint32_t aw = ig_arrow_w ? ig_arrow_w : 12;
    uint32_t ah = ig_arrow_h ? ig_arrow_h : 16;
    uint32_t astrip = aw + 4;
    uint32_t grid_x1 = L->cx1;
    if (L->cx1 > L->gx0 + astrip + IG_CELL_MIN + 2) {
        grid_x1 = L->cx1 - astrip;
        L->astrip_x = L->cx1 - astrip;
        L->astrip_w = astrip;
        L->aw = aw; L->ah = ah;
    } else {
        L->astrip_x = 0; L->astrip_w = 0; L->aw = aw; L->ah = ah;
    }

    uint32_t gw = (grid_x1 > L->gx0 + 2) ? (grid_x1 - 2 - L->gx0) : 0;
    uint32_t gh = (L->footy > L->gy0 + 2) ? (L->footy - 2 - L->gy0) : 0;
    uint32_t cell = (gw >= IG_COLS_TARGET) ? gw / IG_COLS_TARGET : IG_CELL_MIN;
    if (cell < IG_CELL_MIN) cell = IG_CELL_MIN;
    if (cell > IG_CELL_MAX) cell = IG_CELL_MAX;
    L->cell = cell;
    L->cols = (int)(gw / cell); if (L->cols < 1) L->cols = 1;
    L->rows = (int)(gh / cell); if (L->rows < 1) L->rows = 1;
    L->per  = L->cols * L->rows;

    L->up_y = L->gut_y0 + 3;
    L->dn_y = (L->footy > L->gy0 + 2 * ah + 9) ? (L->footy - ah - 2) : (L->gy0 + ah + 7);
}

static int ig_max_scroll_for(const ig_layout_t *L)
{
    if (L->cols < 1) return 0;
    int total_rows = (ig_nview + L->cols - 1) / L->cols;
    int m = total_rows - L->rows;
    return m > 0 ? m : 0;
}

static void ig_resolve_arrows(void)
{
    if (ig_arrow_w) return;
    if ((uint32_t)G32(VA_g_reloc_base) == 0) return;
    uint32_t iw = 0, ih = 0;
    uint32_t desc = E->resolve_reloc_ptr(IG_ARROW_UP_OFF);
    if (desc) { iw = *(uint16_t *)(uintptr_t)(desc + 4); ih = *(uint16_t *)(uintptr_t)(desc + 6); }
    ig_arrow_w = (iw && iw <= 64) ? (uint8_t)iw : 12;
    ig_arrow_h = (ih && ih <= 64) ? (uint8_t)ih : 16;
}

enum { IG_HIT_NONE = 0, IG_HIT_TAB, IG_HIT_GRID, IG_HIT_ARROW_UP, IG_HIT_ARROW_DN };
static int ig_hit(const ig_layout_t *L, uint32_t w, uint32_t h, int *idx)
{
    uint32_t mx = (uint32_t)ig_mx * w / 320;
    uint32_t my = (uint32_t)ig_my * h / 200;
    uint32_t tabrow_w = L->tabw * (1 + IG_NTABS);
    if (L->tabw && my >= L->tabs_y0 && my < L->tabs_y1 &&
        mx >= L->tabx0 && mx < L->tabx0 + tabrow_w) {
        int t = (int)((mx - L->tabx0) / L->tabw);
        if (t >= 0 && t <= IG_NTABS) { *idx = t; return IG_HIT_TAB; }
    }
    if (L->astrip_w && ig_max_scroll_for(L) > 0 &&
        mx >= L->astrip_x && mx < L->astrip_x + L->astrip_w) {
        if (my + 3 >= L->up_y && my < L->up_y + L->ah + 3) return IG_HIT_ARROW_UP;
        if (my + 3 >= L->dn_y && my < L->dn_y + L->ah + 3) return IG_HIT_ARROW_DN;
    }
    if (L->cell && mx >= L->gx0 && my >= L->gy0) {
        int cx = (int)((mx - L->gx0) / L->cell);
        int cy = (int)((my - L->gy0) / L->cell);
        if (cx < L->cols && cy < L->rows) {
            int v = (ig_scroll + cy) * L->cols + cx;
            if (v >= 0 && v < ig_nview) { *idx = v; return IG_HIT_GRID; }
        }
    }
    return IG_HIT_NONE;
}

/* ============================ surface render (game thread) ============================ */

static void ig_hline(uint8_t *S, uint32_t w, uint32_t x, uint32_t y, uint32_t len, uint8_t c)
{ for (uint32_t i = 0; i < len; i++) S[y * w + x + i] = c; }
static void ig_vline(uint8_t *S, uint32_t w, uint32_t x, uint32_t y, uint32_t len, uint8_t c)
{ for (uint32_t i = 0; i < len; i++) S[(y + i) * w + x] = c; }
static void ig_rect(uint8_t *S, uint32_t w, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint8_t c)
{
    if (x1 <= x0 || y1 <= y0) return;
    ig_hline(S, w, x0, y0, x1 - x0, c);
    ig_hline(S, w, x0, y1 - 1, x1 - x0, c);
    ig_vline(S, w, x0, y0, y1 - y0, c);
    ig_vline(S, w, x1 - 1, y0, y1 - y0, c);
}
static void ig_fillrect(uint8_t *S, uint32_t w, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint8_t c)
{
    if (x1 <= x0 || y1 <= y0) return;
    for (uint32_t y = y0; y < y1; y++) memset(S + (uintptr_t)y * w + x0, c, x1 - x0);
}

static void ig_text(uint8_t *S, uint32_t w, uint32_t x, uint32_t y, uint8_t color, const char *s)
{
    char buf[192];
    if (color == 0) color = IG_GOLD_HI_DEF;
    buf[0] = 0x01; buf[1] = (char)color;
    size_t n = strlen(s);
    if (n > sizeof buf - 3) n = sizeof buf - 3;
    memcpy(buf + 2, s, n);
    buf[2 + n] = 0;
    uint32_t flags = (G8(VA_g_hires_line_doubling_flag) != 0) ? 1u : 0u;
    E->draw_text_to_buffer((uint32_t)(uintptr_t)buf,
                           (uint32_t)(uintptr_t)(S + (uintptr_t)y * w + x),
                           w, flags);
}

static void ig_native_tile(uint8_t *S, uint32_t w, uint32_t h, int32_t x, int32_t y, uint32_t slot)
{
    if (x < 0 || y < 0 || (uint32_t)x >= w || (uint32_t)y >= h) return;
    E->blit_reloc_das_image((uint32_t)(uintptr_t)(S + (uintptr_t)(uint32_t)y * w + (uint32_t)x), slot, w);
}

static void ig_cache_spine(void)
{
    uint32_t base = (uint32_t)G32(VA_g_reloc_base);
    if (base == 0) { ig_spine_w = ig_spine_h = 0; return; }
    if (ig_spine_w != 0 && ig_spine_base == base) return;
    ig_spine_w = ig_spine_h = 0;
    uint32_t desc = E->resolve_reloc_ptr(IG_SPINE_SLOT);
    if (!desc) return;
    uint32_t sw = *(uint16_t *)(uintptr_t)(desc + 4);
    uint32_t sh = ig_strip_cap ? ig_strip_cap : (uint32_t)IG_SPINE_H;
    if (sh > IG_SPINE_H) sh = IG_SPINE_H;
    if (sw == 0 || sw > IG_SPINE_W || sh == 0) return;
    uint16_t *hp = (uint16_t *)(uintptr_t)(desc + 6);
    uint16_t saved = *hp;
    memset(ig_spine_tex, 0, (size_t)sw * sh);
    *hp = (uint16_t)sh;
    E->blit_reloc_das_image((uint32_t)(uintptr_t)ig_spine_tex, IG_SPINE_SLOT, sw);
    *hp = saved;
    ig_spine_w = sw; ig_spine_h = sh; ig_spine_base = base;
}

static int ig_draw_frame(uint8_t *S, uint32_t w, uint32_t h, const ig_layout_t *L)
{
    if ((uint32_t)G32(VA_g_reloc_base) == 0) return 0;
    int32_t bx = (int32_t)L->x0, by = (int32_t)L->y0;
    int32_t bw = (int32_t)(L->x1 - L->x0), bh = (int32_t)(L->y1 - L->y0);
    if (bw < 0x4a || bh < 0x1f) return 0;

    ig_cache_spine();
    if (ig_spine_w && ig_spine_h) {
        uint32_t sw = ig_spine_w, sh = ig_spine_h;
        uint32_t band_h = (bh > 3) ? (uint32_t)(bh - 3) : 0;
        uint32_t period = 2u * sh;
        for (uint32_t r = 0; r < band_h; r++) {
            int32_t yy = by + (int32_t)r;
            if (yy < 0 || (uint32_t)yy >= h) continue;
            uint32_t src;
            if (ig_spine_mirror) {
                uint32_t p = r % period;
                src = (p < sh) ? p : (period - 1u - p);
            } else {
                src = r % sh;
            }
            const uint8_t *srow = ig_spine_tex + (uintptr_t)src * sw;
            uint8_t *drow = S + (uintptr_t)(uint32_t)yy * w + (uint32_t)bx;
            for (uint32_t c = 0; c < sw; c++) {
                if ((uint32_t)bx + c >= w) break;
                uint8_t v = srow[c];
                drow[c] = v ? v : (uint8_t)ig_frame_bg;
            }
        }
    }
    ig_native_tile(S, w, h, bx, by + bh - 3, 0xd8);

    ig_native_tile(S, w, h, bx + 0x22, by, 0x2b8);
    int32_t rem = bw - 0x47, tx = bx + 0x3f;
    while (rem >= 0x33) { ig_native_tile(S, w, h, tx, by, 0x2a0); rem -= 0x33; tx += 0x33; }
    if (rem > 0) ig_native_tile(S, w, h, tx - 0x33 + rem, by, 0x2a0);
    ig_native_tile(S, w, h, bx + bw - 8, by, 0x318);

    int32_t rowy = by + 0xf;
    int32_t count = ((int32_t)bh - IG_FRAME_BASE) / IG_FRAME_STEP; if (count < 1) count = 1;
    for (int32_t i = 0; i < count; i++) { ig_native_tile(S, w, h, bx + bw - 8, rowy, 0x2a8); rowy += 0xb; }
    rowy += 1;
    ig_native_tile(S, w, h, bx + 0x21, rowy, 0x320);
    if (rowy >= 0 && (uint32_t)rowy < h) {
        uint32_t dst = (uint32_t)(uintptr_t)(S + (uintptr_t)(uint32_t)rowy * w + (uint32_t)(bx + 0x23));
        E->blit_reloc_das_image(dst, 0x2c8, (((uint32_t)(bw - 0x2a)) << 16) + w);
    }
    ig_native_tile(S, w, h, bx + bw - 8, rowy, 0x2b0);

    if (getenv("ROTH_ITEM_GRABBER_DEBUG")) {
        static int once = 0;
        if (!once) { once = 1;
            uint32_t s0 = E->resolve_reloc_ptr(0x2b8);
            fprintf(stderr, "[mods:item_grabber] frame: base=0x%x panel=%dx%d spine=%ux%u(mirror=%d) count=%d corner0x2b8=%ux%u\n",
                    (uint32_t)G32(VA_g_reloc_base), bw, bh, ig_spine_w, ig_spine_h, ig_spine_mirror, count,
                    s0 ? *(uint16_t *)(uintptr_t)(s0 + 4) : 0, s0 ? *(uint16_t *)(uintptr_t)(s0 + 6) : 0);
        }
    }
    return 1;
}

static void ig_frame_band_fill(uint8_t *S, uint32_t w, const ig_layout_t *L)
{
    for (uint32_t y = L->y0; y < L->y1; y++) {
        int in_y = (y >= L->cy0 && y < L->cy1);
        uint8_t *row = S + (uintptr_t)y * w;
        for (uint32_t x = L->x0; x < L->x1; x++) {
            if (in_y && x >= L->cx0 && x < L->cx1) { x = L->cx1 - 1; continue; }
            if (row[x] == 0) row[x] = (uint8_t)ig_frame_bg;
        }
    }
}

static void ig_cache_bg(void)
{
    uint32_t base = (uint32_t)G32(VA_g_reloc_base);
    if (base == 0) { ig_bg_w = ig_bg_h = 0; return; }
    if (ig_bg_w != 0 && ig_bg_cached_base == base) return;
    ig_bg_w = ig_bg_h = 0;
    uint32_t desc = E->resolve_reloc_ptr(IG_DIALOG_BG_SLOT);
    if (!desc) return;
    uint32_t bw = *(uint16_t *)(uintptr_t)(desc + 4);
    uint32_t bh = *(uint16_t *)(uintptr_t)(desc + 6);
    if (bw == 0 || bh == 0 || bw > IG_BG_W || bh > IG_BG_H) return;
    memset(ig_bg_tex, 0, (size_t)bw * bh);
    E->blit_reloc_das_image((uint32_t)(uintptr_t)ig_bg_tex, IG_DIALOG_BG_SLOT, bw);
    ig_bg_w = bw; ig_bg_h = bh; ig_bg_cached_base = base;

    ig_bg_top = 0; ig_bg_bot = bh;
    {
        const uint8_t *pal = (const uint8_t *)(uintptr_t)(uint32_t)G32(VA_g_palette_rgb_ptr);
        uint32_t col = (IG_DIALOG_ARROW_X < (int)bw) ? (uint32_t)IG_DIALOG_ARROW_X : bw - 1;
        int a = (IG_DIALOG_ARROW_Y < (int)bh) ? IG_DIALOG_ARROW_Y : (int)bh / 2;
        if (pal) {
            int top = a, bot = a;
            while (top > 0) {
                uint8_t i = ig_bg_tex[(uint32_t)(top - 1) * bw + col];
                if (pal[i*3] + pal[i*3+1] + pal[i*3+2] < IG_BG_BORDER_LUM) break;
                top--;
            }
            while (bot + 1 < (int)bh) {
                uint8_t i = ig_bg_tex[(uint32_t)(bot + 1) * bw + col];
                if (pal[i*3] + pal[i*3+1] + pal[i*3+2] < IG_BG_BORDER_LUM) break;
                bot++;
            }
            if (bot > top) { ig_bg_top = (uint32_t)top; ig_bg_bot = (uint32_t)(bot + 1); }
        }
        if (getenv("ROTH_ITEM_GRABBER_DEBUG"))
            fprintf(stderr, "[mods:item_grabber] bg band: %ux%u col=%u -> [%u,%u)\n",
                    bw, bh, col, ig_bg_top, ig_bg_bot);
    }
}

static uint8_t ig_strip_texel(uint32_t x, uint32_t y, uint32_t ax, uint32_t up_y)
{
    if (ig_bg_w == 0 || ig_bg_h == 0) return (uint8_t)ig_frame_bg;
    int32_t bc = IG_DIALOG_ARROW_X + (int32_t)x - (int32_t)ax;
    if (bc < 0) bc = 0;
    if (bc >= (int32_t)ig_bg_w) bc = (int32_t)ig_bg_w - 1;
    int32_t T = (int32_t)ig_bg_top, B = (int32_t)ig_bg_bot;
    if (B <= T || B > (int32_t)ig_bg_h) { T = 0; B = (int32_t)ig_bg_h; }
    int32_t H = B - T;
    int32_t period = 2 * H;
    int32_t q = (IG_DIALOG_ARROW_Y - T + (int32_t)y - (int32_t)up_y) % period;
    if (q < 0) q += period;
    int32_t local = (q < H) ? q : (period - 1 - q);
    int32_t br = T + local;
    uint8_t v = ig_bg_tex[(uint32_t)br * ig_bg_w + (uint32_t)bc];
    return v ? v : (uint8_t)ig_frame_bg;
}

static void ig_draw_one_arrow(uint8_t *S, uint32_t w, uint32_t h, uint32_t x, uint32_t y,
                              uint32_t aw, uint32_t ah, int up, int active, int strip_tex, uint32_t up_y)
{
    if (x + aw + 1 > w || y + ah + 1 > h) return;
    uint8_t darkfill = (uint8_t)ig_frame_bg;
    if ((uint32_t)G32(VA_g_reloc_base) != 0) {
        uint32_t slot = up ? (active ? IG_ARROW_UP_ON : IG_ARROW_UP_OFF)
                           : (active ? IG_ARROW_DN_ON : IG_ARROW_DN_OFF);
        ig_native_tile(S, w, h, (int32_t)x, (int32_t)y, slot);
        for (uint32_t yy = y; yy < y + ah; yy++) {
            uint8_t *r = S + (uintptr_t)yy * w;
            for (uint32_t xx = x; xx < x + aw; xx++)
                if (r[xx] == 0)
                    r[xx] = strip_tex ? ig_strip_texel(xx, yy, x, up_y)
                                      : darkfill;
        }
    } else {
        uint8_t g = active ? ig_gold_hi : ig_gold_lo;
        for (uint32_t yy = y; yy < y + ah; yy++)
            memset(S + (uintptr_t)yy * w + x, darkfill, aw);
        uint32_t cxm = x + aw / 2;
        for (uint32_t r = 0; r < ah; r++) {
            uint32_t half = up ? (r * (aw / 2)) / (ah ? ah : 1)
                               : ((ah - 1 - r) * (aw / 2)) / (ah ? ah : 1);
            uint32_t xa = (cxm > half) ? cxm - half : x;
            uint32_t xb = cxm + half + 1; if (xb > x + aw) xb = x + aw;
            for (uint32_t xx = xa; xx < xb; xx++) S[(uintptr_t)(y + r) * w + xx] = g;
        }
    }
}

static int ig_fill_arrow_strip(uint8_t *S, uint32_t w, uint32_t h, const ig_layout_t *L, int max_scroll)
{
    if (max_scroll <= 0 || L->astrip_w == 0) return 0;
    if ((uint32_t)G32(VA_g_reloc_base) == 0) return 0;
    if (G8(VA_g_hires_line_doubling_flag) != 0) return 0;
    ig_cache_bg();
    if (ig_bg_w == 0) return 0;
    uint32_t aw = L->aw;
    uint32_t ax = L->astrip_x + ((L->astrip_w > aw) ? (L->astrip_w - aw) / 2 : 0);
    uint32_t x0 = L->astrip_x, x1 = L->astrip_x + L->astrip_w;
    uint32_t y0 = L->gut_y0,   y1 = L->footy;
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    for (uint32_t y = y0; y < y1; y++) {
        uint8_t *row = S + (uintptr_t)y * w;
        for (uint32_t x = x0; x < x1; x++)
            row[x] = ig_strip_texel(x, y, ax, L->up_y);
    }
    if (getenv("ROTH_ITEM_GRABBER_DEBUG")) {
        static int once = 0;
        if (!once) { once = 1;
            fprintf(stderr, "[mods:item_grabber] strip textured: bg=0x%x %ux%u col=0x%x sample=0x%02x "
                    "astrip x=%u w=%u y=%u..%u\n", IG_DIALOG_BG_SLOT, ig_bg_w, ig_bg_h,
                    IG_DIALOG_ARROW_X, ig_strip_texel(ax, y0, ax, L->up_y),
                    L->astrip_x, L->astrip_w, y0, y1);
        }
    }
    return 1;
}

static void ig_draw_arrows(uint8_t *S, uint32_t w, uint32_t h, const ig_layout_t *L, int max_scroll, int strip_tex)
{
    if (max_scroll <= 0 || L->astrip_w == 0) return;
    uint32_t aw = L->aw, ah = L->ah;
    uint32_t ax = L->astrip_x + ((L->astrip_w > aw) ? (L->astrip_w - aw) / 2 : 0);
    ig_draw_one_arrow(S, w, h, ax, L->up_y, aw, ah, 1, ig_scroll > 0, strip_tex, L->up_y);
    ig_draw_one_arrow(S, w, h, ax, L->dn_y, aw, ah, 0, ig_scroll < max_scroll, strip_tex, L->up_y);
}

static void ig_dump_colors(void)
{
    if (!getenv("ROTH_ITEM_GRABBER_COLORDUMP")) return;
    static int once = 0; if (once) return; once = 1;
    const uint8_t *pal = (const uint8_t *)(uintptr_t)(uint32_t)G32(VA_g_palette_rgb_ptr);
    #define IG_PRGB(ix) do { unsigned _i=(unsigned)(ix)&0xff; \
        if (pal) fprintf(stderr, "  idx 0x%02x  rgb %3u,%3u,%3u  lum %3u\n", _i, \
            pal[_i*3]*255/63, pal[_i*3+1]*255/63, pal[_i*3+2]*255/63, \
            (pal[_i*3]*54+pal[_i*3+1]*183+pal[_i*3+2]*19)/64*255/63); \
        else fprintf(stderr, "  idx 0x%02x  (no palette)\n", _i); } while (0)
    fprintf(stderr, "[ig-colordump] game-sourced UI colours (index -> rgb, lum green-weighted):\n");
    fprintf(stderr, " message-colour table @0x7675e:\n");
    for (int k = 0; k <= 6; k++) { fprintf(stderr, " +%d:", k); IG_PRGB(G8(VA_g_default_message_color + (uint32_t)k)); }
    fprintf(stderr, " marker_selected 0x7675d:"); IG_PRGB(G8(VA_g_map_menu_marker_selected));
    fprintf(stderr, " timed_message_color 0x82040:"); IG_PRGB(G8(VA_g_timed_message_color));
    fprintf(stderr, " rect_fill_color 0x7f355:"); IG_PRGB(G8(VA_g_rect_fill_color));
    fprintf(stderr, " dialogue default literal 0x20:"); IG_PRGB(0x20);
    fprintf(stderr, " inventory highlight ramp @(0x7123c+0x19), 32 dwords low byte:\n");
    for (int k = 0; k < 32; k += 4) {
        uint32_t v = (uint32_t)G32((VA_g_inventory_tab_context_map + 0x19) + (uint32_t)k * 4);
        fprintf(stderr, " ramp[%2d]=0x%02x:", k, (unsigned)(v & 0xff)); IG_PRGB(v & 0xff);
    }
    if ((uint32_t)G32(VA_g_reloc_base) != 0) {
        uint32_t slots[2] = { 0x2a0, 0x2b8 };
        for (int t = 0; t < 2; t++) {
            uint32_t desc = E->resolve_reloc_ptr(slots[t]);
            if (!desc) continue;
            uint32_t iw = *(uint16_t *)(uintptr_t)(desc + 4);
            uint32_t ih = *(uint16_t *)(uintptr_t)(desc + 6);
            if (!iw || !ih || iw > IG_SCRATCH_W || ih > IG_SCRATCH_H) continue;
            memset(ig_scratch, 0, (size_t)iw * ih);
            E->blit_reloc_das_image((uint32_t)(uintptr_t)ig_scratch, slots[t], iw);
            uint32_t cnt[256]; memset(cnt, 0, sizeof cnt);
            for (uint32_t i = 0; i < iw * ih; i++) cnt[ig_scratch[i]]++;
            fprintf(stderr, " frame tile 0x%x (%ux%u) top indices by freq:\n", slots[t], iw, ih);
            for (int n = 0; n < 8; n++) {
                uint32_t bi = 0, bc = 0;
                for (uint32_t c = 1; c < 256; c++) if (cnt[c] > bc) { bc = cnt[c]; bi = c; }
                if (!bc) break;
                fprintf(stderr, "  cnt=%4u", bc); IG_PRGB(bi); cnt[bi] = 0;
            }
        }
    }
    {
        uint32_t sb = (uint32_t)G32(VA_g_world_shading_table_ptr);
        if (sb && pal) {
            fprintf(stderr, " shade table @0x86d28 base=0x%x, per-row avg output luminance (0..255):\n", sb);
            int darkest = 0; uint32_t darklum = 0xffffffff;
            for (int r = 0; r < 32; r++) {
                const uint8_t *row = (const uint8_t *)(uintptr_t)(sb + (uint32_t)r * 256);
                uint32_t sum = 0;
                for (int c = 0; c < 256; c++) { uint8_t o = row[c];
                    sum += (uint32_t)(pal[o*3]*54 + pal[o*3+1]*183 + pal[o*3+2]*19) / 256; }
                uint32_t avg = sum / 256;
                if (avg < darklum) { darklum = avg; darkest = r; }
                fprintf(stderr, "  row 0x%02x avg-lum %u\n", r, avg);
            }
            fprintf(stderr, " -> darkest row = 0x%02x (avg-lum %u)\n", darkest, darklum);
        } else {
            fprintf(stderr, " shade table not live (base=0x%x) -> dim fallback path\n", sb);
        }
    }
    #undef IG_PRGB
}

static void ig_sample_accent(void)
{
    if (ig_gold_hi) return;
    uint8_t hi = IG_GOLD_HI_DEF, mid = IG_GOLD_MID_DEF, lo = IG_GOLD_LO_DEF;
    if (ig_accent_color >= 0) {
        hi = mid = (uint8_t)ig_accent_color; lo = (uint8_t)ig_frame_bg;
    } else if ((uint32_t)G32(VA_g_reloc_base) != 0) {
        uint32_t desc = E->resolve_reloc_ptr(0x2a0);
        const uint8_t *pal = (const uint8_t *)(uintptr_t)(uint32_t)G32(VA_g_palette_rgb_ptr);
        if (desc && pal) {
            uint32_t iw = *(uint16_t *)(uintptr_t)(desc + 4);
            uint32_t ih = *(uint16_t *)(uintptr_t)(desc + 6);
            if (iw && ih && iw <= IG_SCRATCH_W && ih <= IG_SCRATCH_H) {
                memset(ig_scratch, 0, (size_t)iw * ih);
                E->blit_reloc_das_image((uint32_t)(uintptr_t)ig_scratch, 0x2a0, iw);
                uint32_t cnt[256]; memset(cnt, 0, sizeof cnt);
                for (uint32_t i = 0; i < iw * ih; i++) cnt[ig_scratch[i]]++;
                uint32_t hilum = 0, lolum = 0xffffffffu, mfreq = 0;
                uint8_t fhi = 0, flo = 0, fmid = 0;
                for (uint32_t c = 1; c < 256; c++) {
                    if (cnt[c] < 4) continue;
                    if (pal[c*3] < pal[c*3+2]) continue;
                    uint32_t lum = pal[c*3]*54 + pal[c*3+1]*183 + pal[c*3+2]*19;
                    if (lum > hilum)  { hilum = lum;  fhi  = (uint8_t)c; }
                    if (lum < lolum)  { lolum = lum;  flo  = (uint8_t)c; }
                    if (cnt[c] > mfreq){ mfreq = cnt[c]; fmid = (uint8_t)c; }
                }
                if (fhi)  hi  = fhi;
                if (fmid) mid = fmid;
                if (flo)  lo  = flo;
            }
        }
    }
    ig_gold_hi = hi; ig_gold_mid = mid; ig_gold_lo = lo;
    if (getenv("ROTH_ITEM_GRABBER_DEBUG"))
        fprintf(stderr, "[mods:item_grabber] accent: gold hi=0x%02x mid=0x%02x lo=0x%02x, text=0x%02x\n",
                ig_gold_hi, ig_gold_mid, ig_gold_lo, (unsigned)ig_text_color);
}

static void ig_render(uint32_t w, uint32_t h)
{
    if ((uint64_t)w * h > IG_FB_MAX || w < 64 || h < 64) return;
    ig_dump_colors();
    ig_sample_accent();
    ig_layout_t L; ig_layout(w, h, &L);

    int per  = L.per;
    int cols = L.cols, rows = L.rows;
    int max_scroll = ig_max_scroll_for(&L);
    if (ig_scroll > max_scroll) ig_scroll = max_scroll;
    if (ig_scroll < 0) ig_scroll = 0;
    ig_rows = rows; ig_max_scroll = max_scroll;
    int start = ig_scroll * cols;

    if (getenv("ROTH_ITEM_GRABBER_DEBUG")) {
        static int last = -0x7fffffff;
        if (ig_scroll != last) { last = ig_scroll;
            fprintf(stderr, "[mods:item_grabber] scroll=%d rows=%d cols=%d max=%d view=%d\n",
                    ig_scroll, rows, cols, max_scroll, ig_nview);
        }
        static int aonce = 0;
        if (!aonce && L.astrip_w && max_scroll > 0) { aonce = 1;
            uint32_t acx = (L.astrip_x + L.astrip_w / 2) * 320 / w;
            fprintf(stderr, "[mods:item_grabber] arrowdbg up_cx=%u up_cy=%u dn_cx=%u dn_cy=%u aw=%u ah=%u\n",
                    acx, (L.up_y + L.ah / 2) * 200 / h, acx, (L.dn_y + L.ah / 2) * 200 / h,
                    (unsigned)L.aw, (unsigned)L.ah);
        }
    }

    ig_sync_visible_icons(start, per);

    int back = (ig_pub ^ 1) & 1;
    uint8_t *S = ig_surf[back];
    uint8_t txt  = (uint8_t)ig_text_color;
    uint8_t gold = ig_gold_hi;
    uint8_t rule = ig_gold_mid;
    uint8_t dark = ig_gold_lo;

    for (uint32_t y = L.y0; y < L.y1; y++)
        memset(S + (uintptr_t)y * w + L.x0, 0, L.x1 - L.x0);

    if (L.frame && ig_draw_frame(S, w, h, &L))
        ig_frame_band_fill(S, w, &L);
    else
        ig_rect(S, w, L.x0, L.y0, L.x1, L.y1, rule);

    for (int t = 0; t <= IG_NTABS; t++) {
        uint32_t bx = L.tabx0 + (uint32_t)t * L.tabw;
        int active = (t == 0) ? (ig_tab < 0) : (ig_tab == t - 1);
        if (active) {
            ig_fillrect(S, w, bx + 1, L.tabs_y0 + 1, bx + L.tabw - 3, L.tabs_y1 - 1, dark);
            ig_rect(S, w, bx, L.tabs_y0, bx + L.tabw - 2, L.tabs_y1, gold);
        } else {
            ig_rect(S, w, bx, L.tabs_y0, bx + L.tabw - 2, L.tabs_y1, rule);
        }
        ig_text(S, w, bx + 4, L.tabs_y0 + 2, active ? gold : txt, ig_tab_label[t]);
    }

    ig_rect(S, w, L.cx0, L.srch_y0, L.cx1, L.srch_y1, rule);
    {
        char sb[80];
        snprintf(sb, sizeof sb, "Search: %s_", ig_search);
        ig_text(S, w, L.cx0 + 4, L.srch_y0 + 2, txt, sb);
    }

    for (int k = 0; k < per; k++) {
        int vv = start + k;
        if (vv >= ig_nview) break;
        ig_item_t *it = &ig_items[ig_view[vv]];
        uint32_t cx = L.gx0 + (uint32_t)(k % L.cols) * L.cell;
        uint32_t cy = L.gy0 + (uint32_t)(k / L.cols) * L.cell;
        uint32_t desc = it->icon_h ? *(uint32_t *)(uintptr_t)it->icon_h : 0;
        int drew = 0;
        if (desc) {
            uint32_t iw = *(uint16_t *)(uintptr_t)(desc + 4);
            uint32_t ih = *(uint16_t *)(uintptr_t)(desc + 6);
            uint32_t sh = (ih + 1) / 2;
            if (iw && sh && iw <= IG_SCRATCH_W && sh <= IG_SCRATCH_H) {
                memset(ig_scratch, 0, (size_t)iw * sh);
                E->blit_item_icon(desc, (uint32_t)(uintptr_t)ig_scratch, iw, 2);
                uint32_t avail = (L.cell > 4) ? L.cell - 4 : L.cell;
                uint32_t dw, dh;
                if (iw >= sh) { dw = avail; dh = sh * avail / iw; }
                else          { dh = avail; dw = iw * avail / sh; }
                if (dw == 0) dw = 1;
                if (dh == 0) dh = 1;
                uint32_t ox = cx + (L.cell - dw) / 2, oy = cy + (L.cell - dh) / 2;
                for (uint32_t dy = 0; dy < dh; dy++) {
                    uint32_t syi = dy * sh / dh;
                    const uint8_t *srow = ig_scratch + (uintptr_t)syi * iw;
                    uint8_t *drow = S + (uintptr_t)(oy + dy) * w + ox;
                    for (uint32_t dx = 0; dx < dw; dx++) {
                        uint8_t pv = srow[dx * iw / dw];
                        if (pv) drow[dx] = pv;
                    }
                }
                drew = 1;
            }
        }
        if (!drew)
            ig_rect(S, w, cx + 4, cy + 4, cx + L.cell - 4, cy + L.cell - 4, dark);
        if (vv == ig_hover)
            ig_rect(S, w, cx, cy, cx + L.cell, cy + L.cell, gold);
    }

    int strip_tex = ig_fill_arrow_strip(S, w, h, &L, max_scroll);
    ig_draw_arrows(S, w, h, &L, max_scroll, strip_tex);
    if (max_scroll > 0 && L.astrip_w) {
        uint32_t sx0 = L.astrip_x, sx1 = L.astrip_x + L.astrip_w;
        if (sx1 > w) sx1 = w;
        if (sx1 > sx0 && L.gut_y0 < h)
            ig_hline(S, w, sx0, L.gut_y0, sx1 - sx0, rule);
        if (L.footy > L.gut_y0 && L.footy <= h)
            ig_vline(S, w, sx0, L.gut_y0, L.footy - L.gut_y0, rule);
    }

    ig_hline(S, w, L.cx0, L.footy, (L.cx1 > L.cx0) ? L.cx1 - L.cx0 : 0, rule);
    {
        char pg[48];
        if (ig_nview <= 0)
            snprintf(pg, sizeof pg, "(0)");
        else if (max_scroll > 0) {
            int first = start + 1, last = start + per;
            if (last > ig_nview) last = ig_nview;
            snprintf(pg, sizeof pg, "%d-%d/%d", first, last, ig_nview);
        } else
            snprintf(pg, sizeof pg, "(%d)", ig_nview);
        uint32_t pgw = (uint32_t)strlen(pg) * 8 + 6;
        uint32_t px = (L.cx1 > L.cx0 + pgw) ? L.cx1 - pgw : L.cx0 + 4;
        ig_text(S, w, px, L.footy + 3, gold, pg);

        const char *msg;
        if (ig_flash_ticks > 0)
            msg = ig_flash;
        else if (ig_hover >= 0 && ig_hover < ig_nview)
            msg = ig_items[ig_view[ig_hover]].name;
        else
            msg = ig_tab_full[(ig_tab < 0) ? 0 : ig_tab + 1];
        ig_text(S, w, L.cx0 + 4, L.footy + 3, txt, msg);
    }

    ig_cx0 = L.x0; ig_cy0 = L.y0; ig_cx1 = L.x1; ig_cy1 = L.y1;
    ig_surf_w = w; ig_surf_h = h;
    __asm__ __volatile__("" ::: "memory");
    ig_pub = back;
}

/* ============================ game-thread pump ============================ */

static void ig_close(void)
{
    ig_open = 0;
    ig_free_all_icons();
    ig_pub = -1;
    ig_hover = -1;
    ig_flash_ticks = 0;
    ig_arrow_held = 0;
}

static void ig_game_tick(void)
{
    if (!ig_open)
        return;
    if (!ig_gameplay_live()) { ig_close(); return; }

    if (ig_built_base != (uint32_t)G32(VA_g_dbase100_base)) {
        ig_free_all_icons();
        ig_build_list();
    }
    if (ig_view_dirty) { ig_rebuild_view(); ig_view_dirty = 0; }

    ig_resolve_arrows();

    if (getenv("ROTH_ITEM_GRABBER_DEBUG")) {
        static uint16_t dbg_base_tick; static int dbg_calls; static int dbg_inited;
        uint16_t nt = (uint16_t)G16(VA_g_frame_tick_counter);
        if (!dbg_inited) { dbg_inited = 1; dbg_base_tick = nt; }
        dbg_calls++;
        int16_t span = (int16_t)(nt - dbg_base_tick);
        if (span >= 35) {
            fprintf(stderr, "[ig-rate] %d ig_game_tick calls over %d engine ticks (%.2f calls/tick)\n",
                    dbg_calls, (int)span, (double)dbg_calls / (span ? span : 1));
            dbg_calls = 0; dbg_base_tick = nt;
        }
    }

    uint32_t w = (uint32_t)(uint16_t)G16(VA_g_screen_pitch);
    uint32_t h = (uint32_t)(uint16_t)G16(VA_g_screen_height);
    if ((uint64_t)w * h > IG_FB_MAX || w < 64 || h < 64) return;

    ig_layout_t L; ig_layout(w, h, &L);
    int idx = -1, kind = ig_hit(&L, w, h, &idx);
    ig_hover = (kind == IG_HIT_GRID) ? idx : -1;
    int max_scroll = ig_max_scroll_for(&L);

    uint16_t now_tick = (uint16_t)G16(VA_g_frame_tick_counter);
    int pressed = (ig_buttons & 1) && !(ig_buttons_prev & 1);
    if (kind == IG_HIT_ARROW_UP || kind == IG_HIT_ARROW_DN) {
        int dir = (kind == IG_HIT_ARROW_UP) ? -1 : 1;
        int fired = 0;
        if (pressed) {
            ig_scroll += dir;
            ig_arrow_press_tick = now_tick;
            ig_arrow_last_tick  = now_tick;
            ig_arrow_held = 1;
        } else if ((ig_buttons & 1) && ig_arrow_held) {
            int16_t since_press = (int16_t)(now_tick - ig_arrow_press_tick);
            int16_t since_last  = (int16_t)(now_tick - ig_arrow_last_tick);
            if (since_press >= IG_ARROW_DELAY && since_last >= IG_ARROW_REPEAT) {
                ig_scroll += dir;
                ig_arrow_last_tick = now_tick;
                fired = 1;
            }
        } else if (!(ig_buttons & 1)) {
            ig_arrow_held = 0;
        }
        if (ig_scroll < 0) ig_scroll = 0;
        if (ig_scroll > max_scroll) ig_scroll = max_scroll;
        if ((pressed || fired) && getenv("ROTH_ITEM_GRABBER_DEBUG"))
            fprintf(stderr, "[ig-arrow] %s dir=%+d scroll=%d tick=%u since_press=%d\n",
                    pressed ? "PRESS " : "REPEAT", dir, ig_scroll, now_tick,
                    (int)(int16_t)(now_tick - ig_arrow_press_tick));
    } else {
        ig_arrow_held = 0;
        if (pressed) {
            if (kind == IG_HIT_TAB) {
                int newtab = (idx == 0) ? -1 : idx - 1;
                if (newtab != ig_tab) { ig_tab = newtab; ig_view_dirty = 1; }
            } else if (kind == IG_HIT_GRID) {
                ig_item_t *it = &ig_items[ig_view[idx]];
                uint32_t r = E->give_item((uint32_t)it->entry, 0);
                snprintf(ig_flash, sizeof ig_flash, "%s: %s", r ? "Granted" : "Inventory full", it->name);
                ig_flash_ticks = IG_FLASH_TICKS;
                fprintf(stderr, "[mods:item_grabber] grant id=0x%x entry=%u -> %s (%s)\n",
                        it->dbase_id, it->entry, it->name, r ? "ok" : "full");
            }
        }
    }
    ig_buttons_prev = ig_buttons;
    if (ig_flash_ticks > 0) ig_flash_ticks--;

    ig_render(w, h);
}

/* ============================ seams (SDK callbacks) ============================ */

static void ig_cursor_move(int dx, int dy)
{
    ig_fx += dx * ig_sens;
    ig_fy += dy * ig_sens;
    if (ig_fx < 0) ig_fx = 0; else if (ig_fx > 319) ig_fx = 319;
    if (ig_fy < 0) ig_fy = 0; else if (ig_fy > 199) ig_fy = 199;
    ig_mx = (int)ig_fx;
    ig_my = (int)ig_fy;
}

/* on_mouse_poll(GAME): the int33 seam. Every engine call lives here. Suppress buttons/motion from the
 * game while the overlay is open (clear the low 16 bits to hide the mouse from the game). */
static void on_mouse_poll(const struct roth_api_v1 *api, struct roth_mouse_poll_v1 *mp)
{
    g_api = api;
    if (!ig_open)
        return;
    if (mp->ax == 0x0003) {
        ig_buttons = mp->ebx & 3;
        mp->ebx &= ~0xffffu;
        ig_game_tick();
    } else if (mp->ax == 0x000b) {
        int dx = (int)(int16_t)(mp->ecx & 0xffff);
        int dy = (int)(int16_t)(mp->edx & 0xffff);
        ig_cursor_move(dx, dy);
        mp->ecx &= ~0xffffu;
        mp->edx &= ~0xffffu;
        ig_game_tick();
    }
}

/* on_scancode(TICK_ISR): F7 toggle + search text + scroll keys. No engine calls (flag flips + game_ram
 * reads via ig_gameplay_live only). Returns the possibly-swallowed scancode. */
static uint8_t on_scancode(const struct roth_api_v1 *api, uint8_t sc)
{
    g_api = api;
    if (!ig_enabled)
        return sc;
    if (ig_open && sc == IG_SC_E0)
        return 0;                              /* swallow the extended prefix while open */
    if (sc & 0x80)
        return sc;                             /* breaks ALWAYS pass (sticky-key rule) */
    uint8_t make = sc & 0x7f;
    if (!ig_open) {
        if (make == ig_key && ig_gameplay_live()) {
            ig_fx = 160; ig_fy = 100; ig_mx = 160; ig_my = 100;
            ig_open = 1;
            return 0;
        }
        return sc;
    }
    if (make == ig_key || make == 0x01) { ig_open = 0; ig_arrow_held = 0; return 0; }   /* toggle / Esc: close */
    if (make == IG_SCROLL_PREV || make == IG_SC_UP) {
        int step = (make == IG_SCROLL_PREV && ig_rows > 0) ? ig_rows : 1;
        ig_scroll -= step;
        if (ig_scroll < 0) ig_scroll = 0;
        return 0;
    }
    if (make == IG_SCROLL_NEXT || make == IG_SC_DOWN) {
        int step = (make == IG_SCROLL_NEXT && ig_rows > 0) ? ig_rows : 1;
        ig_scroll += step;
        if (ig_scroll > ig_max_scroll) ig_scroll = ig_max_scroll;
        return 0;
    }
    if (make == IG_SC_BACKSPACE) {
        if (ig_search_len > 0) { ig_search[--ig_search_len] = 0; ig_view_dirty = 1; }
        return 0;
    }
    char c = ig_sc_to_char(make);
    if (c && ig_search_len < IG_SEARCH_MAX - 1) {
        ig_search[ig_search_len++] = c;
        ig_search[ig_search_len] = 0;
        ig_view_dirty = 1;
    }
    return 0;                                  /* swallow everything while open */
}

/* compose seam (on_compose_tick; TICK_ISR — pixel copy + cursor). game_ram reads (shade table) only. */
static void ig_draw_cursor(uint8_t *px, uint32_t w, uint32_t h)
{
    uint32_t cx = (uint32_t)ig_mx * w / 320;
    uint32_t cy = (uint32_t)ig_my * h / 200;
    uint8_t cur[2 + 32 * 32];                     /* the cursor sprite header + pixels, from game_ram */
    if (GR->read_block(VA_g_cursor_sprite_header, cur, sizeof cur) != 0)
        cur[0] = cur[1] = 0;
    uint8_t cw = cur[0], ch = cur[1];
    int ys = (w == 320 && h >= 400) ? 2 : 1;
    if (cw == 0 || ch == 0 || cw > 32 || ch > 32) {
        uint8_t cc = ig_gold_hi ? ig_gold_hi : (uint8_t)IG_GOLD_HI_DEF;
        for (int d = -3; d <= 3; d++) {
            uint32_t X = cx + (uint32_t)d, Y = cy + (uint32_t)d;
            if (X < w) px[cy * w + X] = cc;
            if (Y < h) px[Y * w + cx] = cc;
        }
        return;
    }
    for (uint32_t sy = 0; sy < ch; sy++)
        for (int r = 0; r < ys; r++) {
            uint32_t Y = cy + sy * (uint32_t)ys + (uint32_t)r;
            if (Y >= h) continue;
            for (uint32_t sx = 0; sx < cw; sx++) {
                uint8_t v = cur[2 + sy * cw + sx];
                if (v == 0) continue;
                uint32_t X = cx + sx;
                if (X < w) px[Y * w + X] = v;
            }
        }
}

static void on_compose_tick(const struct roth_api_v1 *api, uint8_t *px, uint32_t w, uint32_t h)
{
    g_api = api;
    if (!ig_enabled || !ig_open || !px || w < 64 || h < 64)
        return;
    int idx = ig_pub;
    if (idx >= 0 && ig_surf_w == w && ig_surf_h == h && (uint64_t)w * h <= IG_FB_MAX) {
        uint32_t x0 = ig_cx0, y0 = ig_cy0, x1 = ig_cx1, y1 = ig_cy1;
        if (x1 > w) x1 = w;
        if (y1 > h) y1 = h;
        const uint8_t *dim = NULL;
        int fill_dark = 0;
        if (ig_dim_row >= 0) {
            uint32_t sb = (uint32_t)G32(VA_g_world_shading_table_ptr);
            if (sb) dim = (const uint8_t *)(uintptr_t)(sb + ((uint32_t)ig_dim_row << 8));
            else    fill_dark = 1;
        }
        uint8_t darkfill = (uint8_t)ig_frame_bg;
        int passes = ig_dim_passes;
        const uint8_t *surf = ig_surf[idx];
        for (uint32_t y = y0; y < y1; y++) {
            const uint8_t *s = surf + (uintptr_t)y * w + x0;
            uint8_t *d = px + (uintptr_t)y * w + x0;
            if (dim) {
                for (uint32_t x = x0; x < x1; x++, s++, d++) {
                    uint8_t v = *s;
                    if (v) { *d = v; continue; }
                    uint8_t p = *d;
                    for (int k = 0; k < passes; k++) p = dim[p];
                    *d = p;
                }
            } else if (fill_dark) {
                for (uint32_t x = x0; x < x1; x++, s++, d++) {
                    uint8_t v = *s;
                    *d = v ? v : darkfill;
                }
            } else {
                for (uint32_t x = x0; x < x1; x++, s++, d++)
                    if (*s) *d = *s;
            }
        }
    }
    ig_draw_cursor(px, w, h);
}

/* ============================ boot init (on_load) ============================ */

static char *ig_trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static void on_load(const struct roth_api_v1 *api)
{
    g_api = api;
    char path[2600];
    snprintf(path, sizeof path, "%s/item_grabber.cfg", api->plugin_dir());

    int enabled = 0;
    FILE *f = fopen(path, "r");
    if (f) {
        int lineno = 0;
        char line[256];
        while (fgets(line, sizeof line, f)) {
            lineno++;
            char *hsh = strchr(line, '#'); if (hsh) *hsh = 0;
            char *eq = strchr(line, '=');
            if (!eq) { if (*ig_trim(line)) fprintf(stderr, "[mods:item_grabber] %s:%d: no '=' — skipped\n", path, lineno); continue; }
            *eq = 0;
            char *k = ig_trim(line), *v = ig_trim(eq + 1);
            if (!*k) continue;
            if (!strcasecmp(k, "enabled")) {
                enabled = atoi(v) != 0;
            } else if (!strcasecmp(k, "key")) {
                int sc = -1;
                for (unsigned i = 0; i < sizeof ig_keys / sizeof *ig_keys; i++)
                    if (!strcasecmp(ig_keys[i].name, v)) { sc = ig_keys[i].sc; break; }
                if (sc < 0 && !strncasecmp(v, "0x", 2)) sc = (int)strtoul(v, NULL, 16) & 0xff;
                if (sc > 0 && sc <= 0x7f) ig_key = (uint8_t)sc;
                else fprintf(stderr, "[mods:item_grabber] %s:%d: unknown key '%s' — keeping default\n", path, lineno, v);
            } else {
                fprintf(stderr, "[mods:item_grabber] %s:%d: unknown setting '%s' — skipped\n", path, lineno, k);
            }
        }
        fclose(f);
    }
    const char *env = getenv("ROTH_ITEM_GRABBER");
    if (env) enabled = env[0] != '0';
    if (!enabled) {
        fprintf(stderr, "[mods:item_grabber] disabled (no %s or enabled=0)\n", path);
        return;
    }

    ig_enabled = 1;
    ig_force = getenv("ROTH_ITEM_GRABBER_OPEN") ? 1 : 0;
    { const char *s = getenv("ROTH_ITEM_GRABBER_SENS"); if (s) { double v = atof(s); if (v > 0) ig_sens = v; } }

    { const char *s = getenv("ROTH_ITEM_GRABBER_PANEL");
      if (s) {
          int pw = atoi(s), ph = pw;
          const char *xp = strpbrk(s, "xX*");
          if (xp) ph = atoi(xp + 1);
          if (pw >= 10 && pw <= 100) ig_panel_pw = (uint32_t)pw;
          if (ph >= 10 && ph <= 100) ig_panel_ph = (uint32_t)ph;
      } }
    { const char *s = getenv("ROTH_ITEM_GRABBER_DIM");
      if (s) {
          if (!strcasecmp(s, "off") || !strcasecmp(s, "none")) ig_dim_row = IG_DIM_OFF;
          else {
              int r = atoi(s);
              ig_dim_row = (r < 0) ? IG_DIM_OFF : (r > 31 ? 31 : r);
              const char *xp = strpbrk(s, "xX*");
              int p = xp ? atoi(xp + 1) : 1;
              if (p < 1) p = 1;
              if (p > 4) p = 4;
              ig_dim_passes = p;
          }
      } }
    { const char *s = getenv("ROTH_ITEM_GRABBER_TEXT_COLOR");
      if (s) { long v = strtol(s, NULL, 0); if (v >= 0 && v <= 255) ig_text_color = (int)v; } }
    { const char *s = getenv("ROTH_ITEM_GRABBER_ACCENT_COLOR");
      if (s) { long v = strtol(s, NULL, 0); if (v >= 0 && v <= 255) ig_accent_color = (int)v; } }
    { const char *s = getenv("ROTH_ITEM_GRABBER_FRAME");   if (s) ig_frame_enable = (s[0] != '0'); }
    { const char *s = getenv("ROTH_ITEM_GRABBER_FRAME_BG");
      if (s) { long v = strtol(s, NULL, 0); if (v >= 0 && v <= 255) ig_frame_bg = (uint32_t)v; } }
    { const char *s = getenv("ROTH_ITEM_GRABBER_FRAME_STRIP_H");
      if (s) { long v = strtol(s, NULL, 0); if (v > 0) ig_strip_cap = (uint32_t)v; } }
    { const char *s = getenv("ROTH_ITEM_GRABBER_SPINE_MIRROR"); if (s) ig_spine_mirror = (s[0] != '0'); }

    fprintf(stderr, "[mods:item_grabber] enabled (toggle key sc 0x%02x%s; panel %u%%x%u%% "
            "dim=%dx%d text=0x%02x accent=%s frame=%d)\n",
            ig_key, ig_force ? ", dev force-open" : "",
            ig_panel_pw, ig_panel_ph, ig_dim_row, ig_dim_passes, (unsigned)ig_text_color,
            ig_accent_color >= 0 ? "override" : "frame-sampled", ig_frame_enable);
}

static const struct roth_plugin_info_v1 ITEM_GRABBER = {
    .abi_major     = ROTH_ABI_MAJOR,
    .abi_minor     = ROTH_ABI_MINOR,
    .struct_size   = sizeof(struct roth_plugin_info_v1),
    .id            = "ire.roth.item_grabber",
    .name          = "Item Grabber (F7 give-any-item overlay)",
    .version       = "1.0.0",
    .sdk_req_major = ROTH_SDK_MAJOR,
    .sdk_req_minor = ROTH_SDK_MINOR,
    .api_use       = ROTH_API_USE_GAME_RAM | ROTH_API_USE_ENGINE | ROTH_API_USE_COMPOSE | ROTH_API_USE_INPUT,
    .on_load           = on_load,
    .on_game_ram_ready = NULL,
    .on_frame_game     = NULL,
    .on_compose_tick   = on_compose_tick,
    .on_audio          = NULL,
    .on_unload         = NULL,
    .on_register_overrides = NULL,
    .on_scancode       = on_scancode,
    .on_mouse_poll     = on_mouse_poll,
};

ROTH_PLUGIN_EXPORT const struct roth_plugin_info_v1 *roth_plugin_query_v1(void)
{
    return &ITEM_GRABBER;
}
