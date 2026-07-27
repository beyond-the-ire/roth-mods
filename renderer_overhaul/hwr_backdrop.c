#include "hwr_backdrop.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* An eight-byte header precedes the pixel stream: a kind tag, then the source
 * width and height as 16-bit values. */
#define HWR_BACKDROP_HEADER 8u
#define HWR_BACKDROP_WIDTH_OFFSET 4u
#define HWR_BACKDROP_HEIGHT_OFFSET 6u

/* Byte values below the run marker stand for themselves; the rest introduce a
 * run whose length is the value above the marker and whose pixel is the byte
 * that follows. That caps a run at fifteen pixels. */
#define HWR_BACKDROP_RUN_BASE 0xf0u
#define HWR_BACKDROP_RUN_MIN 0xf1u

/* Generous but finite: the shipped image is 640x400, and the whole decode is
 * held in memory at one byte per pixel. */
#define HWR_BACKDROP_MAX_DIMENSION 4096u
#define HWR_BACKDROP_MAX_FILE_SIZE (64u * 1024u * 1024u)

/* The scale factors are 16.16 fixed point, and the source-row walk carries a
 * fractional remainder of one whole step between rows. */
#define HWR_BACKDROP_ONE 0x10000
#define HWR_BACKDROP_ROUND 0x7fff

static int hwr_backdrop_fail(struct hwr_backdrop *backdrop, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(backdrop->error, sizeof backdrop->error, format, arguments);
    va_end(arguments);
    free(backdrop->pixels);
    backdrop->pixels = NULL;
    backdrop->width = 0;
    backdrop->height = 0;
    backdrop->valid = 0;
    return -1;
}

int hwr_backdrop_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *setting = getenv("HWR_BACKDROP");
        enabled = setting != NULL && setting[0] == '0' ? 0 : 1;
    }
    return enabled;
}

void hwr_backdrop_clear(struct hwr_backdrop *backdrop)
{
    if (backdrop == NULL)
        return;
    free(backdrop->pixels);
    backdrop->pixels = NULL;
    backdrop->width = 0;
    backdrop->height = 0;
    backdrop->valid = 0;
    backdrop->error[0] = 0;
}

const char *hwr_backdrop_error(const struct hwr_backdrop *backdrop)
{
    if (backdrop == NULL)
        return "no backdrop";
    return backdrop->error[0] != 0 ? backdrop->error : "no error";
}

static int hwr_backdrop_read_file(struct hwr_backdrop *backdrop, const char *path,
                                  uint8_t **bytes, size_t *size)
{
    FILE *file = fopen(path, "rb");
    long length;
    uint8_t *buffer;
    size_t got;

    *bytes = NULL;
    *size = 0;
    if (file == NULL)
        return hwr_backdrop_fail(backdrop, "%s: cannot open", path);
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return hwr_backdrop_fail(backdrop, "%s: cannot size", path);
    }
    if ((unsigned long)length <= HWR_BACKDROP_HEADER ||
        (unsigned long)length > HWR_BACKDROP_MAX_FILE_SIZE) {
        fclose(file);
        return hwr_backdrop_fail(backdrop, "%s: implausible size %ld", path, length);
    }
    buffer = (uint8_t *)malloc((size_t)length);
    if (buffer == NULL) {
        fclose(file);
        return hwr_backdrop_fail(backdrop, "%s: out of memory", path);
    }
    got = fread(buffer, 1, (size_t)length, file);
    fclose(file);
    if (got != (size_t)length) {
        free(buffer);
        return hwr_backdrop_fail(backdrop, "%s: short read", path);
    }
    *bytes = buffer;
    *size = got;
    return 0;
}

static uint16_t hwr_backdrop_read_u16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8));
}

int hwr_backdrop_load(struct hwr_backdrop *backdrop, const char *path)
{
    uint8_t *bytes;
    size_t size;
    size_t cursor;
    size_t written = 0;
    size_t wanted;
    uint32_t width;
    uint32_t height;
    uint8_t *pixels;

    if (backdrop == NULL || path == NULL)
        return -1;
    hwr_backdrop_clear(backdrop);
    if (hwr_backdrop_read_file(backdrop, path, &bytes, &size) != 0)
        return -1;

    width = hwr_backdrop_read_u16(bytes + HWR_BACKDROP_WIDTH_OFFSET);
    height = hwr_backdrop_read_u16(bytes + HWR_BACKDROP_HEIGHT_OFFSET);
    if (width == 0 || height == 0 || width > HWR_BACKDROP_MAX_DIMENSION ||
        height > HWR_BACKDROP_MAX_DIMENSION) {
        free(bytes);
        return hwr_backdrop_fail(backdrop, "%s: unusable dimensions %ux%u", path,
                                 width, height);
    }
    wanted = (size_t)width * height;
    pixels = (uint8_t *)malloc(wanted);
    if (pixels == NULL) {
        free(bytes);
        return hwr_backdrop_fail(backdrop, "%s: out of memory for %ux%u", path,
                                 width, height);
    }

    for (cursor = HWR_BACKDROP_HEADER; cursor < size && written < wanted;) {
        uint8_t token = bytes[cursor++];
        size_t run;
        if (token < HWR_BACKDROP_RUN_MIN) {
            pixels[written++] = token;
            continue;
        }
        if (cursor >= size)
            break;                              /* a run with no pixel byte */
        run = (size_t)(token - HWR_BACKDROP_RUN_BASE);
        if (run > wanted - written)
            run = wanted - written;             /* a run overhanging the image */
        memset(pixels + written, bytes[cursor++], run);
        written += run;
    }
    free(bytes);

    if (written != wanted) {
        free(pixels);
        return hwr_backdrop_fail(backdrop, "%s: decoded %lu of %lu pixels", path,
                                 (unsigned long)written, (unsigned long)wanted);
    }
    backdrop->pixels = pixels;
    backdrop->width = width;
    backdrop->height = height;
    backdrop->valid = 1;
    backdrop->error[0] = 0;
    return 0;
}

/* The horizontal factor is a table of source columns, one per destination
 * column, stepped in 16.16 from a half-step offset so the samples land in the
 * middle of the source pixels they stand for. A source as wide as the screen is
 * copied straight across instead: the stepped table drifts by a column near the
 * right edge, and the engine sidesteps that by building no table at all when the
 * two widths agree. */
static void hwr_backdrop_column_table(uint32_t source_width, uint32_t screen_width,
                                      uint32_t view_x, uint32_t view_width,
                                      uint16_t *table)
{
    uint32_t step;
    uint32_t accumulator;
    uint32_t column;

    if (source_width == screen_width) {
        for (column = 0; column < view_width; column++)
            table[column] = (uint16_t)(view_x + column);
        return;
    }
    step = (((uint32_t)source_width << 16) - HWR_BACKDROP_ROUND) / screen_width;
    accumulator = (step - 1u) >> 1;
    for (column = 0; column < view_x; column++)
        accumulator += step;
    for (column = 0; column < view_width; column++) {
        uint32_t index = accumulator >> 16;
        table[column] = (uint16_t)(index < source_width ? index : source_width - 1u);
        accumulator += step;
    }
}

/* The vertical factor cannot be expressed the same way: the engine walks the
 * source rows in order and decides per row whether to skip it, emit it once, or
 * emit it several times, so the mapping is produced by running that walk. */
static uint32_t hwr_backdrop_row_table(uint32_t source_height, uint32_t screen_height,
                                       uint32_t *table)
{
    uint32_t step = (((uint32_t)source_height << 16) - HWR_BACKDROP_ROUND) / screen_height;
    int32_t accumulator = 0;
    uint32_t row = 0;
    uint32_t source;

    for (source = 0; source < source_height && row < screen_height; source++) {
        for (;;) {
            if (accumulator < 0) {
                accumulator += HWR_BACKDROP_ONE;
                break;                          /* this source row is skipped */
            }
            table[row++] = source;
            if (row >= screen_height)
                break;
            accumulator -= (int32_t)step;
            if (accumulator >= 0)
                continue;                       /* the same source row again */
            accumulator += HWR_BACKDROP_ONE;
            break;
        }
    }
    return row;
}

int hwr_backdrop_view_indices(const struct hwr_backdrop *backdrop,
                              uint32_t screen_width, uint32_t screen_height,
                              uint32_t view_x, uint32_t view_y,
                              uint32_t view_width, uint32_t view_height,
                              uint32_t output_width, uint32_t output_height,
                              uint8_t *out)
{
    uint16_t *columns;
    uint32_t *rows;
    uint8_t *view;
    uint32_t filled;
    uint32_t y;

    if (backdrop == NULL || !backdrop->valid || backdrop->pixels == NULL || out == NULL)
        return -1;
    if (screen_width == 0 || screen_height == 0 ||
        view_width == 0 || view_height == 0 ||
        output_width == 0 || output_height == 0)
        return -1;
    /* Ordered so neither bound is computed from a subtraction that could wrap. */
    if (view_width > screen_width || view_height > screen_height)
        return -1;
    if (view_x > screen_width - view_width || view_y > screen_height - view_height)
        return -1;
    if (screen_width > HWR_BACKDROP_MAX_DIMENSION ||
        screen_height > HWR_BACKDROP_MAX_DIMENSION)
        return -1;

    columns = (uint16_t *)malloc(sizeof *columns * view_width);
    rows = (uint32_t *)malloc(sizeof *rows * screen_height);
    view = (uint8_t *)malloc((size_t)view_width * view_height);
    if (columns == NULL || rows == NULL || view == NULL) {
        free(columns);
        free(rows);
        free(view);
        return -1;
    }
    hwr_backdrop_column_table(backdrop->width, screen_width, view_x, view_width, columns);
    filled = hwr_backdrop_row_table(backdrop->height, screen_height, rows);

    for (y = 0; y < view_height; y++) {
        uint8_t *destination = view + (size_t)y * view_width;
        uint32_t screen_row = view_y + y;
        const uint8_t *source;
        uint32_t x;
        if (screen_row >= filled) {
            memset(destination, 0, view_width);
            continue;
        }
        source = backdrop->pixels + (size_t)rows[screen_row] * backdrop->width;
        for (x = 0; x < view_width; x++)
            destination[x] = source[columns[x]];
    }
    for (y = 0; y < output_height; y++) {
        const uint8_t *source = view +
            (size_t)(((uint64_t)y * view_height) / output_height) *
            view_width;
        uint8_t *destination = out + (size_t)y * output_width;
        uint32_t x;
        for (x = 0; x < output_width; x++)
            destination[x] =
                source[((uint64_t)x * view_width) / output_width];
    }
    free(columns);
    free(rows);
    free(view);
    return 0;
}
