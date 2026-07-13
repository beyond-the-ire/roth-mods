/* keybinds.c — an action-key rebinding plugin. Compiles against ONLY <roth_sdk.h> — no engine.h,
 * no repo headers.
 *
 *   make            # -> plugin.so  (gcc -m32 -shared -fPIC -std=c11 -Wall -Wextra -Werror)
 *   cp -r .  <game-dir>/mods/keybinds/      # plugin.so + keybinds.cfg live together
 *
 * Rebinds BOTH engine keybind tables (identical {u8 scancode, u32 handler} record shape) from a
 * config file, then rewrites menu-nav keys through the input seam:
 *   g_keymap_table         canon 0x7093d, 37 records — PRESS actions
 *   g_held_key_move_table  canon 0x707f1, 12 records — HELD movement/look/jump/crouch
 * The handler dwords are RUNTIME pointers the engine's reloc pass wrote as (canon VA + game_ram base);
 * game_ram->to_ptr(canon) reproduces that value, so an action's record is matched by its handler
 * WITHOUT hardcoding the host mapping base (the VA->host-pointer op).
 *
 * How the two callbacks are used:
 *   on_game_ram_ready (MAIN):     game_ram staged pristine; patch the two tables.
 *   on_scancode       (TICK_ISR): menu-nav keys -> arrow while a menu owns input
 *                                 (g_player_movement_enabled != 1); passthrough during gameplay.
 * Config path: <plugin_dir>/keybinds.cfg (beside plugin.so), overridable with ROTH_KEYBINDS=/path.
 * ROTH_KEYBINDS_DUMP=1 prints the effective tables. Missing file = no-op.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strcasecmp / strncasecmp (POSIX) — needs _GNU_SOURCE (see Makefile) */
#include <stdint.h>
#include <ctype.h>

#include "roth_sdk.h"

#define KEYMAP_MAX   64          /* per-table walk guard (real tables: 37 and 12 records) */

/* the two keybind tables (index = kb_action_t.tbl) — canonical VAs (game_ram offsets) */
static const struct { const char *name; uint32_t canon; } kb_tables[] = {
    { "press", 0x7093du },       /* g_keymap_table */
    { "held",  0x707f1u },       /* g_held_key_move_table */
};

/* ---- the bindable actions (handler canon VAs from the original tables; names ours) ---- */
typedef struct { const char *name; uint32_t va; uint8_t tbl; } kb_action_t;
static const kb_action_t kb_actions[] = {
    /* -- held: movement / look / jump / crouch (g_held_key_move_table) -- */
    { "move_forward",      0x12686, 1 }, /* Up      move_input_forward */
    { "move_backward",     0x12668, 1 }, /* Down    move_input_backward */
    { "turn_left",         0x126b6, 1 }, /* Left    turn_input_left */
    { "turn_right",        0x12703, 1 }, /* Right   turn_input_right */
    { "strafe_left",       0x126ad, 1 }, /* ,       move_input_strafe_left */
    { "strafe_right",      0x126a4, 1 }, /* .       move_input_strafe_right */
    { "look_up",           0x12927, 1 }, /* Home    look_pitch_up */
    { "look_down",         0x12939, 1 }, /* End     look_pitch_down */
    { "look_recenter_up",  0x1296f, 1 }, /* PgUp    look_pitch_recenter_up */
    { "look_recenter_down",0x1294b, 1 }, /* PgDn    look_pitch_recenter_down */
    { "jump",              0x1c5f9, 1 }, /* A       key_a_jump */
    { "crouch",            0x1c5d0, 1 }, /* Z       key_z_crouch */
    /* -- press: the action keys (g_keymap_table) -- */
    { "sprint_toggle",     0x17fed, 0 }, /* CapsLock  key_toggle_sprint */
    { "weapon_overlay",    0x175d8, 0 }, /* F1        key_toggle_weapon_overlay */
    { "subtitles",         0x1f89b, 0 }, /* F2        key_toggle_subtitles */
    { "easy_select",       0x1801c, 0 }, /* F3        key_toggle_easy_select */
    { "display_cycle_alt", 0x147a8, 0 }, /* F4        key_cycle_display_type_alt */
    { "mouse_swap",        0x17fd2, 0 }, /* F5        key_toggle_mouse_swap */
    { "display_cycle",     0x14794, 0 }, /* F6        key_cycle_display_type */
    { "noop_f7",           0x17fd1, 0 }, /* F7        key_f7_noop */
    { "show_version",      0x14c9c, 0 }, /* F8        key_show_version */
    { "quicksave",         0x14ce5, 0 }, /* F9        key_quicksave */
    { "quickload",         0x14cc9, 0 }, /* F10       key_quickload */
    { "gamma_up",          0x14601, 0 }, /* C         key_gamma_increase */
    { "gamma_down",        0x145ec, 0 }, /* V         key_gamma_decrease */
    { "snapshot",          0x11124, 0 }, /* B         check_snapshot_key */
    { "view_scale_0",      0x14573, 0 }, /* 0         key_set_view_scale */
    { "view_smaller",      0x14678, 0 }, /* -         key_view_size_decrease */
    { "view_larger",       0x14613, 0 }, /* =         key_view_size_increase */
    { "noop_n",            0x145a4, 0 }, /* N         key_n_vestigial */
    { "escape_menu",       0x145ae, 0 }, /* Esc       the quit/menu prompt appendage */
    { "flush_textures",    0x144fb, 0 }, /* F         key_flush_texture_cache */
    { "wireframe_map",     0x1444c, 0 }, /* L         key_toggle_wireframe_map */
    { "choice_down",       0x146ba, 0 }, /* Down      choice_select_next */
    { "choice_up",         0x146c9, 0 }, /* Up        choice_select_prev */
    { "dev_map_menu",      0x17fa4, 0 }, /* W         dev_toggle_map_menu */
    { "help_overlay",      0x17fc0, 0 }, /* H         key_toggle_help_overlay */
    { "use_enter",         0x173f4, 0 }, /* Enter     use_enter_key_handler */
    { "noop_t",            0x14ca7, 0 }, /* T         key_t_handler_vestigial */
    { "render_mode",       0x144da, 0 }, /* M         key_m_toggle_render_mode */
    { "weapon_1",          0x1be8e, 0 }, /* 1         reset_weapon_hud (fist/weapon 1) */
    { "weapon_2",          0x1bea9, 0 }, /* 2         select_weapon_by_number(1) mid-entry */
    { "weapon_3",          0x1beb0, 0 }, /* 3         select_weapon_by_number(2) */
    { "weapon_4",          0x1beb7, 0 }, /* 4         select_weapon_by_number(3) */
    { "weapon_5",          0x1bebe, 0 }, /* 5         select_weapon_by_number(4) */
    { "weapon_6",          0x1bec5, 0 }, /* 6         select_weapon_by_number(5) */
    { "dev_open_door",     0x3df96, 0 }, /* Space     dev_open_nearest_door */
    { "inventory",         0x1a132, 0 }, /* I         update_ui_overlay (inventory screen) */
    { "fire",              0x14cb6, 0 }, /* Ctrl      key_fire_weapon */
};

/* ---- menu-nav virtual binds: source key -> arrow scancode WHILE A MENU IS UP (input seam). The
 * menus read arrows 0x48/0x50/0x4b/0x4d HARDCODED, so they can't be table-rebound; the seam rewrites
 * the source key to the arrow ONLY when g_player_movement_enabled != 1 (menu/modal owns input). ---- */
#define KB_MOVE_ENABLED_CANON 0x7674au          /* g_player_movement_enabled: 1 = gameplay; else menu/dead */
typedef struct { const char *name; uint8_t arrow; } kb_menuvirt_t;
static const kb_menuvirt_t kb_menuvirt[] = {
    { "menu_up",    0x48 }, { "menu_down",  0x50 },
    { "menu_left",  0x4b }, { "menu_right", 0x4d },
};
static struct { uint8_t src; uint8_t arrow; } g_menu_xlat[8];
static unsigned g_menu_xlat_n;

/* ---- key names -> set-1 make scancodes (single-byte; no E0-extended keys) ---- */
typedef struct { const char *name; uint8_t sc; } kb_key_t;
static const kb_key_t kb_keys[] = {
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
    {"kp7",0x47},{"kp8",0x48},{"kp9",0x49},{"kp4",0x4b},{"kp5",0x4c},{"kp6",0x4d},
    {"kp1",0x4f},{"kp2",0x50},{"kp3",0x51},{"kp0",0x52},
    {"up",0x48},{"down",0x50},{"left",0x4b},{"right",0x4d},
};

/* the host API table for the apply-time helpers (set in on_game_ram_ready) */
static const struct roth_api_v1 *g_api;
#define GR (g_api->game_ram)

/* ---- game_ram record accessors (bounded little-endian; the handler dword is an unaligned LE u32
 * the bounded accessor reads correctly). ---- */
static uint8_t  rec_sc(uint32_t rec)              { return GR->u8(rec); }
static uint32_t rec_handler(uint32_t rec)         { return GR->u32(rec + 1); }
static uint32_t handler_of(uint32_t canon)        { return (uint32_t)(uintptr_t)GR->to_ptr(canon); }

static const char *action_of(uint32_t handler_rt)
{
    for (unsigned i = 0; i < sizeof kb_actions / sizeof *kb_actions; i++)
        if (handler_of(kb_actions[i].va) == handler_rt) return kb_actions[i].name;
    return "?";
}
static const char *keyname_of(uint8_t sc)
{
    for (unsigned i = 0; i < sizeof kb_keys / sizeof *kb_keys; i++)
        if (kb_keys[i].sc == sc) return kb_keys[i].name;
    return "?";
}

/* find (within table `tbl`) the record whose handler is `va` / whose scancode is `sc`; 0 = none */
static uint32_t find_by_action(uint8_t tbl, uint32_t va)
{
    uint32_t rec = kb_tables[tbl].canon;
    for (int i = 0; i < KEYMAP_MAX && rec_sc(rec) != 0; i++, rec += 5)
        if (rec_handler(rec) == handler_of(va)) return rec;
    return 0;
}
static uint32_t find_by_key(uint8_t tbl, uint8_t sc)
{
    uint32_t rec = kb_tables[tbl].canon;
    for (int i = 0; i < KEYMAP_MAX && rec_sc(rec) != 0; i++, rec += 5)
        if (rec_sc(rec) == sc) return rec;
    return 0;
}

static void kb_dump(void)
{
    for (unsigned t = 0; t < sizeof kb_tables / sizeof *kb_tables; t++) {
        fprintf(stderr, "[mods:keybinds] effective %s table (@%#x):\n", kb_tables[t].name, kb_tables[t].canon);
        uint32_t rec = kb_tables[t].canon;
        for (int i = 0; i < KEYMAP_MAX && rec_sc(rec) != 0; i++, rec += 5)
            fprintf(stderr, "[mods:keybinds]   %-10s (sc 0x%02x) -> %s\n",
                    keyname_of(rec_sc(rec)), rec_sc(rec), action_of(rec_handler(rec)));
    }
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static void apply_config(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        if (getenv("ROTH_KEYBINDS"))
            fprintf(stderr, "[mods:keybinds] ROTH_KEYBINDS=%s: cannot open — no rebinds\n", path);
        if (getenv("ROTH_KEYBINDS_DUMP")) kb_dump();
        return;                                   /* no config = the mod is a no-op */
    }
    int applied = 0, lineno = 0;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *h = strchr(line, '#'); if (h) *h = 0;
        h = strchr(line, ';'); if (h) *h = 0;
        char *eq = strchr(line, '=');
        if (!eq) { if (*trim(line)) fprintf(stderr, "[mods:keybinds] %s:%d: no '=' — skipped\n", path, lineno); continue; }
        *eq = 0;
        char *aname = trim(line), *kname = trim(eq + 1);
        if (!*aname && !*kname) continue;

        /* resolve the key first (shared by table actions + menu-virt binds) */
        int sc = -1;
        for (unsigned i = 0; i < sizeof kb_keys / sizeof *kb_keys; i++)
            if (!strcasecmp(kb_keys[i].name, kname)) { sc = kb_keys[i].sc; break; }
        if (sc < 0 && !strncasecmp(kname, "0x", 2)) sc = (int)strtoul(kname, NULL, 16) & 0xff;
        if (sc <= 0 || sc > 0x7f) { fprintf(stderr, "[mods:keybinds] %s:%d: unknown key '%s' — skipped\n", path, lineno, kname); continue; }

        /* menu-nav virtual bind? (input-seam translation, not a table patch) */
        const kb_menuvirt_t *mv = NULL;
        for (unsigned i = 0; i < sizeof kb_menuvirt / sizeof *kb_menuvirt; i++)
            if (!strcasecmp(kb_menuvirt[i].name, aname)) { mv = &kb_menuvirt[i]; break; }
        if (mv) {
            if (g_menu_xlat_n < sizeof g_menu_xlat / sizeof *g_menu_xlat) {
                g_menu_xlat[g_menu_xlat_n].src = (uint8_t)sc;
                g_menu_xlat[g_menu_xlat_n].arrow = mv->arrow;
                g_menu_xlat_n++;
                fprintf(stderr, "[mods:keybinds] [menu] %s -> %s (in-menu = arrow 0x%02x)\n", aname, kname, mv->arrow);
                applied++;
            }
            continue;
        }

        const kb_action_t *act = NULL;
        for (unsigned i = 0; i < sizeof kb_actions / sizeof *kb_actions; i++)
            if (!strcasecmp(kb_actions[i].name, aname)) { act = &kb_actions[i]; break; }
        if (!act) { fprintf(stderr, "[mods:keybinds] %s:%d: unknown action '%s' — skipped\n", path, lineno, aname); continue; }

        uint32_t ra = find_by_action(act->tbl, act->va);
        if (!ra) { fprintf(stderr, "[mods:keybinds] %s:%d: action '%s' not in the live %s table — skipped\n", path, lineno, aname, kb_tables[act->tbl].name); continue; }
        uint8_t old = rec_sc(ra);
        if (old == (uint8_t)sc) continue;                       /* already bound */

        uint32_t rb = find_by_key(act->tbl, (uint8_t)sc);       /* swap-or-set WITHIN the action's table */
        if (rb) {
            GR->set_u8(rb, old);
            GR->set_u8(ra, (uint8_t)sc);
            fprintf(stderr, "[mods:keybinds] [%s] %s -> %s (swapped: %s -> %s)\n",
                    kb_tables[act->tbl].name, aname, kname, action_of(rec_handler(rb)), keyname_of(old));
        } else {
            GR->set_u8(ra, (uint8_t)sc);
            fprintf(stderr, "[mods:keybinds] [%s] %s -> %s (was %s)\n",
                    kb_tables[act->tbl].name, aname, kname, keyname_of(old));
        }
        applied++;
    }
    fclose(f);
    if (applied) fprintf(stderr, "[mods:keybinds] %d rebind(s) applied from %s\n", applied, path);
    if (getenv("ROTH_KEYBINDS_DUMP")) kb_dump();
}

/* on_game_ram_ready(MAIN): game_ram staged pristine; patch the tables. */
static void on_game_ram_ready(const struct roth_api_v1 *api)
{
    g_api = api;
    char path[2600];
    const char *env = getenv("ROTH_KEYBINDS");
    if (env) snprintf(path, sizeof path, "%s", env);
    else     snprintf(path, sizeof path, "%s/keybinds.cfg", api->plugin_dir());
    apply_config(path);
}

/* on_scancode(TICK_ISR): while a menu/modal owns input (g_player_movement_enabled != 1), rewrite a
 * configured source key to the arrow scancode the hardcoded menu loops read — carrying make/break.
 * During gameplay (== 1) a pure passthrough, so movement is untouched. game_ram reads only (tick-safe). */
static uint8_t on_scancode(const struct roth_api_v1 *api, uint8_t sc)
{
    if (g_menu_xlat_n == 0)
        return sc;
    if (api->game_ram->u8(KB_MOVE_ENABLED_CANON) == 1)
        return sc;                                  /* normal gameplay: no translation */
    uint8_t make = sc & 0x7f, brk = sc & 0x80;
    for (unsigned i = 0; i < g_menu_xlat_n; i++)
        if (g_menu_xlat[i].src == make) {
            uint8_t out = (uint8_t)(g_menu_xlat[i].arrow | brk);
            if (getenv("ROTH_KEYBINDS_DEBUG"))
                fprintf(stderr, "[mods:keybinds] [menu-seam] sc 0x%02x -> 0x%02x (in-menu remap)\n", sc, out);
            return out;
        }
    return sc;
}

static void on_load(const struct roth_api_v1 *api)
{
    fprintf(stderr, "[mods:keybinds] on_load (MAIN): cfg dir = %s\n", api->plugin_dir());
}

static const struct roth_plugin_info_v1 KEYBINDS = {
    .abi_major     = ROTH_ABI_MAJOR,
    .abi_minor     = ROTH_ABI_MINOR,
    .struct_size   = sizeof(struct roth_plugin_info_v1),
    .id            = "ire.roth.keybinds",
    .name          = "Keybinds (action-key rebinds + menu-nav)",
    .version       = "1.0.0",
    .sdk_req_major = ROTH_SDK_MAJOR,
    .sdk_req_minor = ROTH_SDK_MINOR,
    .api_use       = ROTH_API_USE_GAME_RAM | ROTH_API_USE_INPUT,
    .on_load           = on_load,
    .on_game_ram_ready = on_game_ram_ready,   /* table rebinds (game_ram + to_ptr) */
    .on_frame_game     = NULL,
    .on_compose_tick   = NULL,
    .on_audio          = NULL,
    .on_unload         = NULL,
    .on_register_overrides = NULL,
    .on_scancode       = on_scancode,         /* menu-nav seam translation */
    .on_mouse_poll     = NULL,
};

ROTH_PLUGIN_EXPORT const struct roth_plugin_info_v1 *roth_plugin_query_v1(void)
{
    return &KEYBINDS;
}
