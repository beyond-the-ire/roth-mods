#include "debug_draw.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEBUG_SURFACE_MAX_W 640u
#define DEBUG_SURFACE_MAX_H 480u
#define DEBUG_SURFACE_BYTES (DEBUG_SURFACE_MAX_W * DEBUG_SURFACE_MAX_H)
#define DEBUG_LINE_MAX 128
#define DEBUG_MAX_LINES 56
#define DEBUG_LINE_HEIGHT 8u
#define DEBUG_TEXT_COLOR 0x70u
#define DEBUG_SHADOW_COLOR 0x01u
#define DEBUG_BACKGROUND_COLOR 0x01u

struct debug_surface {
    uint8_t *pixels;
    uint8_t *mask;
    uint32_t width;
    uint32_t height;
};

static struct debug_surface debug_surfaces[2];
static uint8_t *debug_surface_storage;
static int32_t debug_published = -1;
static uint32_t debug_readers[2];

static uint8_t debug_glyph_row(char character, unsigned row)
{
    static const uint8_t digits[10][7] = {
        {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
        {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
        {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
        {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14}
    };
    static const uint8_t letters[26][7] = {
        {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
        {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
        {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
        {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17},
        {14,4,4,4,4,4,14}, {7,2,2,2,2,18,12},
        {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
        {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
        {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
        {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
        {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}
    };
    unsigned char ch = (unsigned char)character;

    if (row >= 7)
        return 0;
    if (ch >= 'a' && ch <= 'z')
        ch = (unsigned char)(ch - 'a' + 'A');
    if (ch >= '0' && ch <= '9')
        return digits[ch - '0'][row];
    if (ch >= 'A' && ch <= 'Z')
        return letters[ch - 'A'][row];
    switch (ch) {
    case ' ': return 0;
    case '.': return row == 6 ? 4 : 0;
    case ',': return row == 5 ? 4 : (row == 6 ? 8 : 0);
    case ':': return (row == 2 || row == 5) ? 4 : 0;
    case ';': return row == 2 ? 4 : (row == 5 ? 4 : (row == 6 ? 8 : 0));
    case '-': return row == 3 ? 14 : 0;
    case '_': return row == 6 ? 31 : 0;
    case '/': return (uint8_t)(1u << (4u - (row * 4u / 6u)));
    case '\\': return (uint8_t)(1u << (row * 4u / 6u));
    case '(': return (row == 0 || row == 6) ? 2 : 4;
    case ')': return (row == 0 || row == 6) ? 8 : 4;
    case '[': return (row == 0 || row == 6) ? 14 : 8;
    case ']': return (row == 0 || row == 6) ? 14 : 2;
    case '=': return (row == 2 || row == 4) ? 14 : 0;
    case '+': return row == 3 ? 14 : ((row == 1 || row == 2 || row == 4 || row == 5) ? 4 : 0);
    case '%': return (row == 0 || row == 1) ? 17 :
                     (row == 2 ? 2 : (row == 3 ? 4 :
                     (row == 4 ? 8 : ((row == 5 || row == 6) ? 17 : 0))));
    case '#': return (row == 2 || row == 4) ? 31 :
                     ((row == 1 || row == 3 || row == 5) ? 10 : 0);
    case '?': return (uint8_t[]){14,17,1,2,4,0,4}[row];
    default: return (uint8_t[]){14,17,1,2,4,0,4}[row];
    }
}

static void debug_put_pixel(struct debug_surface *surface,
                            int x, int y, uint8_t color)
{
    size_t offset;

    if (x < 0 || y < 0 ||
        (uint32_t)x >= surface->width || (uint32_t)y >= surface->height)
        return;
    offset = (size_t)(uint32_t)y * surface->width + (uint32_t)x;
    surface->pixels[offset] = color;
    surface->mask[offset] = 1;
}

static void debug_draw_character(struct debug_surface *surface,
                                 int x, int y, char character,
                                 uint8_t color)
{
    unsigned row;
    unsigned column;

    for (row = 0; row < 7; row++) {
        uint8_t bits = debug_glyph_row(character, row);

        for (column = 0; column < 5; column++) {
            if ((bits & (1u << (4u - column))) != 0)
                debug_put_pixel(surface, x + (int)column, y + (int)row, color);
        }
    }
}

static void debug_draw_text(struct debug_surface *surface,
                            int x, int y, const char *text)
{
    while (*text != '\0') {
        debug_draw_character(surface, x + 1, y + 1, *text, DEBUG_SHADOW_COLOR);
        debug_draw_character(surface, x, y, *text, DEBUG_TEXT_COLOR);
        x += 6;
        if ((uint32_t)x + 5 >= surface->width)
            break;
        text++;
    }
}

static void debug_fill(struct debug_surface *surface, uint8_t color)
{
    size_t bytes = (size_t)surface->width * surface->height;

    memset(surface->pixels, color, bytes);
    memset(surface->mask, 1, bytes);
}

static void debug_add_line(char lines[][DEBUG_LINE_MAX], unsigned *count,
                           const char *format, ...)
{
    va_list arguments;

    if (*count >= DEBUG_MAX_LINES)
        return;
    va_start(arguments, format);
    vsnprintf(lines[*count], DEBUG_LINE_MAX, format, arguments);
    va_end(arguments);
    (*count)++;
}

static void debug_format_rate(char *output, size_t size, uint32_t rate_x10)
{
    snprintf(output, size, "%u.%u", rate_x10 / 10u, rate_x10 % 10u);
}

static void debug_format_fat_index(char *output, size_t size, uint16_t index)
{
    if ((int16_t)index < 0)
        snprintf(output, size, "NONE(0x%04X)", index);
    else
        snprintf(output, size, "%u(0x%04X)", index, index);
}

/* THE PICTURE ONE PLACED OBJECT IS DRAWN WITH, in both the numbers the
 * community map editor prints for it.
 *
 * Its object panel shows a PAIR -- texture index and texture source -- which is
 * the record's own word read straight across. Its picking window shows ONE
 * number for the same picture, which is that picture's place in the file it
 * lives in: a map's own file holds its object pictures from 4096 (source 0) and
 * from 4352 (source 1), and the shared file holds its own from 0 (source 2) and
 * from 256 (source 3). A source outside those four names no single number, and
 * then only the pair is printed rather than a number that would be wrong. */
static void debug_format_picture(char *output, size_t size, uint16_t graphic)
{
    unsigned index = (unsigned)(graphic & 0xffu);
    unsigned source = (unsigned)(graphic >> 8);
    unsigned single;

    switch (source) {
    case 0: single = 4096u + index; break;
    case 1: single = 4352u + index; break;
    case 2: single = index; break;
    case 3: single = 256u + index; break;
    default:
        snprintf(output, size, "%u/%u", index, source);
        return;
    }
    snprintf(output, size, "%u (%u/%u)", single, index, source);
}

static const char *debug_window_mode(uint32_t mode)
{
    switch (mode) {
    case ROTH_WINMODE_WINDOWED: return "WINDOWED";
    case ROTH_WINMODE_BORDERLESS_FULLSCREEN: return "BORDERLESS";
    case ROTH_WINMODE_EXCLUSIVE_FULLSCREEN: return "EXCLUSIVE";
    default: return "UNKNOWN";
    }
}

static const char *debug_present_mode(uint32_t mode)
{
    switch (mode) {
    case ROTH_PRESENT_FIFO: return "FIFO";
    case ROTH_PRESENT_IMMEDIATE: return "IMMEDIATE";
    case ROTH_PRESENT_ADAPTIVE: return "ADAPTIVE";
    default: return "UNKNOWN";
    }
}

static void debug_build_compact_lines(
    char lines[][DEBUG_LINE_MAX], unsigned *count,
    const struct debug_config *config,
    const struct debug_metrics_view *metrics,
    const struct debug_state_view *state)
{
    char value[32];

    if (config->show_present_rate) {
        if (metrics->rates_valid) {
            debug_format_rate(value, sizeof(value), metrics->present_rate_x10);
            debug_add_line(lines, count, "PRESENT/s %s  AVG %u.%03ums",
                           value, metrics->average_interval_us / 1000u,
                           metrics->average_interval_us % 1000u);
        } else {
            debug_add_line(lines, count, "PRESENT/s N/A  AVG N/A");
        }
    }
    if (config->show_tick_rate) {
        if (metrics->rates_valid) {
            debug_format_rate(value, sizeof(value), metrics->tick_rate_x10);
            debug_add_line(lines, count, "TICK/s %s", value);
        } else {
            debug_add_line(lines, count, "TICK/s N/A");
        }
    }
    if (config->show_compose_rate) {
        if (metrics->rates_valid) {
            debug_format_rate(value, sizeof(value), metrics->compose_rate_x10);
            debug_add_line(lines, count, "COMPOSE/s %s", value);
        } else {
            debug_add_line(lines, count, "COMPOSE/s N/A");
        }
    }
    if (config->show_game_rate) {
        if (metrics->rates_valid) {
            debug_format_rate(value, sizeof(value), metrics->game_rate_x10);
            debug_add_line(lines, count, "GAME/s %s", value);
        } else {
            debug_add_line(lines, count, "GAME/s N/A");
        }
    }
    if (config->show_frame_intervals) {
        if (metrics->intervals_valid) {
            debug_add_line(lines, count, "PRESENT INTERVAL MIN %u.%03ums",
                           metrics->minimum_interval_us / 1000u,
                           metrics->minimum_interval_us % 1000u);
            debug_add_line(lines, count, "PRESENT INTERVAL MAX %u.%03ums",
                           metrics->maximum_interval_us / 1000u,
                           metrics->maximum_interval_us % 1000u);
        } else {
            debug_add_line(lines, count, "PRESENT INTERVAL MIN/MAX N/A");
        }
    }
    if (config->show_window_state) {
        if (state->window_valid) {
            debug_add_line(lines, count, "WINDOW %s  PRESENT %s",
                           debug_window_mode(state->window.window_mode),
                           debug_present_mode(state->window.present_mode));
            debug_add_line(lines, count, "MIN PRESENT INTERVAL %uus",
                           state->window.min_present_interval_us);
        } else {
            debug_add_line(lines, count, "WINDOW N/A  PRESENT N/A");
        }
    }
}

static void debug_build_inspector_lines(
    char lines[][DEBUG_LINE_MAX], unsigned *count,
    const struct debug_config *config,
    const struct debug_state_view *state,
    int detailed)
{
    unsigned before = *count;
    int sector_group =
        config->show_player_location ||
        (detailed && config->show_sector_details);
    int cursor_group =
        config->show_cursor ||
        (detailed && config->show_face_details);

    debug_add_line(lines, count, "SCENE / CURSOR");
    if (config->show_map) {
        debug_add_line(lines, count, "MAP %s",
                       state->raw_map_valid ? state->raw_map_name : "N/A");
        debug_add_line(lines, count, "DAS %s",
                       state->das_valid ? state->das_name : "N/A");
        if (sector_group || config->show_view || cursor_group)
            debug_add_line(lines, count, "");
    }
    if (config->show_player_location) {
        if (state->player_valid) {
            debug_add_line(lines, count, "PLAYER RAW X %d  Y %d  Z %d",
                           state->player_x, state->player_y, state->player_z);
            if (state->sector_index_valid) {
                debug_add_line(lines, count,
                               "PLAYER SECTOR %u  OFFSET 0x%04X",
                               state->player_sector_index,
                               state->player_sector_offset);
            } else {
                debug_add_line(lines, count,
                               "PLAYER SECTOR N/A  OFFSET 0x%04X",
                               state->player_sector_offset);
            }
        } else {
            debug_add_line(lines, count, "PLAYER RAW N/A");
            debug_add_line(lines, count,
                           "PLAYER SECTOR N/A  OFFSET N/A");
        }
    }
    if (detailed && config->show_sector_details) {
        char bottom[24];
        char top[24];

        if (!config->show_player_location) {
            if (state->sector_index_valid) {
                debug_add_line(lines, count,
                               "PLAYER SECTOR %u  OFFSET 0x%04X",
                               state->player_sector_index,
                               state->player_sector_offset);
            } else {
                debug_add_line(lines, count,
                               "PLAYER SECTOR N/A  OFFSET 0x%04X",
                               state->player_sector_offset);
            }
        }
        if (state->sector_details_valid) {
            debug_add_line(lines, count,
                           "SECTOR ID %d (0x%04X)  GLOW %u",
                           state->sector_id, (uint16_t)state->sector_id,
                           state->sector_glow);
            debug_add_line(lines, count, "HEIGHT FLOOR %d  ROOF %d",
                           state->sector_floor_height,
                           state->sector_roof_height);
            debug_format_fat_index(bottom, sizeof(bottom),
                                   state->sector_texture_bottom);
            debug_format_fat_index(top, sizeof(top),
                                   state->sector_texture_top);
            debug_add_line(lines, count, "SECTOR TEX BOTTOM %s  TOP %s",
                           bottom, top);
            if (state->mid_platform_valid) {
                debug_add_line(lines, count,
                               "MID HEIGHT FLOOR %d  ROOF %d",
                               state->mid_platform_floor_height,
                               state->mid_platform_roof_height);
                debug_format_fat_index(
                    bottom, sizeof(bottom),
                    state->mid_platform_floor_texture);
                debug_format_fat_index(
                    top, sizeof(top),
                    state->mid_platform_roof_texture);
                debug_add_line(lines, count,
                               "MID TEX FLOOR %s  ROOF %s",
                               bottom, top);
            } else {
                debug_add_line(lines, count, "MID PLATFORM NONE");
            }
        } else {
            debug_add_line(lines, count, "SECTOR DETAILS N/A");
        }
    }
    if (sector_group && (config->show_view || cursor_group))
        debug_add_line(lines, count, "");
    if (config->show_view) {
        if (state->player_valid) {
            debug_add_line(lines, count, "YAW ANGLE16 0x%04X",
                           state->player_yaw);
            debug_add_line(lines, count, "PITCH REQUESTED %d  APPLIED %d",
                           state->requested_pitch, state->applied_pitch);
        } else {
            debug_add_line(lines, count, "YAW / PITCH N/A");
        }
        if (state->video_valid) {
            debug_add_line(lines, count, "VIDEO %ux%u  LOGICAL ASPECT %u:%u",
                           state->pixel_w, state->pixel_h,
                           state->aspect_w, state->aspect_h);
        } else {
            debug_add_line(lines, count, "VIDEO / LOGICAL ASPECT N/A");
        }
        if (cursor_group)
            debug_add_line(lines, count, "");
    }
    if (config->show_cursor) {
        char picture[32];

        debug_add_line(lines, count, "CURSOR CLASS RAW %u", state->cursor_type);
        debug_add_line(lines, count, "CURSOR SURFACE KIND RAW %u",
                       state->cursor_surface_kind);
        /* The object's OWN picture, and the entry the cursor sampled, on two
         * lines next to each other: for a 3D object those are different
         * numbers -- the entry below is the material of the mesh face under
         * the cursor, and the picture here is the object itself. */
        if (state->cursor_object_valid) {
            debug_format_picture(picture, sizeof(picture),
                                 state->cursor_object_graphic);
            debug_add_line(lines, count,
                           "CURSOR OBJECT %u  PICTURE %s  ID %u",
                           state->cursor_object_index, picture,
                           state->cursor_object_id);
        } else {
            debug_add_line(lines, count, "CURSOR OBJECT N/A");
        }
        debug_add_line(lines, count, "CURSOR DAS FAT INDEX %u (0x%04X)",
                       state->cursor_das_entry, state->cursor_das_entry);
        debug_add_line(lines, count, "CURSOR TEXEL U %u  V %u",
                       state->cursor_texture_u, state->cursor_texture_v);
        debug_add_line(lines, count, "CURSOR DRAW FLAGS 0x%04X",
                       state->cursor_draw_flags);
        if (detailed && config->show_face_details)
            debug_add_line(lines, count, "");
    }
    if (detailed && config->show_face_details) {
        char bottom[24];
        char middle[24];
        char top[24];

        if (state->cursor_face_index_valid) {
            if (state->cursor_sector_index_valid) {
                debug_add_line(lines, count,
                               "CURSOR FACE %u  SECTOR %u",
                               state->cursor_face_index,
                               state->cursor_sector_index);
            } else {
                debug_add_line(lines, count,
                               "CURSOR FACE %u  SECTOR N/A",
                               state->cursor_face_index);
            }
        } else {
            debug_add_line(lines, count, "CURSOR FACE N/A");
        }
        if (state->cursor_face_textures_valid) {
            debug_format_fat_index(bottom, sizeof(bottom),
                                   state->cursor_face_texture_bottom);
            debug_format_fat_index(middle, sizeof(middle),
                                   state->cursor_face_texture_middle);
            debug_format_fat_index(top, sizeof(top),
                                   state->cursor_face_texture_top);
            debug_add_line(lines, count,
                           "FACE TEX BOTTOM %s  MID %s  TOP %s",
                           bottom, middle, top);
        } else {
            debug_add_line(lines, count,
                           "FACE TEX BOTTOM N/A  MID N/A  TOP N/A");
        }
    }
    if (*count == before + 1)
        debug_add_line(lines, count, "NO INSPECTOR GROUPS ENABLED");
}

int debug_draw_init(void)
{
    size_t total = DEBUG_SURFACE_BYTES * 4u;

    debug_surface_storage = (uint8_t *)calloc(total, 1);
    if (debug_surface_storage == NULL)
        return -1;
    debug_surfaces[0].pixels = debug_surface_storage;
    debug_surfaces[0].mask = debug_surface_storage + DEBUG_SURFACE_BYTES;
    debug_surfaces[1].pixels = debug_surface_storage + DEBUG_SURFACE_BYTES * 2u;
    debug_surfaces[1].mask = debug_surface_storage + DEBUG_SURFACE_BYTES * 3u;
    __atomic_store_n(&debug_published, -1, __ATOMIC_RELEASE);
    __atomic_store_n(&debug_readers[0], 0, __ATOMIC_RELAXED);
    __atomic_store_n(&debug_readers[1], 0, __ATOMIC_RELAXED);
    return 0;
}

void debug_draw_shutdown(void)
{
    __atomic_store_n(&debug_published, -1, __ATOMIC_RELEASE);
    free(debug_surface_storage);
    debug_surface_storage = NULL;
    memset(debug_surfaces, 0, sizeof(debug_surfaces));
}

void debug_draw_render(const struct debug_config *config,
                       const struct debug_metrics_view *metrics,
                       const struct debug_state_view *state,
                       enum debug_layout_mode mode)
{
    char lines[DEBUG_MAX_LINES][DEBUG_LINE_MAX];
    unsigned line_count = 0;
    unsigned i;
    uint32_t canvas_w;
    uint32_t canvas_h;
    uint32_t panel_w;
    uint32_t panel_h;
    int current;
    int back;
    struct debug_surface *surface;

    int performance_visible;
    int inspector_visible;
    int detailed;

    if (mode == DEBUG_LAYOUT_OFF ||
        (unsigned)mode > (unsigned)DEBUG_LAYOUT_ALL) {
        __atomic_store_n(&debug_published, -1, __ATOMIC_RELEASE);
        return;
    }
    performance_visible =
        mode == DEBUG_LAYOUT_PERFORMANCE || mode == DEBUG_LAYOUT_ALL;
    inspector_visible =
        mode == DEBUG_LAYOUT_INSPECTOR ||
        mode == DEBUG_LAYOUT_DETAILED ||
        mode == DEBUG_LAYOUT_ALL;
    detailed =
        mode == DEBUG_LAYOUT_DETAILED || mode == DEBUG_LAYOUT_ALL;

    debug_add_line(lines, &line_count, "DEBUG OVERLAY - %s",
                   debug_layout_mode_name(mode));
    if (performance_visible)
        debug_build_compact_lines(lines, &line_count, config, metrics, state);
    if (inspector_visible)
        debug_build_inspector_lines(lines, &line_count, config, state,
                                    detailed);
    if (line_count == 0) {
        __atomic_store_n(&debug_published, -1, __ATOMIC_RELEASE);
        return;
    }

    canvas_w = state->video_valid ? state->pixel_w : 640u;
    canvas_h = state->video_valid ? state->pixel_h : 480u;
    if (canvas_w < 8)
        canvas_w = 8;
    if (canvas_h < 8)
        canvas_h = 8;
    panel_w = performance_visible && !inspector_visible ? 350u : 620u;
    if (panel_w > canvas_w - 4u)
        panel_w = canvas_w - 4u;
    if (panel_w > DEBUG_SURFACE_MAX_W)
        panel_w = DEBUG_SURFACE_MAX_W;
    panel_h = line_count * DEBUG_LINE_HEIGHT + 6u;
    if (panel_h > canvas_h - 4u)
        panel_h = canvas_h - 4u;
    if (panel_h > DEBUG_SURFACE_MAX_H)
        panel_h = DEBUG_SURFACE_MAX_H;

    current = __atomic_load_n(&debug_published, __ATOMIC_ACQUIRE);
    back = current == 0 ? 1 : 0;
    if (__atomic_load_n(&debug_readers[back], __ATOMIC_ACQUIRE) != 0)
        return;
    surface = &debug_surfaces[back];
    surface->width = panel_w;
    surface->height = panel_h;
    memset(surface->pixels, 0, (size_t)panel_w * panel_h);
    memset(surface->mask, 0, (size_t)panel_w * panel_h);
    if (config->background)
        debug_fill(surface, DEBUG_BACKGROUND_COLOR);
    for (i = 0; i < line_count; i++) {
        uint32_t y = 3u + i * DEBUG_LINE_HEIGHT;

        if (y + 7u > panel_h)
            break;
        debug_draw_text(surface, 3, (int)y, lines[i]);
    }
    __atomic_store_n(&debug_published, back, __ATOMIC_RELEASE);
}

void debug_draw_compose(uint8_t *pixels, uint32_t width, uint32_t height,
                        enum debug_anchor anchor)
{
    int index;
    unsigned attempt;

    if (pixels == NULL)
        return;
    for (attempt = 0; attempt < 2; attempt++) {
        struct debug_surface *surface;
        uint32_t origin_x;
        uint32_t origin_y;
        uint32_t copy_w;
        uint32_t copy_h;
        uint32_t y;

        index = __atomic_load_n(&debug_published, __ATOMIC_ACQUIRE);
        if (index < 0 || index > 1)
            return;
        __atomic_fetch_add(&debug_readers[index], 1, __ATOMIC_ACQUIRE);
        if (index != __atomic_load_n(&debug_published, __ATOMIC_ACQUIRE)) {
            __atomic_fetch_sub(&debug_readers[index], 1, __ATOMIC_RELEASE);
            continue;
        }

        surface = &debug_surfaces[index];
        origin_x = 2;
        origin_y = 2;
        if ((anchor == DEBUG_ANCHOR_TOP_RIGHT ||
             anchor == DEBUG_ANCHOR_BOTTOM_RIGHT) &&
            width > surface->width + 2u)
            origin_x = width - surface->width - 2u;
        if ((anchor == DEBUG_ANCHOR_BOTTOM_LEFT ||
             anchor == DEBUG_ANCHOR_BOTTOM_RIGHT) &&
            height > surface->height + 2u)
            origin_y = height - surface->height - 2u;
        copy_w = surface->width;
        copy_h = surface->height;
        if (origin_x >= width || origin_y >= height) {
            __atomic_fetch_sub(&debug_readers[index], 1, __ATOMIC_RELEASE);
            return;
        }
        if (copy_w > width - origin_x)
            copy_w = width - origin_x;
        if (copy_h > height - origin_y)
            copy_h = height - origin_y;

        for (y = 0; y < copy_h; y++) {
            const uint8_t *source =
                surface->pixels + (size_t)y * surface->width;
            const uint8_t *mask =
                surface->mask + (size_t)y * surface->width;
            uint8_t *destination =
                pixels + (size_t)(origin_y + y) * width + origin_x;
            uint32_t x;

            for (x = 0; x < copy_w; x++) {
                if (mask[x] != 0)
                    destination[x] = source[x];
            }
        }
        __atomic_fetch_sub(&debug_readers[index], 1, __ATOMIC_RELEASE);
        return;
    }
}
