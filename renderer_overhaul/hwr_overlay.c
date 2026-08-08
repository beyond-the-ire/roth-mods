#include "hwr_overlay.h"

#include "hwr_config.h"
#include "hwr_enh_truecolor.h"
#include "hwr_look.h"
#include "hwr_overlay_draw.h"
#include "hwr_perf.h"
#include "hwr_schema.h"
#include "hwr_settings.h"
#include "hwr_sysinfo.h"
#include "hwr_util.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define VA_G_SCREEN_PITCH        0x85498u
#define VA_G_SCREEN_HEIGHT       0x854a0u
#define VA_G_FRAME_TICK_COUNTER  0x90bccu
#define VA_G_MOUSE_RELATIVE_MODE 0x7e8d0u

/* Set-1 make scancodes the panel reads. */
enum {
    HWR_SC_ESC = 0x01,
    HWR_SC_MINUS = 0x0c,
    HWR_SC_BACKSPACE = 0x0e,
    HWR_SC_TAB = 0x0f,
    HWR_SC_ENTER = 0x1c,
    HWR_SC_LSHIFT = 0x2a,
    HWR_SC_PERIOD = 0x34,
    HWR_SC_RSHIFT = 0x36,
    HWR_SC_HOME = 0x47,
    HWR_SC_UP = 0x48,
    HWR_SC_PGUP = 0x49,
    HWR_SC_LEFT = 0x4b,
    HWR_SC_RIGHT = 0x4d,
    HWR_SC_END = 0x4f,
    HWR_SC_DOWN = 0x50,
    HWR_SC_PGDN = 0x51,
    HWR_SC_F11 = 0x57,
    HWR_SC_F12 = 0x58,
    HWR_SC_EXTENDED = 0xe0
};

/* Hold-to-repeat, in engine ticks, matching the pace the engine's own held
 * keys run at. */
#define HWR_HOLD_DELAY  28
#define HWR_HOLD_REPEAT 6

/* A panel as a share of the engine frame. */
#define HWR_PANEL_PCT_W 78u
#define HWR_PANEL_PCT_H 82u

enum { HWR_RING_SIZE = 32u };
/* How many rows one section may put in front of a player. It bounds the array
 * below rather than the panel -- the panel scrolls -- and it has to stand clear
 * of the largest section the schema holds, because a section that reaches it
 * loses its LAST rows and says nothing. The enhancements section is the
 * largest, and adding the room's reach to it took that section to twenty-five. */
enum { HWR_OVERLAY_MAX_ROWS = 40 };
/* One key of the ring carries whether shift was down with it. */
#define HWR_RING_SHIFT 0x80u

enum {
    HWR_ZONE_SECTIONS = 0,
    HWR_ZONE_ROWS,
    HWR_ZONE_ACTIONS,
    HWR_ZONE_COUNT
};

enum {
    HWR_W_TOGGLE = 0,
    HWR_W_CYCLER,
    HWR_W_NUMBER,
    HWR_W_RESOLUTION,
    HWR_W_KEY,
    HWR_W_COLOR,
    HWR_W_INFO
};

enum {
    HWR_EDIT_NONE = 0,
    HWR_EDIT_TEXT,
    HWR_EDIT_KEY
};

enum {
    HWR_HIT_NONE = 0,
    HWR_HIT_SECTION,
    HWR_HIT_ROW,
    HWR_HIT_DECREASE,
    HWR_HIT_INCREASE,
    HWR_HIT_ACTION,
    HWR_HIT_SCROLL_UP,
    HWR_HIT_SCROLL_DOWN,
    /* The line that asks about a display change is the thing to click to keep
     * it, so the answer is reachable without the keyboard the change may have
     * just made hard to see. */
    HWR_HIT_CONFIRM
};

enum {
    HWR_ACTION_APPLY = 0,
    HWR_ACTION_CANCEL,
    HWR_ACTION_DEFAULTS,
    HWR_ACTION_ORIGINAL,
    HWR_ACTION_RELOAD,
    HWR_ACTION_COUNT
};

static const char *const HWR_ACTION_LABELS[HWR_ACTION_COUNT] = {
    "Apply", "Cancel", "Defaults", "Original", "Reload From File"
};

/* How long a display change stands unanswered before the settings it replaced
 * are put back. Long enough to find the keyboard, short enough that a screen
 * nobody can see does not stay. */
#define HWR_CONFIRM_SECONDS 15u

static const char *const HWR_SECTION_LABELS[] = {
    "Display", "View", "Rendering", "Enhancements", "System"
};
#define HWR_SECTION_COUNT \
    ((int)(sizeof HWR_SECTION_LABELS / sizeof HWR_SECTION_LABELS[0]))

static const char *const HWR_SECTION_EMPTY[HWR_SECTION_COUNT] = {
    "No display settings are available in this build.",
    "No view settings are available in this build.",
    "No renderer settings are available in this build.",
    "No enhancements are available in this build.",
    "No information is available in this build."
};

/* The resolutions the panel offers. Every one of them is still put through the
 * schema before it is taken, so this list proposes and the schema decides. */
static const struct {
    uint32_t width;
    uint32_t height;
} HWR_RESOLUTIONS[] = {
    { 0u, 0u },
    { 640u, 480u },
    { 800u, 600u },
    { 1024u, 768u },
    { 1280u, 720u },
    { 1366u, 768u },
    { 1600u, 900u },
    { 1920u, 1080u },
    { 2560u, 1440u },
    { 3440u, 1440u },
    { 3840u, 1600u },
    { 3840u, 2160u }
};
#define HWR_RESOLUTION_COUNT \
    ((int)(sizeof HWR_RESOLUTIONS / sizeof HWR_RESOLUTIONS[0]))

struct hwr_overlay_row {
    int index;      /* schema row */
    int partner;    /* the second schema row of a pair, or -1 */
    int widget;
    int selectable;
};

struct hwr_overlay_layout {
    uint32_t x0, y0, x1, y1;
    uint32_t cx0, cy0, cx1, cy1;
    uint32_t scale;
    uint32_t row_h;
    uint32_t title_y;
    uint32_t strip_y;
    uint32_t rows_y0;
    uint32_t rows_y1;
    uint32_t help_y;
    uint32_t status_y;
    uint32_t actions_y;
    uint32_t content_x1;
    uint32_t value_x;
    uint32_t arrow_x;
    uint32_t arrow_w;
    uint32_t arrow_h;
    uint32_t arrow_up_y;
    uint32_t arrow_dn_y;
    uint32_t tab_x;
    uint32_t tab_w;
    uint32_t action_x;
    uint32_t action_w;
    int action_columns;
    int action_lines;
    int frame;
    int arrows;
    int compact;
    int visible;
};

/* ---- state the two tick callbacks share with the game thread ---- */
static volatile uint8_t g_ring[HWR_RING_SIZE];
static volatile uint32_t g_ring_head;
static volatile uint32_t g_ring_tail;
static volatile int g_keys_dropped;
static volatile int g_open;
static volatile int g_close_request;
static volatile int g_capture_armed;
static volatile int g_can_open;
static int g_available;
static volatile uint32_t g_toggle_key = HWR_SC_F12;
static volatile int g_shift_held;
/* Esc belongs to the panel rather than to closing it: an editor, a question the
 * panel has asked, or a display change waiting for an answer. */
static volatile int g_modal;
/* A display change is waiting to be answered, and when it stops waiting. The
 * deadline is milliseconds from the same clock the tick reads, kept in one
 * 32-bit word so the tick can never see half of it. */
static volatile int g_confirm_pending;
static volatile int g_confirm_expired;
static volatile uint32_t g_confirm_deadline_ms;

/* ---- state the game thread owns ---- */
static int g_active;
static int g_redraw;
static uint32_t g_frame_w;
static uint32_t g_frame_h;
static int g_zone;
static int g_section;
static int g_focus;
static int g_action;
static int g_scroll;
static int g_edit;
static int g_edit_row;
static char g_edit_text[24];
static size_t g_edit_length;
static struct hwr_settings g_draft;
static struct hwr_settings g_drawn_store;
static char g_status[160];
static int g_status_error;
static struct hwr_overlay_row g_rows[HWR_OVERLAY_MAX_ROWS];
static int g_row_count;
static int g_hover_kind;
static int g_hover_index;
static int g_cursor_owned;
static double g_cursor_fx;
static double g_cursor_fy;
static int32_t g_cursor_x;
static int32_t g_cursor_y;
static uint32_t g_buttons;
static uint32_t g_buttons_prev;
static int g_hold_kind;
static int g_hold_index;
static uint16_t g_hold_press_tick;
static uint16_t g_hold_last_tick;
static uint32_t g_panel_x;
static uint32_t g_panel_y;
static uint32_t g_panel_w;
static uint32_t g_panel_h;
/* The panel has asked whether to throw the draft away. */
static int g_guard;
/* The panel is to come down as soon as the keys in hand are dealt with. */
static int g_close_now;
/* The outcome of the last thing that happened to the settings file, so a new
 * one can be told from the one already on the status line. */
static uint32_t g_outcome_serial;
/* A reload was asked for, so the draft follows the file when it lands. */
static int g_await_reload;
/* What to say when it lands, for a reload that is the second half of something
 * the panel did rather than a reading of the file for its own sake. */
static char g_await_status[160];
/* The countdown as it was last drawn, so the panel is redrawn once a second
 * rather than every tick. */
static int g_confirm_drawn_seconds;

/* ------------------------------- the ring -------------------------------- */

static void hwr_overlay_ring_push(uint8_t entry)
{
    uint32_t head = g_ring_head;
    uint32_t next = (head + 1u) % HWR_RING_SIZE;

    if (next == g_ring_tail) {
        /* Only reachable when the game thread has already stopped draining,
         * in which case the newest key is the one worth keeping. */
        g_ring_tail = (g_ring_tail + 1u) % HWR_RING_SIZE;
        g_keys_dropped = 1;
    }
    g_ring[head] = entry;
    __asm__ __volatile__("" ::: "memory");
    g_ring_head = next;
}

static int hwr_overlay_ring_pop(uint8_t *entry)
{
    uint32_t tail = g_ring_tail;

    if (tail == g_ring_head)
        return 0;
    *entry = g_ring[tail];
    g_ring_tail = (tail + 1u) % HWR_RING_SIZE;
    return 1;
}

static void hwr_overlay_ring_reset(void)
{
    g_ring_tail = g_ring_head;
    g_keys_dropped = 0;
}

/* ---------------------------- the scancode seam --------------------------- */

uint8_t hwr_overlay_scancode(uint8_t scancode)
{
    uint8_t make;

    /* The extended prefix is tested before anything else: it carries the high
     * bit a break carries, and passing it on while swallowing the key it
     * introduces would leave the engine holding a prefix for a key that never
     * arrives. */
    if (g_open && scancode == HWR_SC_EXTENDED)
        return 0;
    /* A break always passes. Swallowing one would leave the key latched in the
     * engine's held-key bitmap and the player walking into a wall. */
    if ((scancode & 0x80u) != 0) {
        make = (uint8_t)(scancode & 0x7fu);
        if (make == HWR_SC_LSHIFT || make == HWR_SC_RSHIFT)
            g_shift_held = 0;
        return scancode;
    }
    make = scancode;
    if (!g_open) {
        if (make == (uint8_t)g_toggle_key && g_can_open) {
            /* Retract any close the game thread has not carried out yet, so
             * closing and reopening faster than it pumps still reopens. */
            g_close_request = 0;
            g_open = 1;
            return 0;
        }
        return scancode;
    }
    if (make == HWR_SC_LSHIFT || make == HWR_SC_RSHIFT) {
        g_shift_held = 1;
        return 0;
    }
    /* While the panel waits for a key to bind, every key is that key -- which
     * is why closing is left to the game thread for as long as it lasts. Esc
     * likewise belongs to the panel while something in it is waiting for an
     * answer; the key that opens the panel always closes it. */
    if (!g_capture_armed &&
        (make == (uint8_t)g_toggle_key || (make == HWR_SC_ESC && !g_modal))) {
        g_close_request = 1;
        g_open = 0;
        return 0;
    }
    hwr_overlay_ring_push(
        (uint8_t)(make | (g_shift_held ? HWR_RING_SHIFT : 0u)));
    return 0;
}

/* Milliseconds from a clock that only goes forward. Comparisons are made on the
 * difference so that the wrap this word does every seven weeks costs nothing. */
static uint32_t hwr_overlay_now_ms(void)
{
    return (uint32_t)(hwr_perf_now_ns() / 1000000u);
}

void hwr_overlay_compose(const struct roth_api_v1 *api, uint8_t *pixels,
                         uint32_t width, uint32_t height)
{
    /* The tick keeps running when the game thread is struggling with the very
     * display change that is waiting to be answered, so noticing that its time
     * is up belongs here. Putting the earlier settings back is file work, and
     * file work belongs to the game thread. */
    if (g_confirm_pending && !g_confirm_expired &&
        (int32_t)(hwr_overlay_now_ms() - g_confirm_deadline_ms) >= 0)
        g_confirm_expired = 1;
    if (!g_open)
        return;
    hwr_overlay_draw_compose(api, pixels, width, height);
}

int hwr_overlay_is_open(void)
{
    return g_open != 0;
}

int hwr_overlay_rect(uint32_t *x, uint32_t *y, uint32_t *width,
                     uint32_t *height)
{
    if (g_panel_w == 0 || g_panel_h == 0)
        return 0;
    if (x != NULL)
        *x = g_panel_x;
    if (y != NULL)
        *y = g_panel_y;
    if (width != NULL)
        *width = g_panel_w;
    if (height != NULL)
        *height = g_panel_h;
    return 1;
}

/* -------------------------------- the rows -------------------------------- */

static void hwr_overlay_status(const char *text, int error)
{
    snprintf(g_status, sizeof g_status, "%s", text != NULL ? text : "");
    g_status_error = error;
    /* The System section keeps the last message, so one that has been replaced
     * on the status line is still somewhere to be read. */
    if (g_status[0] != 0)
        hwr_sysinfo_note_message(g_status);
    g_redraw = 1;
}

/* Esc is the panel's own while an editor is open, while the panel has asked a
 * question, or while a display change is waiting to be answered. */
static void hwr_overlay_update_modal(void)
{
    g_modal = g_guard || g_confirm_pending || g_edit != HWR_EDIT_NONE;
}

static int hwr_overlay_row_visible(const struct hwr_setting_desc *desc)
{
    return desc->exposure == HWR_EXP_CONSUMER ||
           desc->exposure == HWR_EXP_READONLY;
}

static int hwr_overlay_widget_of(const struct hwr_setting_desc *desc)
{
    if (desc->exposure == HWR_EXP_READONLY)
        return HWR_W_INFO;
    if (desc->widget == HWR_WIDGET_RESOLUTION_PAIR)
        return HWR_W_RESOLUTION;
    if (desc->widget == HWR_WIDGET_KEY_CAPTURE)
        return HWR_W_KEY;
    switch (desc->type) {
    case HWR_VT_BOOL:
        return HWR_W_TOGGLE;
    case HWR_VT_ENUM:
        return HWR_W_CYCLER;
    case HWR_VT_U32:
    case HWR_VT_F32:
        return HWR_W_NUMBER;
    case HWR_VT_COLOR:
        return HWR_W_COLOR;
    }
    return HWR_W_INFO;
}

/* Whether the row's control is one that steps: a value with two ends and an
 * order between them, which is what the < and > either side of it mean. A
 * colour has neither, so it is typed rather than stepped. */
static int hwr_overlay_widget_steps(int widget)
{
    return widget != HWR_W_COLOR && widget != HWR_W_INFO;
}

/* A row whose parent is off in the draft is shown and never taken: a dependency
 * is reported, never quietly satisfied by turning its parent on. */
static int hwr_overlay_row_enabled(const struct hwr_setting_desc *desc)
{
    const struct hwr_setting_desc *parent;
    int parent_index;

    if (desc->available != NULL && !desc->available())
        return 0;
    parent_index = hwr_schema_depends_on(desc);
    if (parent_index < 0)
        return 1;
    parent = hwr_schema_at((size_t)parent_index);
    /* A row can only be held back by something the player can see and turn on.
     * Naming a parent nobody is shown -- the true-colour foundation, which is
     * derived state rather than a choice -- says where a row belongs, not that
     * it is unavailable. */
    if (parent == NULL || parent->exposure != HWR_EXP_CONSUMER)
        return 1;
    /* What "on" means for that parent is the table's to say, not the panel's:
     * a switch has one off, and a parent with more than two states says which
     * of them this row has anything to do with. */
    return hwr_schema_dependency_met(desc, &g_draft);
}

static void hwr_overlay_build_rows(void)
{
    size_t count = hwr_schema_count();
    struct hwr_row_set consumed;
    size_t i;

    hwr_row_set_clear(&consumed);
    g_row_count = 0;
    for (i = 0; i < count && g_row_count < HWR_OVERLAY_MAX_ROWS; i++) {
        const struct hwr_setting_desc *desc = hwr_schema_at(i);
        struct hwr_overlay_row *row;
        int partner = -1;

        if ((int)desc->category != g_section || !hwr_overlay_row_visible(desc))
            continue;
        if (hwr_row_set_has(&consumed, (int)i))
            continue;
        if (desc->widget == HWR_WIDGET_RESOLUTION_PAIR) {
            size_t j;

            for (j = i + 1u; j < count; j++) {
                const struct hwr_setting_desc *other = hwr_schema_at(j);

                if ((int)other->category != g_section ||
                    !hwr_overlay_row_visible(other) ||
                    other->widget != HWR_WIDGET_RESOLUTION_PAIR)
                    continue;
                partner = (int)j;
                hwr_row_set_add(&consumed, (int)j);
                break;
            }
        }
        row = &g_rows[g_row_count++];
        row->index = (int)i;
        row->partner = partner;
        row->widget = hwr_overlay_widget_of(desc);
        row->selectable = row->widget != HWR_W_INFO &&
                          hwr_overlay_row_enabled(desc);
    }
}

static const struct hwr_setting_desc *hwr_overlay_row_desc(int row)
{
    if (row < 0 || row >= g_row_count)
        return NULL;
    return hwr_schema_at((size_t)g_rows[row].index);
}

static const struct hwr_setting_desc *hwr_overlay_row_partner(int row)
{
    if (row < 0 || row >= g_row_count || g_rows[row].partner < 0)
        return NULL;
    return hwr_schema_at((size_t)g_rows[row].partner);
}

/* ------------------------------ value display ----------------------------- */

static const char *hwr_overlay_enum_text(const struct hwr_setting_desc *desc,
                                         union hwr_value value)
{
    size_t i;

    for (i = 0; i < desc->value_count; i++) {
        if (desc->values[i].value == value.e)
            return desc->values[i].label != NULL ? desc->values[i].label
                                                 : desc->values[i].token;
    }
    return "?";
}

static int hwr_overlay_is_auto(const struct hwr_setting_desc *desc,
                               union hwr_value value)
{
    if ((desc->flags & HWR_SF_ZERO_IS_AUTO) == 0)
        return 0;
    return desc->type == HWR_VT_F32 ? value.f == 0.0f : value.u == 0u;
}

/* One value of one row in the words the panel shows it in, which are not always
 * the words the settings file spells it with: a switch is On or Off, an
 * enumeration is its own label, and a zero that means "follow" says so. */
static void hwr_overlay_format_value(const struct hwr_setting_desc *desc,
                                     union hwr_value value, char *out,
                                     size_t out_size)
{
    out[0] = 0;
    if (desc == NULL)
        return;
    if (hwr_overlay_is_auto(desc, value)) {
        snprintf(out, out_size, "%s",
                 desc->auto_label != NULL ? desc->auto_label : "Automatic");
        return;
    }
    switch (desc->type) {
    case HWR_VT_BOOL:
        snprintf(out, out_size, "%s", value.b != 0 ? "On" : "Off");
        return;
    case HWR_VT_ENUM:
        snprintf(out, out_size, "%s", hwr_overlay_enum_text(desc, value));
        return;
    default:
        break;
    }
    if (!hwr_schema_format(desc, value, out, out_size))
        snprintf(out, out_size, "?");
}

static void hwr_overlay_value_text(const struct roth_api_v1 *api, int row,
                                   char *out, size_t out_size)
{
    const struct hwr_setting_desc *desc = hwr_overlay_row_desc(row);
    const struct hwr_setting_desc *partner = hwr_overlay_row_partner(row);
    union hwr_value value;

    out[0] = 0;
    if (desc == NULL)
        return;
    if (g_rows[row].widget == HWR_W_INFO) {
        if (desc->read_info == NULL || !desc->read_info(api, out, out_size))
            snprintf(out, out_size, "unavailable");
        return;
    }
    value = hwr_schema_get(&g_draft, desc);
    if (g_rows[row].widget == HWR_W_RESOLUTION && partner != NULL) {
        union hwr_value other = hwr_schema_get(&g_draft, partner);

        if (value.u == 0u && other.u == 0u)
            snprintf(out, out_size, "%s",
                     desc->auto_label != NULL ? desc->auto_label : "Automatic");
        else
            snprintf(out, out_size, "%u x %u", value.u, other.u);
        return;
    }
    hwr_overlay_format_value(desc, value, out, out_size);
}

/* What the map being played says this row is worth, when it says anything. The
 * look file's per-map answers sit above the settings the panel edits, so a dial
 * moved here can be perfectly correct and still change nothing in this room --
 * the row says so rather than leaving the player to wonder. */
static int hwr_overlay_map_text(const struct hwr_setting_desc *desc, char *out,
                                size_t out_size)
{
    if (desc == NULL || !hwr_look_overrides(desc))
        return 0;
    hwr_overlay_format_value(desc, hwr_look_effective(desc, hwr_settings_get()),
                             out, out_size);
    return out[0] != 0;
}

/* A row the player has moved away from what the store holds. */
static int hwr_overlay_row_changed(int row)
{
    const struct hwr_setting_desc *desc = hwr_overlay_row_desc(row);
    const struct hwr_setting_desc *partner = hwr_overlay_row_partner(row);
    const struct hwr_settings *saved = hwr_settings_get();
    union hwr_value a;
    union hwr_value b;

    if (desc == NULL || g_rows[row].widget == HWR_W_INFO)
        return 0;
    a = hwr_schema_get(&g_draft, desc);
    b = hwr_schema_get(saved, desc);
    if (desc->type == HWR_VT_F32 ? a.f != b.f : a.u != b.u)
        return 1;
    if (partner != NULL) {
        a = hwr_schema_get(&g_draft, partner);
        b = hwr_schema_get(saved, partner);
        if (a.u != b.u)
            return 1;
    }
    return 0;
}

/* ------------------------------ value editing ----------------------------- */

static int hwr_overlay_commit(const struct hwr_settings *trial)
{
    char message[160];

    message[0] = 0;
    if (!hwr_schema_validate_all(trial, message, sizeof message)) {
        hwr_overlay_status(message, 1);
        return 0;
    }
    g_draft = *trial;
    g_redraw = 1;
    return 1;
}

static int hwr_overlay_set_value(const struct hwr_setting_desc *desc,
                                 union hwr_value value)
{
    struct hwr_settings trial = g_draft;
    char message[160];

    message[0] = 0;
    if (!hwr_schema_validate(desc, value, message, sizeof message)) {
        hwr_overlay_status(message, 1);
        return 0;
    }
    hwr_schema_set(&trial, desc, value);
    return hwr_overlay_commit(&trial);
}

static int hwr_overlay_set_pair(const struct hwr_setting_desc *width_desc,
                                const struct hwr_setting_desc *height_desc,
                                uint32_t width, uint32_t height)
{
    struct hwr_settings trial = g_draft;
    union hwr_value value;
    char message[160];

    memset(&value, 0, sizeof value);
    message[0] = 0;
    value.u = width;
    if (!hwr_schema_validate(width_desc, value, message, sizeof message)) {
        hwr_overlay_status(message, 1);
        return 0;
    }
    hwr_schema_set(&trial, width_desc, value);
    value.u = height;
    if (!hwr_schema_validate(height_desc, value, message, sizeof message)) {
        hwr_overlay_status(message, 1);
        return 0;
    }
    hwr_schema_set(&trial, height_desc, value);
    return hwr_overlay_commit(&trial);
}

static void hwr_overlay_step_enum(const struct hwr_setting_desc *desc,
                                  int direction)
{
    union hwr_value value = hwr_schema_get(&g_draft, desc);
    size_t count = desc->value_count;
    size_t current = 0;
    size_t i;

    if (count == 0)
        return;
    for (i = 0; i < count; i++) {
        if (desc->values[i].value == value.e) {
            current = i;
            break;
        }
    }
    if (direction > 0)
        current = (current + 1u) % count;
    else
        current = (current + count - 1u) % count;
    value.e = desc->values[current].value;
    hwr_overlay_set_value(desc, value);
}

/* A step is added rather than counted, and a step such as five hundredths is
 * not a number a float holds: ten steps up and ten back land a hair off where
 * they started, which the panel then has to print in full. For an enhancement
 * it is worse than untidy -- a dial that reads as its original without being it
 * is an enhancement that cannot be turned off. Landing back on the grid the
 * steps describe fixes both, and a value typed off that grid keeps its offset
 * because only a value already within a hundredth of a step is moved. */
static void hwr_overlay_snap_step(const struct hwr_setting_desc *desc,
                                  float *value, float step)
{
    double anchor = (double)desc->original.f;
    double steps = ((double)*value - anchor) / (double)step;
    double whole;
    double drift;

    if (!(steps > -1.0e6 && steps < 1.0e6))
        return;
    whole = steps >= 0.0 ? (double)(long)(steps + 0.5)
                         : -(double)(long)(0.5 - steps);
    drift = steps - whole;
    if (drift > -0.01 && drift < 0.01)
        *value = (float)(anchor + whole * (double)step);
}

static void hwr_overlay_step_number(const struct hwr_setting_desc *desc,
                                    int direction, int page, int extreme)
{
    union hwr_value value = hwr_schema_get(&g_draft, desc);
    int automatic = hwr_overlay_is_auto(desc, value);

    if (extreme != 0) {
        if (desc->type == HWR_VT_F32)
            value.f = extreme > 0 ? desc->max.f : desc->min.f;
        else
            value.u = extreme > 0 ? desc->max.u : desc->min.u;
        hwr_overlay_set_value(desc, value);
        return;
    }
    if (desc->type == HWR_VT_F32) {
        float step = page ? desc->page_step.f : desc->step.f;

        if (step <= 0.0f)
            step = page ? 10.0f : 1.0f;
        if (automatic) {
            /* Automatic sits one step below the smallest real value, so it can
             * be reached and left without typing. */
            if (direction <= 0)
                return;
            value.f = desc->min.f;
        } else {
            value.f += (float)direction * step;
            hwr_overlay_snap_step(desc, &value.f, step);
            if (value.f < desc->min.f)
                value.f = (desc->flags & HWR_SF_ZERO_IS_AUTO) != 0
                        ? 0.0f : desc->min.f;
            if (value.f > desc->max.f)
                value.f = desc->max.f;
        }
    } else {
        uint32_t step = page ? desc->page_step.u : desc->step.u;

        if (step == 0u)
            step = page ? 10u : 1u;
        if (automatic) {
            if (direction <= 0)
                return;
            value.u = desc->min.u != 0u ? desc->min.u : step;
        } else if (direction > 0) {
            value.u = value.u > desc->max.u - step ? desc->max.u
                                                   : value.u + step;
        } else if (value.u <= desc->min.u || value.u <= step) {
            value.u = (desc->flags & HWR_SF_ZERO_IS_AUTO) != 0 ? 0u
                                                               : desc->min.u;
        } else {
            value.u -= step;
            if (value.u < desc->min.u)
                value.u = desc->min.u;
        }
    }
    hwr_overlay_set_value(desc, value);
}

static int hwr_overlay_resolution_index(uint32_t width, uint32_t height,
                                        int direction)
{
    int i;

    for (i = 0; i < HWR_RESOLUTION_COUNT; i++) {
        if (HWR_RESOLUTIONS[i].width == width &&
            HWR_RESOLUTIONS[i].height == height)
            return i;
    }
    /* A size that is not on the list still has neighbours on it. */
    if (direction > 0) {
        for (i = 0; i < HWR_RESOLUTION_COUNT; i++) {
            if (HWR_RESOLUTIONS[i].width > width)
                return i - 1;
        }
        return HWR_RESOLUTION_COUNT - 1;
    }
    for (i = HWR_RESOLUTION_COUNT - 1; i >= 0; i--) {
        if (HWR_RESOLUTIONS[i].width < width)
            return i + 1;
    }
    return 0;
}

static void hwr_overlay_step_resolution(const struct hwr_setting_desc *width_desc,
                                        const struct hwr_setting_desc *height_desc,
                                        int direction)
{
    union hwr_value width = hwr_schema_get(&g_draft, width_desc);
    union hwr_value height = hwr_schema_get(&g_draft, height_desc);
    int index = hwr_overlay_resolution_index(width.u, height.u, direction);
    int step = direction > 0 ? 1 : -1;
    int guard;

    for (guard = 0; guard < HWR_RESOLUTION_COUNT; guard++) {
        struct hwr_settings trial = g_draft;
        union hwr_value value;
        char message[160];

        index += step;
        if (index < 0 || index >= HWR_RESOLUTION_COUNT)
            return;
        memset(&value, 0, sizeof value);
        value.u = HWR_RESOLUTIONS[index].width;
        hwr_schema_set(&trial, width_desc, value);
        value.u = HWR_RESOLUTIONS[index].height;
        hwr_schema_set(&trial, height_desc, value);
        message[0] = 0;
        /* An offered size the rules refuse is simply not offered. */
        if (!hwr_schema_validate_all(&trial, message, sizeof message))
            continue;
        g_draft = trial;
        g_redraw = 1;
        return;
    }
}

static void hwr_overlay_adjust(int row, int direction, int page, int extreme)
{
    const struct hwr_setting_desc *desc = hwr_overlay_row_desc(row);
    const struct hwr_setting_desc *partner = hwr_overlay_row_partner(row);
    union hwr_value value;

    if (desc == NULL || !g_rows[row].selectable)
        return;
    switch (g_rows[row].widget) {
    case HWR_W_TOGGLE:
        value = hwr_schema_get(&g_draft, desc);
        value.b = value.b == 0;
        hwr_overlay_set_value(desc, value);
        return;
    case HWR_W_CYCLER:
    case HWR_W_KEY:
        hwr_overlay_step_enum(desc, direction);
        return;
    case HWR_W_NUMBER:
        hwr_overlay_step_number(desc, direction, page, extreme);
        return;
    case HWR_W_RESOLUTION:
        if (partner != NULL)
            hwr_overlay_step_resolution(desc, partner, direction);
        return;
    default:
        return;
    }
}

/* ------------------------------ typed entry ------------------------------- */

static char hwr_overlay_edit_char(uint8_t make)
{
    static const char row_digits[] = "1234567890";
    static const char row_q[] = "qwertyuiop";
    static const char row_a[] = "asdfghjkl";
    static const char row_z[] = "zxcvbnm";

    if (make >= 0x02u && make <= 0x0bu)
        return row_digits[make - 0x02u];
    if (make >= 0x10u && make <= 0x19u)
        return row_q[make - 0x10u];
    if (make >= 0x1eu && make <= 0x26u)
        return row_a[make - 0x1eu];
    if (make >= 0x2cu && make <= 0x32u)
        return row_z[make - 0x2cu];
    if (make == HWR_SC_PERIOD)
        return '.';
    if (make == HWR_SC_MINUS)
        return '-';
    return 0;
}

static void hwr_overlay_begin_edit(int row)
{
    const struct hwr_setting_desc *desc = hwr_overlay_row_desc(row);
    const struct hwr_setting_desc *partner = hwr_overlay_row_partner(row);

    if (desc == NULL)
        return;
    g_edit_text[0] = 0;
    if (g_rows[row].widget == HWR_W_RESOLUTION && partner != NULL) {
        union hwr_value width = hwr_schema_get(&g_draft, desc);
        union hwr_value height = hwr_schema_get(&g_draft, partner);

        if (width.u != 0u || height.u != 0u)
            snprintf(g_edit_text, sizeof g_edit_text, "%ux%u", width.u,
                     height.u);
        hwr_overlay_status("Type a size as width x height, or 0 to follow the "
                           "engine. Enter accepts, Esc cancels.", 0);
    } else {
        hwr_schema_format(desc, hwr_schema_get(&g_draft, desc), g_edit_text,
                          sizeof g_edit_text);
        /* A colour is six hexadecimal digits, red then green then blue, which
         * is the one value on this screen whose spelling has to be said. */
        if (desc->type == HWR_VT_COLOR)
            hwr_overlay_status("Type a colour as six hexadecimal digits, red "
                               "green blue, such as 806040. Enter accepts, Esc "
                               "cancels.", 0);
        else
            hwr_overlay_status("Type a value. Enter accepts, Esc cancels.", 0);
    }
    g_edit_length = strlen(g_edit_text);
    g_edit = HWR_EDIT_TEXT;
    g_edit_row = row;
    hwr_overlay_update_modal();
    g_redraw = 1;
}

static void hwr_overlay_end_edit(void)
{
    g_edit = HWR_EDIT_NONE;
    g_edit_row = -1;
    g_edit_text[0] = 0;
    g_edit_length = 0;
    g_capture_armed = 0;
    hwr_overlay_update_modal();
    g_redraw = 1;
}

static void hwr_overlay_commit_edit(void)
{
    const struct hwr_setting_desc *desc = hwr_overlay_row_desc(g_edit_row);
    const struct hwr_setting_desc *partner = hwr_overlay_row_partner(g_edit_row);
    char message[160];
    union hwr_value value;

    if (desc == NULL) {
        hwr_overlay_end_edit();
        return;
    }
    message[0] = 0;
    if (g_rows[g_edit_row].widget == HWR_W_RESOLUTION && partner != NULL) {
        char text[24];
        char *separator;
        union hwr_value height;

        snprintf(text, sizeof text, "%s", g_edit_text);
        separator = strpbrk(text, "xX*");
        if (separator == NULL) {
            if (!hwr_schema_parse(desc, text, &value, message, sizeof message)) {
                hwr_overlay_status(message, 1);
                return;
            }
            if (value.u != 0u) {
                hwr_overlay_status("A size is a width and a height, as 1280x720.",
                                   1);
                return;
            }
            if (hwr_overlay_set_pair(desc, partner, 0u, 0u))
                hwr_overlay_end_edit();
            return;
        }
        *separator = 0;
        if (!hwr_schema_parse(desc, text, &value, message, sizeof message) ||
            !hwr_schema_parse(partner, separator + 1, &height, message,
                              sizeof message)) {
            hwr_overlay_status(message, 1);
            return;
        }
        if (hwr_overlay_set_pair(desc, partner, value.u, height.u))
            hwr_overlay_end_edit();
        return;
    }
    if (!hwr_schema_parse(desc, g_edit_text, &value, message, sizeof message)) {
        hwr_overlay_status(message, 1);
        return;
    }
    if (hwr_overlay_set_value(desc, value))
        hwr_overlay_end_edit();
}

static void hwr_overlay_edit_key(uint8_t make)
{
    const struct hwr_setting_desc *desc = hwr_overlay_row_desc(g_edit_row);
    union hwr_value value;
    char message[160];

    if (desc == NULL) {
        hwr_overlay_end_edit();
        return;
    }
    if (make == HWR_SC_ESC) {
        hwr_overlay_status("Left as it was.", 0);
        hwr_overlay_end_edit();
        return;
    }
    memset(&value, 0, sizeof value);
    value.e = (int32_t)make;
    message[0] = 0;
    if (!hwr_schema_validate(desc, value, message, sizeof message)) {
        hwr_overlay_status("That key cannot be used for this.", 1);
        return;
    }
    if (!hwr_overlay_set_value(desc, value))
        return;
    /* The engine's own key table binds everything below F11, so anything else
     * is offered with a word of warning rather than refused: a player who has
     * rebound the game action is entitled to the key. */
    if (make != HWR_SC_F11 && make != HWR_SC_F12)
        hwr_overlay_status("Taken. The game may also use that key.", 0);
    else
        hwr_overlay_status("Taken.", 0);
    hwr_overlay_end_edit();
}

/* -------------------------------- the draft ------------------------------- */

static void hwr_overlay_reset_draft(void)
{
    g_draft = *hwr_settings_get();
    g_redraw = 1;
}

/* Whether the draft says anything the settings in force do not. Rows the panel
 * never shows count too: the draft carries the whole configuration, so a value
 * only the file has is still something to be asked about before it is lost. */
static int hwr_overlay_draft_dirty(void)
{
    const struct hwr_settings *saved = hwr_settings_get();
    size_t count = hwr_schema_count();
    size_t i;

    for (i = 0; i < count; i++) {
        const struct hwr_setting_desc *desc = hwr_schema_at(i);

        if (desc->offset == HWR_SCHEMA_NO_FIELD)
            continue;
        if (!hwr_schema_value_same(desc, hwr_schema_get(&g_draft, desc),
                                   hwr_schema_get(saved, desc)))
            return 1;
    }
    return 0;
}

/* Whether the draft moves one of the settings that can leave a screen nobody
 * can see. Those are the changes that are asked about and put back if the
 * question goes unanswered; the rest simply take effect. */
static int hwr_overlay_confirm_needed(void)
{
    const struct hwr_settings *saved = hwr_settings_get();
    size_t count = hwr_schema_count();
    size_t i;

    for (i = 0; i < count; i++) {
        const struct hwr_setting_desc *desc = hwr_schema_at(i);

        if ((desc->flags & HWR_SF_NEEDS_CONFIRM) == 0 ||
            desc->offset == HWR_SCHEMA_NO_FIELD)
            continue;
        if (!hwr_schema_value_same(desc, hwr_schema_get(&g_draft, desc),
                                   hwr_schema_get(saved, desc)))
            return 1;
    }
    return 0;
}

static void hwr_overlay_begin_confirm(void)
{
    g_confirm_deadline_ms = hwr_overlay_now_ms() + HWR_CONFIRM_SECONDS * 1000u;
    g_confirm_expired = 0;
    g_confirm_pending = 1;
    g_confirm_drawn_seconds = -1;
    hwr_overlay_update_modal();
    /* The line itself is drawn from the countdown, so that it can count. */
    hwr_overlay_status("", 0);
    g_redraw = 1;
}

/* Seconds still on the clock, rounded up so the last one is shown. */
static int hwr_overlay_confirm_seconds(void)
{
    int32_t left;

    if (!g_confirm_pending)
        return 0;
    left = (int32_t)(g_confirm_deadline_ms - hwr_overlay_now_ms());
    if (left < 0)
        return 0;
    return (int)((left + 999) / 1000);
}

static void hwr_overlay_confirm_keep(void)
{
    if (!g_confirm_pending)
        return;
    g_confirm_pending = 0;
    g_confirm_expired = 0;
    hwr_config_release_bytes();
    hwr_overlay_update_modal();
    hwr_overlay_status("Kept.", 0);
}

/* Putting a display change back is only writing a configuration, so it takes
 * the same road every other change takes: the file, then one reload. */
static void hwr_overlay_confirm_revert(const char *reason)
{
    char message[160];

    if (!g_confirm_pending)
        return;
    message[0] = 0;
    g_confirm_pending = 0;
    g_confirm_expired = 0;
    hwr_overlay_update_modal();
    if (hwr_config_restore_bytes(message, sizeof message) != 0) {
        /* The kept copy stays where it is: the next run of the game starts on
         * it, which is the whole reason it is a file and not a buffer. */
        hwr_overlay_status(message, 1);
        return;
    }
    hwr_config_request_reload();
    g_await_reload = 1;
    snprintf(g_await_status, sizeof g_await_status, "%s", reason);
    hwr_overlay_status(reason, 0);
}

static void hwr_overlay_apply(void)
{
    char message[160];
    int confirm;

    message[0] = 0;
    if (!hwr_schema_validate_all(&g_draft, message, sizeof message)) {
        hwr_overlay_status(message, 1);
        return;
    }
    confirm = hwr_overlay_confirm_needed();
    /* The file about to be replaced is kept first, so a display change nobody
     * can see to answer can always be taken back -- by this run of the game or,
     * if it never gets the chance, by the next one. */
    if (confirm && hwr_config_capture_bytes(message, sizeof message) != 0) {
        hwr_overlay_status(message, 1);
        return;
    }
    if (hwr_config_save(&g_draft, message, sizeof message) != 0) {
        /* Nothing was written, so nothing is waiting to be answered either --
         * unless an earlier change already was, whose kept copy is not this
         * one's to drop. */
        if (confirm && !g_confirm_pending)
            hwr_config_release_bytes();
        hwr_overlay_status(message, 1);
        return;
    }
    hwr_config_request_reload();
    if (confirm)
        hwr_overlay_begin_confirm();
    else
        hwr_overlay_status("Saved.", 0);
}

static void hwr_overlay_begin_guard(void)
{
    g_guard = 1;
    g_close_request = 0;
    /* The keystroke callback took the panel down the moment the key arrived;
     * it stays up for as long as the question does. */
    g_open = 1;
    hwr_overlay_update_modal();
    hwr_overlay_status("Leave without applying? Enter throws the changes away, "
                       "Esc goes back to them.", 0);
}

static void hwr_overlay_end_guard(void)
{
    g_guard = 0;
    hwr_overlay_update_modal();
}

static void hwr_overlay_run_action(int action)
{
    switch (action) {
    case HWR_ACTION_APPLY:
        hwr_overlay_apply();
        return;
    case HWR_ACTION_CANCEL:
        hwr_overlay_reset_draft();
        hwr_overlay_status("Back to the settings in force.", 0);
        return;
    case HWR_ACTION_DEFAULTS:
        hwr_schema_defaults(&g_draft);
        hwr_overlay_status("Built-in defaults are in the draft. Apply puts "
                           "them in force.", 0);
        return;
    case HWR_ACTION_ORIGINAL:
        hwr_schema_original(&g_draft);
        hwr_overlay_status("The original look is in the draft: 640 by 480, "
                           "original field of view, every enhancement off.", 0);
        return;
    case HWR_ACTION_RELOAD:
        /* The one road from the file into the settings, asked for by hand
         * instead of by a save. The game configs folder is listed again with it
         * and the chosen file read again: they are the halves of what is on
         * disk, and a file put in that folder while the game was running has to
         * turn up in the picker without a restart. */
        hwr_config_request_reload();
        hwr_look_request_reload();
        g_await_reload = 1;
        hwr_overlay_status("Reading the settings and the game configs folder.",
                           0);
        return;
    default:
        return;
    }
}

/* -------------------------------- the layout ------------------------------ */

static uint32_t hwr_overlay_sub(uint32_t value, uint32_t amount)
{
    return value > amount ? value - amount : 0u;
}

static void hwr_overlay_layout(const struct roth_api_v1 *api,
                               struct hwr_overlay_layout *layout)
{
    uint32_t width = g_frame_w;
    uint32_t height = g_frame_h;
    uint32_t panel_w = width * HWR_PANEL_PCT_W / 100u;
    uint32_t panel_h = height * HWR_PANEL_PCT_H / 100u;
    uint32_t left;
    uint32_t top;
    uint32_t right;
    uint32_t bottom;
    uint32_t inner;
    uint32_t body;
    uint32_t rows_room;
    uint32_t widest_action = 0;
    int i;

    memset(layout, 0, sizeof *layout);
    layout->scale = hwr_overlay_draw_line_scale(api);
    layout->row_h = 14u * layout->scale;
    if (panel_w > hwr_overlay_sub(width, 16u))
        panel_w = hwr_overlay_sub(width, 16u);
    if (panel_h > hwr_overlay_sub(height, 12u))
        panel_h = hwr_overlay_sub(height, 12u);

    hwr_overlay_draw_frame_bands(&left, &top, &right, &bottom);
    layout->frame = hwr_overlay_draw_line_scale(api) == 1u &&
                    panel_w > left + right + 32u &&
                    panel_h > top + bottom + 32u;
    if (layout->frame)
        panel_h = hwr_overlay_draw_frame_snap(panel_h);

    layout->x0 = (width - panel_w) / 2u;
    layout->y0 = (height - panel_h) / 2u;
    layout->x1 = layout->x0 + panel_w;
    layout->y1 = layout->y0 + panel_h;
    if (layout->frame) {
        layout->cx0 = layout->x0 + left;
        layout->cy0 = layout->y0 + top;
        layout->cx1 = layout->x1 - right;
        layout->cy1 = layout->y1 - bottom;
    } else {
        layout->cx0 = layout->x0 + 4u;
        layout->cy0 = layout->y0 + 4u;
        layout->cx1 = hwr_overlay_sub(layout->x1, 4u);
        layout->cy1 = hwr_overlay_sub(layout->y1, 4u);
    }
    if (layout->cx1 <= layout->cx0 + 32u || layout->cy1 <= layout->cy0 + 32u) {
        layout->visible = 0;
        return;
    }
    inner = layout->cx1 - layout->cx0;
    body = layout->cy1 - layout->cy0;

    /* The buttons take one line where they fit on one, and two where the
     * widest of them says otherwise, which is what a 320-wide frame says. */
    for (i = 0; i < HWR_ACTION_COUNT; i++) {
        uint32_t width = hwr_overlay_draw_measure(api, HWR_ACTION_LABELS[i]);

        if (width > widest_action)
            widest_action = width;
    }
    widest_action += 10u;
    layout->action_columns =
        widest_action * (uint32_t)HWR_ACTION_COUNT <= inner
            ? HWR_ACTION_COUNT : (HWR_ACTION_COUNT + 1) / 2;
    layout->action_lines = (HWR_ACTION_COUNT + layout->action_columns - 1) /
                           layout->action_columns;
    /* Title, sections, one setting, the status line, and the buttons are the
     * least a panel can be; anything smaller is not opened over. */
    if (body < (4u + (uint32_t)layout->action_lines) * layout->row_h) {
        layout->visible = 0;
        return;
    }

    /* Top down: the title and the section strip. Bottom up: the actions, the
     * status line, and the help text. The rows take what is left. */
    layout->title_y = layout->cy0;
    layout->strip_y = layout->title_y + layout->row_h + 1u;
    layout->actions_y =
        layout->cy1 - (uint32_t)layout->action_lines * layout->row_h;
    layout->status_y = layout->actions_y - layout->row_h - 1u;
    layout->help_y = layout->status_y - 2u * layout->row_h - 1u;
    layout->rows_y0 = layout->strip_y + layout->row_h + 2u;
    layout->rows_y1 = layout->help_y;
    /* A frame too short for all of it keeps the rows and drops the help. */
    if (body < (8u + (uint32_t)layout->action_lines) * layout->row_h) {
        layout->help_y = layout->status_y;
        layout->rows_y1 = layout->status_y;
    }
    if (layout->rows_y1 <= layout->rows_y0 + layout->row_h) {
        layout->visible = 0;
        return;
    }
    rows_room = layout->rows_y1 - layout->rows_y0;
    layout->visible = (int)(rows_room / layout->row_h);
    if (layout->visible < 1)
        layout->visible = 1;

    /* The section strip collapses to one cycler where five labels cannot fit. */
    layout->compact = inner < 60u * (uint32_t)HWR_SECTION_COUNT;
    layout->tab_w = layout->compact ? inner : inner / (uint32_t)HWR_SECTION_COUNT;
    layout->tab_x = layout->cx0;

    hwr_overlay_draw_arrow_size(api, &layout->arrow_w, &layout->arrow_h);
    layout->arrows = g_row_count > layout->visible &&
                     inner > layout->arrow_w + 80u &&
                     rows_room > 2u * layout->arrow_h + 8u;
    if (layout->arrows) {
        layout->arrow_x = layout->cx1 - layout->arrow_w - 2u;
        layout->content_x1 = layout->arrow_x - 2u;
        layout->arrow_up_y = layout->rows_y0;
        layout->arrow_dn_y = layout->rows_y1 - layout->arrow_h;
    } else {
        layout->arrow_x = 0;
        layout->content_x1 = layout->cx1;
    }
    layout->value_x = layout->cx0 +
                      (layout->content_x1 - layout->cx0) * 45u / 100u;

    layout->action_w =
        (layout->cx1 - layout->cx0) / (uint32_t)layout->action_columns;
    layout->action_x = layout->cx0;
}

/* --------------------------------- drawing -------------------------------- */

/* One help line at a time, broken on spaces. */
static const char *hwr_overlay_wrap(const struct roth_api_v1 *api,
                                    const char *text, uint32_t max_width,
                                    char *out, size_t out_size)
{
    size_t fits;
    size_t length;
    size_t cut;

    while (*text == ' ')
        text++;
    if (*text == 0) {
        out[0] = 0;
        return text;
    }
    length = strlen(text);
    if (length > out_size - 1u)
        length = out_size - 1u;
    memcpy(out, text, length);
    out[length] = 0;
    fits = hwr_overlay_draw_fit(api, out, max_width);
    if (fits >= length)
        return text + length;
    cut = fits;
    while (cut > 0 && out[cut] != ' ')
        cut--;
    if (cut == 0)
        cut = fits;
    out[cut] = 0;
    return text + cut;
}

static void hwr_overlay_draw_row(const struct roth_api_v1 *api,
                                 const struct hwr_overlay_canvas *canvas,
                                 const struct hwr_overlay_layout *layout,
                                 const struct hwr_overlay_colors *colors,
                                 int row, uint32_t y)
{
    const struct hwr_setting_desc *desc = hwr_overlay_row_desc(row);
    char label[96];
    char value[96];
    int focused = g_zone == HWR_ZONE_ROWS && g_focus == row;
    int hovered = g_hover_kind == HWR_HIT_ROW && g_hover_index == row;
    uint8_t label_color = colors->text;
    uint8_t value_color = colors->gold_hi;

    if (desc == NULL)
        return;
    hwr_overlay_draw_fill(canvas, layout->cx0, y, layout->content_x1,
                          y + layout->row_h,
                          hovered ? colors->gold_lo : colors->panel);
    if (focused)
        hwr_overlay_draw_border(canvas, layout->cx0, y, layout->content_x1,
                                y + layout->row_h, colors->gold_hi);
    if (!g_rows[row].selectable) {
        label_color = colors->gold_lo;
        value_color = colors->gold_lo;
    }
    snprintf(label, sizeof label, "%s%s",
             hwr_overlay_row_changed(row) ? "* " : "  ", desc->label);
    hwr_overlay_draw_text(api, canvas, layout->cx0 + 3u, y + layout->scale,
                          layout->value_x - layout->cx0 - 8u, label_color,
                          label);

    if (g_edit == HWR_EDIT_TEXT && g_edit_row == row) {
        snprintf(value, sizeof value, "%s_", g_edit_text);
        value_color = colors->gold_hi;
    } else if (g_edit == HWR_EDIT_KEY && g_edit_row == row) {
        snprintf(value, sizeof value, "press a key");
    } else {
        char active[80];
        size_t used;

        hwr_overlay_value_text(api, row, value, sizeof value);
        /* What the setting asks for and what it is getting are not always the
         * same thing: a size the host would not give, a synchronization this
         * system does not have, a renderer that had to stand down, a map with
         * its own answer for how it is lit. The row says both rather than
         * quietly showing the request as though it were the answer. */
        used = strlen(value);
        if (g_rows[row].widget != HWR_W_INFO && used + 8u < sizeof value &&
            hwr_sysinfo_active_text(api, desc, hwr_settings_get(), active,
                                    sizeof active))
            snprintf(value + used, sizeof value - used, "  (in use: %s)",
                     active);
        used = strlen(value);
        if (g_rows[row].widget != HWR_W_INFO && used + 12u < sizeof value &&
            hwr_overlay_map_text(desc, active, sizeof active))
            snprintf(value + used, sizeof value - used, "  (this map: %s)",
                     active);
    }
    if (hwr_overlay_widget_steps(g_rows[row].widget) && g_rows[row].selectable) {
        uint32_t marker_w = 8u;

        hwr_overlay_draw_text(api, canvas, layout->value_x, y + layout->scale,
                              marker_w, colors->gold_mid, "<");
        hwr_overlay_draw_text(api, canvas,
                              hwr_overlay_sub(layout->content_x1, 11u),
                              y + layout->scale, marker_w, colors->gold_mid,
                              ">");
    }
    hwr_overlay_draw_text(api, canvas, layout->value_x + 12u,
                          y + layout->scale,
                          hwr_overlay_sub(layout->content_x1,
                                          layout->value_x + 26u),
                          value_color, value);
}

static void hwr_overlay_render(const struct roth_api_v1 *api,
                               const struct hwr_overlay_runtime *runtime,
                               const struct hwr_overlay_layout *layout)
{
    struct hwr_overlay_canvas canvas;
    const struct hwr_overlay_colors *colors = hwr_overlay_draw_colors(api);
    char text[160];
    uint32_t y;
    int i;
    int drawn;

    if (layout->visible <= 0)
        return;
    if (!hwr_overlay_draw_begin(g_frame_w, g_frame_h, &canvas))
        return;

    hwr_overlay_draw_clear(&canvas, layout->x0, layout->y0, layout->x1,
                           layout->y1);
    if (!layout->frame ||
        !hwr_overlay_draw_frame(api, &canvas, layout->x0, layout->y0,
                                layout->x1, layout->y1, colors->panel)) {
        hwr_overlay_draw_fill(&canvas, layout->x0, layout->y0, layout->x1,
                              layout->y1, colors->panel);
        hwr_overlay_draw_border(&canvas, layout->x0, layout->y0, layout->x1,
                                layout->y1, colors->gold_mid);
    }
    /* Title. */
    hwr_overlay_draw_fill(&canvas, layout->cx0, layout->title_y, layout->cx1,
                          layout->title_y + layout->row_h, colors->panel);
    hwr_overlay_draw_text(api, &canvas, layout->cx0 + 3u,
                          layout->title_y + layout->scale,
                          (layout->cx1 - layout->cx0) * 3u / 4u,
                          colors->gold_hi, "Renderer Overhaul - Settings");
    if (runtime->version != NULL) {
        uint32_t width;

        snprintf(text, sizeof text, "v%s", runtime->version);
        width = hwr_overlay_draw_measure(api, text);
        hwr_overlay_draw_text(api, &canvas,
                              hwr_overlay_sub(layout->cx1, width + 4u),
                              layout->title_y + layout->scale, width + 2u,
                              colors->gold_lo, text);
    }
    hwr_overlay_draw_hline(&canvas, layout->cx0,
                           layout->title_y + layout->row_h,
                           layout->cx1 - layout->cx0, colors->gold_mid);

    /* Section strip. */
    if (layout->compact) {
        snprintf(text, sizeof text, "< %s >", HWR_SECTION_LABELS[g_section]);
        hwr_overlay_draw_fill(&canvas, layout->cx0, layout->strip_y,
                              layout->cx1, layout->strip_y + layout->row_h,
                              g_zone == HWR_ZONE_SECTIONS ? colors->gold_lo
                                                          : colors->panel);
        hwr_overlay_draw_text(api, &canvas, layout->cx0 + 3u,
                              layout->strip_y + layout->scale,
                              layout->cx1 - layout->cx0 - 6u, colors->gold_hi,
                              text);
    } else {
        for (i = 0; i < HWR_SECTION_COUNT; i++) {
            uint32_t x = layout->tab_x + (uint32_t)i * layout->tab_w;
            int active = i == g_section;
            int focused = g_zone == HWR_ZONE_SECTIONS && active;
            int hovered = g_hover_kind == HWR_HIT_SECTION && g_hover_index == i;

            hwr_overlay_draw_fill(&canvas, x, layout->strip_y,
                                  x + layout->tab_w - 2u,
                                  layout->strip_y + layout->row_h,
                                  active || hovered ? colors->gold_lo
                                                    : colors->panel);
            if (focused)
                hwr_overlay_draw_border(&canvas, x, layout->strip_y,
                                        x + layout->tab_w - 2u,
                                        layout->strip_y + layout->row_h,
                                        colors->gold_hi);
            hwr_overlay_draw_text(api, &canvas, x + 3u,
                                  layout->strip_y + layout->scale,
                                  layout->tab_w - 8u,
                                  active ? colors->gold_hi : colors->text,
                                  HWR_SECTION_LABELS[i]);
        }
    }

    /* Rows. */
    drawn = 0;
    y = layout->rows_y0;
    for (i = g_scroll; i < g_row_count && drawn < layout->visible; i++) {
        hwr_overlay_draw_row(api, &canvas, layout, colors, i, y);
        y += layout->row_h;
        drawn++;
    }
    if (g_row_count == 0) {
        hwr_overlay_draw_fill(&canvas, layout->cx0, layout->rows_y0,
                              layout->content_x1,
                              layout->rows_y0 + layout->row_h, colors->panel);
        hwr_overlay_draw_text(api, &canvas, layout->cx0 + 3u,
                              layout->rows_y0 + layout->scale,
                              layout->content_x1 - layout->cx0 - 6u,
                              colors->gold_lo,
                              HWR_SECTION_EMPTY[g_section]);
    }
    if (layout->arrows) {
        hwr_overlay_draw_fill(&canvas, layout->arrow_x - 2u, layout->rows_y0,
                              layout->cx1, layout->rows_y1, colors->panel);
        hwr_overlay_draw_arrow(api, &canvas, layout->arrow_x,
                               layout->arrow_up_y, layout->arrow_w,
                               layout->arrow_h, 1, g_scroll > 0, colors);
        hwr_overlay_draw_arrow(api, &canvas, layout->arrow_x,
                               layout->arrow_dn_y, layout->arrow_w,
                               layout->arrow_h, 0,
                               g_scroll + layout->visible < g_row_count,
                               colors);
    }

    /* Help for whatever has focus. */
    if (layout->help_y < layout->status_y) {
        const struct hwr_setting_desc *desc =
            g_zone == HWR_ZONE_ROWS ? hwr_overlay_row_desc(g_focus) : NULL;
        const char *help = desc != NULL && desc->help != NULL ? desc->help : "";
        uint32_t width = layout->cx1 - layout->cx0 - 8u;
        int line;

        hwr_overlay_draw_fill(&canvas, layout->cx0, layout->help_y,
                              layout->cx1, layout->status_y, colors->panel);
        for (line = 0; line < 2; line++) {
            char wrapped[160];

            help = hwr_overlay_wrap(api, help, width, wrapped, sizeof wrapped);
            if (wrapped[0] == 0)
                break;
            hwr_overlay_draw_text(api, &canvas, layout->cx0 + 3u,
                                  layout->help_y +
                                      (uint32_t)line * layout->row_h +
                                      layout->scale,
                                  width, colors->text, wrapped);
        }
    }

    /* Status, or the question a display change is waiting on. */
    hwr_overlay_draw_fill(&canvas, layout->cx0, layout->status_y, layout->cx1,
                          layout->status_y + layout->row_h,
                          g_confirm_pending ? colors->gold_lo : colors->panel);
    if (g_confirm_pending) {
        g_confirm_drawn_seconds = hwr_overlay_confirm_seconds();
        snprintf(text, sizeof text,
                 "Keep this display change? Enter or a click keeps it, Esc "
                 "puts it back. %d", g_confirm_drawn_seconds);
    } else {
        snprintf(text, sizeof text, "%s", g_status);
    }
    hwr_overlay_draw_text(api, &canvas, layout->cx0 + 3u,
                          layout->status_y + layout->scale,
                          layout->cx1 - layout->cx0 - 6u,
                          g_status_error || g_confirm_pending ? colors->gold_hi
                                                              : colors->text,
                          text);

    /* Actions. */
    for (i = 0; i < HWR_ACTION_COUNT; i++) {
        uint32_t x = layout->action_x +
                     (uint32_t)(i % layout->action_columns) * layout->action_w;
        uint32_t y0 = layout->actions_y +
                      (uint32_t)(i / layout->action_columns) * layout->row_h;
        int focused = g_zone == HWR_ZONE_ACTIONS && g_action == i;
        int hovered = g_hover_kind == HWR_HIT_ACTION && g_hover_index == i;

        hwr_overlay_draw_fill(&canvas, x, y0, x + layout->action_w - 2u,
                              y0 + layout->row_h,
                              hovered ? colors->gold_lo : colors->panel);
        if (focused)
            hwr_overlay_draw_border(&canvas, x, y0, x + layout->action_w - 2u,
                                    y0 + layout->row_h, colors->gold_hi);
        hwr_overlay_draw_text(api, &canvas, x + 4u, y0 + layout->scale,
                              layout->action_w - 10u,
                              focused ? colors->gold_hi : colors->text,
                              HWR_ACTION_LABELS[i]);
    }

    /* The panel is opaque and token-free from here on, which is what lets the
     * frame it is composed onto be closed as a whole rect. */
    hwr_overlay_draw_finish(&canvas, layout->x0, layout->y0, layout->x1,
                            layout->y1, colors->panel);
    hwr_overlay_draw_publish(layout->x0, layout->y0, layout->x1, layout->y1);
    g_panel_x = layout->x0;
    g_panel_y = layout->y0;
    g_panel_w = layout->x1 - layout->x0;
    g_panel_h = layout->y1 - layout->y0;
    g_drawn_store = *hwr_settings_get();
    g_redraw = 0;
}

/* ------------------------------ focus movement ---------------------------- */

static int hwr_overlay_first_selectable(int from, int direction)
{
    int i = from;

    while (i >= 0 && i < g_row_count) {
        if (g_rows[i].selectable)
            return i;
        i += direction;
    }
    return -1;
}

static void hwr_overlay_show_row(const struct hwr_overlay_layout *layout)
{
    if (g_focus < g_scroll)
        g_scroll = g_focus;
    if (layout->visible > 0 && g_focus >= g_scroll + layout->visible)
        g_scroll = g_focus - layout->visible + 1;
    if (g_scroll > g_row_count - layout->visible)
        g_scroll = g_row_count - layout->visible;
    if (g_scroll < 0)
        g_scroll = 0;
}

static void hwr_overlay_set_section(int section)
{
    if (section < 0)
        section = HWR_SECTION_COUNT - 1;
    if (section >= HWR_SECTION_COUNT)
        section = 0;
    if (section == g_section)
        return;
    g_section = section;
    g_scroll = 0;
    hwr_overlay_build_rows();
    g_focus = hwr_overlay_first_selectable(0, 1);
    if (g_focus < 0)
        g_focus = 0;
    g_redraw = 1;
}

static void hwr_overlay_move_focus(const struct hwr_overlay_layout *layout,
                                   int direction)
{
    int next;

    if (g_zone == HWR_ZONE_SECTIONS) {
        if (direction > 0)
            g_zone = HWR_ZONE_ROWS;
        else
            g_zone = HWR_ZONE_ACTIONS;
        g_redraw = 1;
        return;
    }
    if (g_zone == HWR_ZONE_ACTIONS) {
        if (direction < 0)
            g_zone = HWR_ZONE_ROWS;
        else
            g_zone = HWR_ZONE_SECTIONS;
        g_redraw = 1;
        return;
    }
    next = hwr_overlay_first_selectable(g_focus + direction, direction);
    if (next < 0) {
        g_zone = direction > 0 ? HWR_ZONE_ACTIONS : HWR_ZONE_SECTIONS;
        g_redraw = 1;
        return;
    }
    g_focus = next;
    hwr_overlay_show_row(layout);
    g_redraw = 1;
}

static void hwr_overlay_activate(int row)
{
    switch (g_rows[row].widget) {
    case HWR_W_TOGGLE:
        hwr_overlay_adjust(row, 1, 0, 0);
        return;
    case HWR_W_NUMBER:
    case HWR_W_RESOLUTION:
    case HWR_W_COLOR:
        hwr_overlay_begin_edit(row);
        return;
    case HWR_W_KEY:
        g_edit = HWR_EDIT_KEY;
        g_edit_row = row;
        g_capture_armed = 1;
        hwr_overlay_status("Press the key to use. Esc leaves it as it is.", 0);
        return;
    case HWR_W_CYCLER:
        hwr_overlay_adjust(row, 1, 0, 0);
        return;
    default:
        return;
    }
}

static void hwr_overlay_key(const struct hwr_overlay_layout *layout,
                            uint8_t entry)
{
    uint8_t make = (uint8_t)(entry & 0x7fu);
    int shifted = (entry & HWR_RING_SHIFT) != 0;

    if (g_edit == HWR_EDIT_KEY) {
        hwr_overlay_edit_key(make);
        return;
    }
    if (g_edit == HWR_EDIT_TEXT) {
        char character;

        if (make == HWR_SC_ESC) {
            hwr_overlay_status("Left as it was.", 0);
            hwr_overlay_end_edit();
            return;
        }
        if (make == HWR_SC_ENTER) {
            hwr_overlay_commit_edit();
            return;
        }
        if (make == HWR_SC_BACKSPACE) {
            if (g_edit_length > 0)
                g_edit_text[--g_edit_length] = 0;
            g_redraw = 1;
            return;
        }
        character = hwr_overlay_edit_char(make);
        if (character != 0 && g_edit_length < sizeof g_edit_text - 1u) {
            g_edit_text[g_edit_length++] = character;
            g_edit_text[g_edit_length] = 0;
            g_redraw = 1;
        }
        return;
    }
    /* A question the panel has asked is answered before anything else it could
     * be asked to do. */
    if (g_guard) {
        if (make == HWR_SC_ENTER) {
            g_close_now = 1;
        } else if (make == HWR_SC_ESC) {
            hwr_overlay_end_guard();
            hwr_overlay_status("Still here. Apply puts the changes in force.",
                               0);
        }
        return;
    }
    if (g_confirm_pending) {
        if (make == HWR_SC_ENTER) {
            hwr_overlay_confirm_keep();
            return;
        }
        if (make == HWR_SC_ESC) {
            hwr_overlay_confirm_revert("The earlier display settings are "
                                       "back.");
            return;
        }
    }
    switch (make) {
    case HWR_SC_TAB:
        g_zone = shifted ? (g_zone + HWR_ZONE_COUNT - 1) % HWR_ZONE_COUNT
                         : (g_zone + 1) % HWR_ZONE_COUNT;
        g_redraw = 1;
        return;
    case HWR_SC_UP:
        hwr_overlay_move_focus(layout, -1);
        return;
    case HWR_SC_DOWN:
        hwr_overlay_move_focus(layout, 1);
        return;
    case HWR_SC_LEFT:
    case HWR_SC_RIGHT: {
        int direction = make == HWR_SC_RIGHT ? 1 : -1;

        if (g_zone == HWR_ZONE_SECTIONS)
            hwr_overlay_set_section(g_section + direction);
        else if (g_zone == HWR_ZONE_ACTIONS)
            g_action = (g_action + HWR_ACTION_COUNT + direction) %
                       HWR_ACTION_COUNT;
        else
            hwr_overlay_adjust(g_focus, direction, 0, 0);
        g_redraw = 1;
        return;
    }
    case HWR_SC_PGUP:
    case HWR_SC_PGDN: {
        int direction = make == HWR_SC_PGDN ? 1 : -1;

        if (g_zone == HWR_ZONE_ROWS && g_focus < g_row_count &&
            g_rows[g_focus].widget == HWR_W_NUMBER) {
            hwr_overlay_adjust(g_focus, direction, 1, 0);
            return;
        }
        g_scroll += direction * (layout->visible > 0 ? layout->visible : 1);
        if (g_scroll > g_row_count - layout->visible)
            g_scroll = g_row_count - layout->visible;
        if (g_scroll < 0)
            g_scroll = 0;
        g_redraw = 1;
        return;
    }
    case HWR_SC_HOME:
    case HWR_SC_END: {
        int extreme = make == HWR_SC_END ? 1 : -1;

        if (g_zone == HWR_ZONE_ROWS && g_focus < g_row_count &&
            g_rows[g_focus].widget == HWR_W_NUMBER) {
            hwr_overlay_adjust(g_focus, 0, 0, extreme);
            return;
        }
        if (g_zone == HWR_ZONE_ROWS) {
            int next = extreme > 0
                     ? hwr_overlay_first_selectable(g_row_count - 1, -1)
                     : hwr_overlay_first_selectable(0, 1);

            if (next >= 0) {
                g_focus = next;
                hwr_overlay_show_row(layout);
            }
        }
        g_redraw = 1;
        return;
    }
    case HWR_SC_ENTER:
        if (g_zone == HWR_ZONE_ACTIONS)
            hwr_overlay_run_action(g_action);
        else if (g_zone == HWR_ZONE_SECTIONS)
            g_zone = HWR_ZONE_ROWS;
        else if (g_focus >= 0 && g_focus < g_row_count &&
                 g_rows[g_focus].selectable)
            hwr_overlay_activate(g_focus);
        g_redraw = 1;
        return;
    default:
        return;
    }
}

/* -------------------------------- hit testing ----------------------------- */

static int hwr_overlay_hit(const struct hwr_overlay_layout *layout, int32_t x,
                           int32_t y, int *index)
{
    uint32_t px;
    uint32_t py;
    int i;

    *index = -1;
    if (x < 0 || y < 0)
        return HWR_HIT_NONE;
    px = (uint32_t)x;
    py = (uint32_t)y;
    if (px < layout->x0 || px >= layout->x1 || py < layout->y0 ||
        py >= layout->y1)
        return HWR_HIT_NONE;
    if (py >= layout->strip_y && py < layout->strip_y + layout->row_h &&
        px >= layout->cx0 && px < layout->cx1) {
        if (layout->compact) {
            *index = px < (layout->cx0 + layout->cx1) / 2u ? -1 : 1;
            return HWR_HIT_SECTION;
        }
        i = (int)((px - layout->tab_x) / layout->tab_w);
        if (i >= 0 && i < HWR_SECTION_COUNT) {
            *index = i;
            return HWR_HIT_SECTION;
        }
        return HWR_HIT_NONE;
    }
    if (layout->arrows && px >= layout->arrow_x - 2u && px < layout->cx1 &&
        py >= layout->rows_y0 && py < layout->rows_y1) {
        if (py < layout->arrow_up_y + layout->arrow_h)
            return HWR_HIT_SCROLL_UP;
        if (py >= layout->arrow_dn_y)
            return HWR_HIT_SCROLL_DOWN;
        return HWR_HIT_NONE;
    }
    if (py >= layout->rows_y0 && py < layout->rows_y1 && px >= layout->cx0 &&
        px < layout->content_x1 && layout->row_h != 0) {
        i = g_scroll + (int)((py - layout->rows_y0) / layout->row_h);
        if (i >= 0 && i < g_row_count) {
            *index = i;
            if (!g_rows[i].selectable ||
                !hwr_overlay_widget_steps(g_rows[i].widget))
                return HWR_HIT_ROW;
            if (px >= layout->value_x && px < layout->value_x + 12u)
                return HWR_HIT_DECREASE;
            if (px + 12u >= layout->content_x1)
                return HWR_HIT_INCREASE;
            return HWR_HIT_ROW;
        }
        return HWR_HIT_NONE;
    }
    /* While a display change waits for an answer, the line that asks for it is
     * the thing to click. */
    if (g_confirm_pending && py >= layout->status_y &&
        py < layout->status_y + layout->row_h && px >= layout->cx0 &&
        px < layout->cx1)
        return HWR_HIT_CONFIRM;
    if (py >= layout->actions_y &&
        py < layout->actions_y +
                 (uint32_t)layout->action_lines * layout->row_h &&
        px >= layout->action_x && layout->action_w != 0 &&
        layout->row_h != 0 && layout->action_columns > 0) {
        int column = (int)((px - layout->action_x) / layout->action_w);
        int line = (int)((py - layout->actions_y) / layout->row_h);

        i = line * layout->action_columns + column;
        if (column < layout->action_columns && i >= 0 && i < HWR_ACTION_COUNT) {
            *index = i;
            return HWR_HIT_ACTION;
        }
    }
    return HWR_HIT_NONE;
}

static void hwr_overlay_press(const struct hwr_overlay_layout *layout, int kind,
                              int index, int secondary)
{
    /* Nothing but the question is live while the panel is asking one. */
    if (g_guard)
        return;
    if (g_confirm_pending && kind == HWR_HIT_CONFIRM) {
        hwr_overlay_confirm_keep();
        return;
    }
    /* An editor is open until something says otherwise, and a click somewhere
     * else says it: the same answer Esc gives, since the click was not the
     * Enter that accepts. It is the closing of the editor and the doing of what
     * was clicked, both, because a click that only closed something would be a
     * click the player has to make twice. The row an editor is anchored to is
     * an index into the rows of the section it was opened in, so leaving it
     * open across a click that changes them would leave it on a stranger. */
    if (g_edit != HWR_EDIT_NONE && kind != HWR_HIT_NONE) {
        hwr_overlay_end_edit();
        hwr_overlay_status("Left as it was.", 0);
    }
    switch (kind) {
    case HWR_HIT_SECTION:
        g_zone = HWR_ZONE_SECTIONS;
        if (layout->compact)
            hwr_overlay_set_section(g_section + index);
        else
            hwr_overlay_set_section(index);
        return;
    case HWR_HIT_ROW:
        g_zone = HWR_ZONE_ROWS;
        g_focus = index;
        g_redraw = 1;
        if (!g_rows[index].selectable)
            return;
        if (g_rows[index].widget == HWR_W_TOGGLE ||
            g_rows[index].widget == HWR_W_CYCLER)
            hwr_overlay_adjust(index, secondary ? -1 : 1, 0, 0);
        else if (!secondary)
            hwr_overlay_activate(index);
        return;
    case HWR_HIT_DECREASE:
    case HWR_HIT_INCREASE:
        g_zone = HWR_ZONE_ROWS;
        g_focus = index;
        hwr_overlay_adjust(index, kind == HWR_HIT_INCREASE ? 1 : -1, 0, 0);
        return;
    case HWR_HIT_ACTION:
        g_zone = HWR_ZONE_ACTIONS;
        g_action = index;
        hwr_overlay_run_action(index);
        return;
    case HWR_HIT_SCROLL_UP:
    case HWR_HIT_SCROLL_DOWN:
        g_scroll += kind == HWR_HIT_SCROLL_DOWN ? 1 : -1;
        if (g_scroll > g_row_count - layout->visible)
            g_scroll = g_row_count - layout->visible;
        if (g_scroll < 0)
            g_scroll = 0;
        g_redraw = 1;
        return;
    default:
        return;
    }
}

/* --------------------------------- the pump ------------------------------- */

static void hwr_overlay_close(void)
{
    /* A display change is only kept by somebody saying so. The panel coming
     * down -- for any reason, including a cutscene taking the screen -- is the
     * end of the chance to say it. */
    if (g_confirm_pending)
        hwr_overlay_confirm_revert("The earlier display settings are back.");
    g_open = 0;
    g_close_request = 0;
    g_close_now = 0;
    g_active = 0;
    g_guard = 0;
    g_capture_armed = 0;
    g_edit = HWR_EDIT_NONE;
    hwr_overlay_update_modal();
    g_hold_kind = HWR_HIT_NONE;
    g_hover_kind = HWR_HIT_NONE;
    g_buttons = 0;
    g_buttons_prev = 0;
    g_panel_w = 0;
    g_panel_h = 0;
    /* The picture goes back to the settings in force with the panel: a preview
     * is what the draft looks like, and there is no draft once the panel is
     * down. */
    hwr_truecolor_preview_clear();
    hwr_overlay_ring_reset();
    hwr_overlay_draw_set_cursor(0, 0, 0);
    hwr_overlay_draw_withdraw();
}

static void hwr_overlay_open(void)
{
    struct hwr_config_outcome outcome;

    g_active = 1;
    g_zone = HWR_ZONE_ROWS;
    g_section = 0;
    g_scroll = 0;
    g_action = 0;
    g_edit = HWR_EDIT_NONE;
    g_edit_row = -1;
    g_guard = 0;
    g_close_now = 0;
    g_await_reload = 0;
    g_await_status[0] = 0;
    /* Whatever the file last did, it did before the panel was opened: it is
     * taken as read rather than announced. */
    hwr_config_last_outcome(&outcome);
    g_outcome_serial = outcome.serial;
    hwr_overlay_update_modal();
    g_hover_kind = HWR_HIT_NONE;
    g_hold_kind = HWR_HIT_NONE;
    g_buttons = 0;
    g_buttons_prev = 0;
    hwr_overlay_ring_reset();
    hwr_overlay_reset_draft();
    hwr_overlay_build_rows();
    g_focus = hwr_overlay_first_selectable(0, 1);
    if (g_focus < 0)
        g_focus = 0;
    g_cursor_fx = (double)g_frame_w / 2.0;
    g_cursor_fy = (double)g_frame_h / 2.0;
    g_cursor_x = (int32_t)g_cursor_fx;
    g_cursor_y = (int32_t)g_cursor_fy;
    hwr_overlay_status("Arrow keys adjust. Tab moves between the sections, the "
                       "settings, and the buttons.", 0);
    g_redraw = 1;
}

static int hwr_overlay_read_frame(const struct roth_api_v1 *api)
{
    uint32_t width;
    uint32_t height;

    if (api == NULL || api->game_ram == NULL)
        return 0;
    width = api->game_ram->u32(VA_G_SCREEN_PITCH);
    height = api->game_ram->u32(VA_G_SCREEN_HEIGHT);
    if (width < 64u || height < 64u ||
        (uint64_t)width * height > HWR_OVERLAY_MAX_PIXELS)
        return 0;
    if (width != g_frame_w || height != g_frame_h) {
        g_frame_w = width;
        g_frame_h = height;
        g_redraw = 1;
    }
    return 1;
}

void hwr_overlay_pump(const struct roth_api_v1 *api,
                      const struct hwr_overlay_runtime *runtime)
{
    struct hwr_overlay_layout layout;
    uint8_t entry;
    int kind;
    int index;
    int pressed;
    int secondary;
    uint16_t tick;

    if (runtime == NULL)
        return;
    /* What the keystroke callback is allowed to read: the key that opens the
     * panel, and whether this screen is the mod's to draw over. */
    g_toggle_key = hwr_settings_get()->overlay_key;
    g_can_open = runtime->screen_available && g_available;

    /* A display change whose time ran out goes back here, on the thread that
     * may write files, however the panel stands. */
    if (g_confirm_expired)
        hwr_overlay_confirm_revert("Nobody kept the display change, so the "
                                   "earlier settings are back.");
    if ((g_close_request || (g_active && !g_open)) && g_active && !g_guard &&
        runtime->screen_available && hwr_overlay_draft_dirty()) {
        /* Changes nobody applied are worth one question before they are lost.
         * The key that opens the panel asked once; asking again closes. */
        hwr_overlay_begin_guard();
    } else if (g_close_request || (g_active && !g_open) ||
               (g_active && !runtime->screen_available)) {
        hwr_overlay_close();
        return;
    }
    if (!g_open) {
        if (g_active)
            hwr_overlay_close();
        return;
    }
    if (!hwr_overlay_read_frame(api)) {
        hwr_overlay_close();
        return;
    }
    if (!g_active)
        hwr_overlay_open();

    hwr_overlay_build_rows();
    if (g_focus >= g_row_count)
        g_focus = g_row_count > 0 ? g_row_count - 1 : 0;
    if (memcmp(&g_drawn_store, hwr_settings_get(), sizeof g_drawn_store) != 0)
        g_redraw = 1;

    hwr_overlay_layout(api, &layout);
    while (hwr_overlay_ring_pop(&entry))
        hwr_overlay_key(&layout, entry);
    if (g_close_now) {
        hwr_overlay_close();
        return;
    }
    if (g_keys_dropped) {
        g_keys_dropped = 0;
        hwr_overlay_status("Some keystrokes were dropped.", 1);
    }
    /* What the settings file last did, once. A reload the panel asked for
     * brings the draft with it; one it only caused does not, because the draft
     * is already what was written. */
    {
        struct hwr_config_outcome outcome;

        hwr_config_last_outcome(&outcome);
        if (outcome.serial != g_outcome_serial) {
            g_outcome_serial = outcome.serial;
            if (!outcome.error && g_await_reload && g_await_status[0] != 0)
                hwr_overlay_status(g_await_status, 0);
            else if (outcome.text[0] != 0)
                hwr_overlay_status(outcome.text, outcome.error);
            if (!outcome.error && g_await_reload)
                hwr_overlay_reset_draft();
            g_await_reload = 0;
            g_await_status[0] = 0;
        }
    }
    if (g_confirm_pending &&
        g_confirm_drawn_seconds != hwr_overlay_confirm_seconds())
        g_redraw = 1;
    /* The rows and the layout are recomputed after the keys, so a section
     * change is drawn from the same state the hit test will use. */
    hwr_overlay_build_rows();
    hwr_overlay_layout(api, &layout);
    if (layout.visible <= 0) {
        /* A frame with no room for a readable panel is one the panel does not
         * open over, rather than one where it swallows input invisibly. */
        hwr_overlay_close();
        return;
    }
    if (!g_cursor_owned && runtime->cursor_valid) {
        g_cursor_x = runtime->cursor_x;
        g_cursor_y = runtime->cursor_y;
    }
    if (g_row_count > layout.visible) {
        if (g_scroll > g_row_count - layout.visible)
            g_scroll = g_row_count - layout.visible;
    } else {
        g_scroll = 0;
    }
    if (g_scroll < 0)
        g_scroll = 0;

    kind = hwr_overlay_hit(&layout, g_cursor_x, g_cursor_y, &index);
    if (kind != g_hover_kind || index != g_hover_index) {
        g_hover_kind = kind;
        g_hover_index = index;
        g_redraw = 1;
    }
    pressed = (g_buttons & 1u) != 0 && (g_buttons_prev & 1u) == 0;
    secondary = (g_buttons & 2u) != 0 && (g_buttons_prev & 2u) == 0;
    tick = (uint16_t)(api != NULL && api->game_ram != NULL
                          ? api->game_ram->u16(VA_G_FRAME_TICK_COUNTER) : 0u);
    if (pressed || secondary) {
        hwr_overlay_press(&layout, kind, index, secondary && !pressed);
        g_hold_kind = kind;
        g_hold_index = index;
        g_hold_press_tick = tick;
        g_hold_last_tick = tick;
    } else if ((g_buttons & 1u) != 0 && g_hold_kind != HWR_HIT_NONE &&
               g_hold_kind == kind && g_hold_index == index) {
        /* Hold to repeat, for the controls where holding means "keep going". */
        if (g_hold_kind == HWR_HIT_DECREASE || g_hold_kind == HWR_HIT_INCREASE ||
            g_hold_kind == HWR_HIT_SCROLL_UP ||
            g_hold_kind == HWR_HIT_SCROLL_DOWN) {
            int16_t since_press = (int16_t)(tick - g_hold_press_tick);
            int16_t since_last = (int16_t)(tick - g_hold_last_tick);

            if (since_press >= HWR_HOLD_DELAY && since_last >= HWR_HOLD_REPEAT) {
                hwr_overlay_press(&layout, kind, index, 0);
                g_hold_last_tick = tick;
            }
        }
    } else if ((g_buttons & 1u) == 0) {
        g_hold_kind = HWR_HIT_NONE;
    }
    g_buttons_prev = g_buttons;

    /* The picture dials as the draft has them, after everything this pump did
     * to it, so moving one is seen in the world image while it is being moved.
     * Cancel puts the draft back and the next pump puts the picture back with
     * it; Apply is still what writes the file. */
    hwr_truecolor_preview(&g_draft);
    hwr_overlay_update_modal();
    if (g_redraw) {
        /* The same treatment the keys get: a click that changed the section --
         * or that turned rows on or off -- changed how many rows there are, and
         * the panel is drawn from the layout the next hit test will use rather
         * than from the one the click was tested against. */
        hwr_overlay_build_rows();
        if (g_focus >= g_row_count)
            g_focus = g_row_count > 0 ? g_row_count - 1 : 0;
        hwr_overlay_layout(api, &layout);
        hwr_overlay_render(api, runtime, &layout);
    }
    hwr_overlay_draw_set_cursor(g_cursor_owned, g_cursor_x, g_cursor_y);
}

/* Everything a poll does that is not the pump: read the buttons, move the
 * panel's own pointer, and hide both from the game. No drawing, no engine call,
 * no file -- which is what lets a poll that arrives somewhere the pump cannot
 * run still be taken. A runtime of NULL is a caller with no pointer to offer;
 * the panel then keeps the one it has. */
static void hwr_overlay_mouse_input(const struct roth_api_v1 *api,
                                    struct roth_mouse_poll_v1 *poll,
                                    const struct hwr_overlay_runtime *runtime)
{
    int relative;

    if (poll == NULL || !g_open)
        return;
    relative = api != NULL && api->game_ram != NULL &&
               api->game_ram->u8(VA_G_MOUSE_RELATIVE_MODE) != 0;
    if (poll->ax == 0x0003u) {
        g_buttons = poll->ebx & 3u;
        /* No click reaches the game underneath while the panel is up. */
        poll->ebx &= ~0xffffu;
    } else if (poll->ax == 0x000bu && relative) {
        /* The engine draws no pointer of its own in this mode, so the panel
         * owns one, and the motion that moves it must not also turn the
         * player. */
        int32_t dx = (int32_t)(int16_t)(poll->ecx & 0xffffu);
        int32_t dy = (int32_t)(int16_t)(poll->edx & 0xffffu);

        g_cursor_fx += (double)dx * 0.5;
        g_cursor_fy += (double)dy * 0.5;
        poll->ecx &= ~0xffffu;
        poll->edx &= ~0xffffu;
    }
    if (relative) {
        if (g_cursor_fx < 0.0)
            g_cursor_fx = 0.0;
        if (g_frame_w != 0 && g_cursor_fx > (double)(g_frame_w - 1u))
            g_cursor_fx = (double)(g_frame_w - 1u);
        if (g_cursor_fy < 0.0)
            g_cursor_fy = 0.0;
        if (g_frame_h != 0 && g_cursor_fy > (double)(g_frame_h - 1u))
            g_cursor_fy = (double)(g_frame_h - 1u);
        g_cursor_x = (int32_t)g_cursor_fx;
        g_cursor_y = (int32_t)g_cursor_fy;
        g_cursor_owned = 1;
    } else {
        /* The engine is drawing its own pointer; a second one would be wrong,
         * and the panel follows the engine's instead. */
        if (runtime != NULL && runtime->cursor_valid) {
            g_cursor_x = runtime->cursor_x;
            g_cursor_y = runtime->cursor_y;
            g_cursor_fx = (double)g_cursor_x;
            g_cursor_fy = (double)g_cursor_y;
        }
        g_cursor_owned = 0;
    }
}

void hwr_overlay_mouse(const struct roth_api_v1 *api,
                       struct roth_mouse_poll_v1 *poll,
                       const struct hwr_overlay_runtime *runtime)
{
    if (poll == NULL || runtime == NULL || !g_open)
        return;
    hwr_overlay_mouse_input(api, poll, runtime);
    hwr_overlay_pump(api, runtime);
}

void hwr_overlay_mouse_capture(const struct roth_api_v1 *api,
                               struct roth_mouse_poll_v1 *poll)
{
    /* The buttons are a level, and the press is the edge the next pump takes
     * from it against the level it saw last -- so a poll taken here is a click
     * kept, not a click lost, and one taken here and pumped later is still one
     * press. Hiding it from the game matters just as much: a click the game
     * sees while the panel is up is a shot fired, and it also puts the engine
     * into the mouselook mode the panel reads the pointer from. */
    hwr_overlay_mouse_input(api, poll, NULL);
}

void hwr_overlay_init(void)
{
    memset(g_rows, 0, sizeof g_rows);
    g_row_count = 0;
    g_edit_row = -1;
    g_hover_index = -1;
    g_status[0] = 0;
    g_toggle_key = hwr_settings_get()->overlay_key;
    g_available = hwr_overlay_draw_init() == 0;
    if (!g_available)
        fprintf(stderr,
                "[renderer_overhaul] the settings overlay has no surface of its own; it will not open\n");
}

void hwr_overlay_shutdown(void)
{
    hwr_overlay_close();
    hwr_overlay_draw_shutdown();
}
