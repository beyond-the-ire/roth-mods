#include "hwr_das.h"

#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define HWR_DAS_HEADER_SIZE 0x44u
#define HWR_DAS_MAX_DIMENSION 4096u
#define HWR_DAS_MAX_ENTRIES 0x1600u

/* A delta-compressed animation stores its first frame uncompressed at
 * BASE_IMAGE_OFFSET, in the ordinary texture layout, so its pixels begin after
 * the usual six-byte texture header. The delta streams that follow it start
 * immediately after those pixels. */
#define HWR_DAS_ANIM_BASE_HEADER 6u

/* Offsets stored inside an animated block are relative to the block as the game
 * holds it in memory, where a ten-byte runtime header precedes the bytes that
 * come from the file. Subtracting that bias turns a stored offset into an index
 * into the payload as read. */
#define HWR_DAS_BLOCK_BIAS 0x0au

/* Where each animation kind keeps its frame directory: sub-image animations
 * chain their frame headers from the first, delta animations index their
 * streams through a table of block offsets. */
#define HWR_DAS_ANIM_TABLE 0x10u
#define HWR_DAS_DELTA_TABLE 0x12u

static uint16_t hwr_read_u16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t hwr_read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static int hwr_das_fail(struct hwr_das_archive *archive, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(archive->error, sizeof archive->error, format, args);
    va_end(args);
    return -1;
}

static int hwr_range_fits(uint32_t offset, uint32_t size, uint32_t extent)
{
    return offset <= extent && size <= extent - offset;
}

static int hwr_seek_read(FILE *file, uint32_t offset, void *destination, size_t size)
{
    if (offset > (uint32_t)LONG_MAX || fseek(file, (long)offset, SEEK_SET) != 0)
        return -1;
    return fread(destination, 1, size, file) == size ? 0 : -1;
}

static void hwr_das_free_entry(struct hwr_das_entry *entry)
{
    size_t i;
    for (i = 0; i < entry->frame_count; i++)
        free(entry->frames[i].pixels);
    free(entry->frames);
    free(entry->raw_payload);
    memset(entry, 0, sizeof *entry);
}

/* Decodes one sub-image frame. Every frame covers the whole buffer: the game
 * clears the margin around the frame's dirty rectangle, the gap that follows
 * each decoded row, and the trailing remainder, so a zeroed buffer plus the
 * decoded rows reproduces it byte for byte. Zero doubles as the transparent
 * key, which is why the cleared margin is what punches the sprite's hole. */
static int hwr_das_decode_rle_frame(struct hwr_das_archive *archive,
                                    const uint8_t *payload, size_t payload_size,
                                    size_t header_offset, struct hwr_das_frame *frame)
{
    const uint8_t *header;
    uint32_t pixel_count;
    uint16_t width;
    uint16_t height;
    uint16_t left;
    uint16_t span;
    uint16_t top;
    uint16_t rows;
    size_t source;
    size_t destination;
    uint16_t row;

    if (header_offset > payload_size || payload_size - header_offset < 0x18u)
        return hwr_das_fail(archive, "animation frame header is truncated");
    header = payload + header_offset;
    if (hwr_read_u16(header) != 0x17u)
        return hwr_das_fail(archive, "animation frame has bad magic");
    width = hwr_read_u16(header + 4);
    height = hwr_read_u16(header + 6);
    left = hwr_read_u16(header + 0x10);
    span = hwr_read_u16(header + 0x12);
    top = hwr_read_u16(header + 0x14);
    rows = hwr_read_u16(header + 0x16);
    if (width == 0 || height == 0 || width > HWR_DAS_MAX_DIMENSION ||
        height > HWR_DAS_MAX_DIMENSION)
        return hwr_das_fail(archive, "animation frame dimensions are invalid");
    pixel_count = (uint32_t)width * height;
    frame->pixels = (uint8_t *)calloc(pixel_count, 1);
    if (frame->pixels == NULL)
        return hwr_das_fail(archive, "out of memory decoding animation frame");
    frame->width = width;
    frame->height = height;
    /* A single-row or single-column frame is a marker rather than an image: the
     * game skips its stream and leaves the buffer blank. */
    if (rows == 1 || span == 1)
        return 0;
    if (left > width || span > width - left || top > height || rows > height - top)
        return hwr_das_fail(archive, "animation frame margins exceed its dimensions");

    source = header_offset + 0x18u;
    destination = (size_t)top * width + left;
    for (row = 0; row < rows; row++) {
        /* A run may spill past the end of its row, which simply carries the
         * write cursor into the next one, so the row counts down rather than
         * capping each run. */
        int32_t remaining = (int32_t)span;
        while (remaining > 0) {
            uint8_t opcode;
            uint16_t count;
            uint8_t value;
            if (source >= payload_size)
                return hwr_das_fail(archive, "animation RLE stream is truncated");
            opcode = payload[source++];
            if (opcode < 0xf1u) {
                count = 1;
                value = opcode;
            } else {
                count = (uint16_t)(opcode - 0xf0u);
                if (source >= payload_size)
                    return hwr_das_fail(archive, "animation RLE run is truncated");
                value = payload[source++];
            }
            if ((size_t)count > pixel_count - destination)
                return hwr_das_fail(archive, "animation RLE run overruns its frame");
            memset(frame->pixels + destination, value, count);
            destination += count;
            remaining -= (int32_t)count;
        }
        /* The gap back to the next row's left edge stays blank; the last row has
         * no gap because the trailing margin covers the rest. */
        if (row + 1 < rows) {
            destination += (size_t)width - span;
            if (destination > pixel_count)
                return hwr_das_fail(archive, "animation frame rows overrun its buffer");
        }
    }
    return 0;
}

/* Recovers the number of frames a sub-image animation really holds. The count in
 * the first header is only a claim; the game walks the chain and adopts the
 * number of hops it could take while every header repeated that claim and the
 * next header stayed inside the block, minus the sixteen bytes it keeps in
 * reserve. Blocks routinely claim one frame more than that walk accepts, so the
 * trailing frame is one the game itself never reaches. */
static size_t hwr_das_repair_frame_count(const uint8_t *payload,
                                         size_t payload_size, uint16_t claimed)
{
    size_t offset = HWR_DAS_ANIM_TABLE;
    size_t limit = payload_size - 0x10u;
    size_t count = 0;
    while (count < (size_t)claimed) {
        int32_t advance;
        if (offset > payload_size || payload_size - offset < 0x10u)
            break;
        if (hwr_read_u16(payload + offset + 8) != claimed)
            break;
        advance = (int32_t)hwr_read_u32(payload + offset + 0x0c);
        if (advance < 1)
            break;
        offset += (uint32_t)advance;
        if (offset > limit)
            break;
        count++;
    }
    return count;
}

static int hwr_das_decode_rle_animation(struct hwr_das_archive *archive,
                                        struct hwr_das_entry *entry)
{
    size_t frame_offset = HWR_DAS_ANIM_TABLE;
    size_t frame_count;
    uint16_t claimed_count;
    size_t i;

    if (entry->raw_payload_size < HWR_DAS_ANIM_TABLE + 0x18u)
        return hwr_das_fail(archive, "animation table is truncated");
    claimed_count = hwr_read_u16(entry->raw_payload + frame_offset + 8);
    if (claimed_count == 0)
        return hwr_das_fail(archive, "animation has no frames");
    frame_count = hwr_das_repair_frame_count(entry->raw_payload,
                                             entry->raw_payload_size, claimed_count);
    if (frame_count == 0)
        return hwr_das_fail(archive, "animation frame chain is unusable");

    entry->frames = (struct hwr_das_frame *)calloc(frame_count, sizeof *entry->frames);
    if (entry->frames == NULL)
        return hwr_das_fail(archive, "out of memory allocating animation frames");
    entry->frame_count = frame_count;
    for (i = 0; i < frame_count; i++) {
        uint32_t advance = hwr_read_u32(entry->raw_payload + frame_offset + 0x0c);
        if (hwr_das_decode_rle_frame(archive, entry->raw_payload,
                                     entry->raw_payload_size, frame_offset,
                                     &entry->frames[i]) != 0)
            return -1;
        frame_offset += advance;
    }
    entry->width = entry->frames[0].width;
    entry->height = entry->frames[0].height;
    return 0;
}

/* Replays one delta stream over a frame buffer. The stream is a sequence of
 * runs, literal blocks and skips; a skip leaves the bytes it passes over as the
 * previous frame left them, which is what makes the encoding a delta. Control
 * byte 0x80 introduces a sixteen-bit word whose high byte selects the operation
 * and also terminates the stream. */
static int hwr_das_apply_delta_stream(struct hwr_das_archive *archive,
                                      const uint8_t *payload, size_t payload_size,
                                      size_t source, uint8_t *pixels,
                                      size_t pixel_count)
{
    size_t destination = 0;
    for (;;) {
        uint8_t control;
        uint16_t count;
        uint8_t value;
        if (source >= payload_size)
            return hwr_das_fail(archive, "delta stream is truncated");
        control = payload[source++];
        if (control == 0x00u) {                        /* run of one byte */
            if (payload_size - source < 2u)
                return hwr_das_fail(archive, "delta run is truncated");
            count = payload[source];
            value = payload[source + 1];
            source += 2u;
        } else if (control < 0x80u) {                  /* literal block */
            count = control;
            if ((size_t)count > payload_size - source ||
                (size_t)count > pixel_count - destination)
                return hwr_das_fail(archive, "delta literal overruns the frame");
            memcpy(pixels + destination, payload + source, count);
            destination += count;
            source += count;
            continue;
        } else if (control != 0x80u) {                 /* skip */
            count = (uint16_t)(control - 0x80u);
            if ((size_t)count > pixel_count - destination)
                return hwr_das_fail(archive, "delta skip leaves the frame");
            destination += count;
            continue;
        } else {                                       /* wide operation */
            uint8_t low;
            uint8_t high;
            if (payload_size - source < 2u)
                return hwr_das_fail(archive, "delta stream ends mid-word");
            low = payload[source];
            high = payload[source + 1];
            source += 2u;
            if (high == 0x00u)
                return 0;
            if (high < 0x80u) {                        /* wide skip */
                count = (uint16_t)(low | ((uint16_t)high << 8));
                if ((size_t)count > pixel_count - destination)
                    return hwr_das_fail(archive, "delta skip leaves the frame");
                destination += count;
                continue;
            }
            /* The operation lives in the top two bits, so the remaining six
             * carry the high half of the length either way. */
            count = (uint16_t)(low | ((uint16_t)(high & 0x3fu) << 8));
            if (high < 0xc0u) {                        /* wide literal block */
                if ((size_t)count > payload_size - source ||
                    (size_t)count > pixel_count - destination)
                    return hwr_das_fail(archive, "delta literal overruns the frame");
                memcpy(pixels + destination, payload + source, count);
                destination += count;
                source += count;
                continue;
            }
            if (source >= payload_size)               /* wide run */
                return hwr_das_fail(archive, "delta run is truncated");
            value = payload[source++];
        }
        if ((size_t)count > pixel_count - destination)
            return hwr_das_fail(archive, "delta run overruns the frame");
        memset(pixels + destination, value, count);
        destination += count;
    }
}

/* Decodes a delta-compressed animation. Its first frame is stored whole in the
 * ordinary texture layout; each later frame is the one before it with a delta
 * stream applied. The last stream returns the buffer to the first frame, so the
 * table holds one stream per frame and the final one is not a frame of its own.
 * A table slot of zero means the frame is unchanged from its predecessor. */
static int hwr_das_decode_delta_animation(struct hwr_das_archive *archive,
                                          struct hwr_das_entry *entry)
{
    const uint8_t *block;
    size_t base;
    size_t pixel_count;
    size_t frame_count;
    size_t i;
    uint16_t width;
    uint16_t height;

    frame_count = hwr_read_u16(entry->raw_payload + 0x0cu);
    if (frame_count == 0)
        return hwr_das_fail(archive, "animation has no frames");
    if (HWR_DAS_DELTA_TABLE + 4u * frame_count > entry->raw_payload_size)
        return hwr_das_fail(archive, "animation delta table is out of range");

    base = hwr_read_u16(entry->raw_payload + 0x0au);
    if (base == 0 || base > entry->raw_payload_size ||
        entry->raw_payload_size - base < HWR_DAS_ANIM_BASE_HEADER)
        return hwr_das_fail(archive, "animation base image is out of range");
    block = entry->raw_payload + base;
    width = hwr_read_u16(block + 2);
    height = hwr_read_u16(block + 4);
    if (width == 0 || height == 0 || width > HWR_DAS_MAX_DIMENSION ||
        height > HWR_DAS_MAX_DIMENSION)
        return hwr_das_fail(archive, "animation base image dimensions are invalid");
    pixel_count = (size_t)width * height;
    if (pixel_count >
        entry->raw_payload_size - base - HWR_DAS_ANIM_BASE_HEADER)
        return hwr_das_fail(archive, "animation base image is truncated");

    entry->frames = (struct hwr_das_frame *)calloc(frame_count, sizeof *entry->frames);
    if (entry->frames == NULL)
        return hwr_das_fail(archive, "out of memory allocating animation frames");
    entry->frame_count = frame_count;
    for (i = 0; i < frame_count; i++) {
        entry->frames[i].pixels = (uint8_t *)malloc(pixel_count);
        if (entry->frames[i].pixels == NULL)
            return hwr_das_fail(archive, "out of memory decoding an animation frame");
        entry->frames[i].width = width;
        entry->frames[i].height = height;
        if (i == 0) {
            memcpy(entry->frames[0].pixels, block + HWR_DAS_ANIM_BASE_HEADER,
                   pixel_count);
            continue;
        }
        memcpy(entry->frames[i].pixels, entry->frames[i - 1].pixels, pixel_count);
        {
            uint32_t stream = hwr_read_u32(entry->raw_payload +
                                           HWR_DAS_DELTA_TABLE + 4u * (i - 1u));
            if (stream == 0)
                continue;
            if (stream < HWR_DAS_BLOCK_BIAS ||
                stream - HWR_DAS_BLOCK_BIAS >= entry->raw_payload_size)
                return hwr_das_fail(archive, "animation delta stream is out of range");
            if (hwr_das_apply_delta_stream(archive, entry->raw_payload,
                                           entry->raw_payload_size,
                                           stream - HWR_DAS_BLOCK_BIAS,
                                           entry->frames[i].pixels, pixel_count) != 0)
                return -1;
        }
    }
    entry->width = width;
    entry->height = height;
    return 0;
}

static int hwr_das_finish_open(struct hwr_das_archive *archive, FILE *file,
                               const char *display_name, int owns_file)
{
    uint8_t header[HWR_DAS_HEADER_SIZE];
    uint8_t *fat = NULL;
    long file_size;
    size_t i;

    memset(archive, 0, sizeof *archive);
    if (fseek(file, 0, SEEK_END) != 0 || (file_size = ftell(file)) < 0 ||
        (unsigned long)file_size > UINT32_MAX)
        return hwr_das_fail(archive, "%s: cannot determine file size", display_name);
    archive->file_size = (uint32_t)file_size;
    if (hwr_seek_read(file, 0, header, sizeof header) != 0)
        return hwr_das_fail(archive, "%s: cannot read header", display_name);
    if (memcmp(header, "DASP", 4) != 0 || hwr_read_u16(header + 4) != 5u)
        return hwr_das_fail(archive, "%s: unsupported DAS header", display_name);
    archive->version = hwr_read_u16(header + 4);
    archive->fat_size = hwr_read_u16(header + 6);
    archive->fat_offset = hwr_read_u32(header + 8);
    archive->palette_offset = hwr_read_u32(header + 0x0c);
    archive->flat_color_offset = hwr_read_u32(header + 0x10);
    if (archive->fat_size == 0 || (archive->fat_size & 7u) != 0 ||
        archive->fat_size / 8u > HWR_DAS_MAX_ENTRIES ||
        !hwr_range_fits(archive->fat_offset, archive->fat_size, archive->file_size))
        return hwr_das_fail(archive, "%s: invalid FAT range", display_name);

    archive->entry_count = archive->fat_size / 8u;
    archive->entries = (struct hwr_das_entry *)calloc(
        archive->entry_count, sizeof *archive->entries);
    fat = (uint8_t *)malloc(archive->fat_size);
    if (archive->entries == NULL || fat == NULL) {
        free(fat);
        free(archive->entries);
        archive->entries = NULL;
        archive->entry_count = 0;
        return hwr_das_fail(archive, "%s: out of memory reading FAT", display_name);
    }
    if (hwr_seek_read(file, archive->fat_offset, fat, archive->fat_size) != 0) {
        free(fat);
        free(archive->entries);
        archive->entries = NULL;
        archive->entry_count = 0;
        return hwr_das_fail(archive, "%s: cannot read FAT", display_name);
    }
    for (i = 0; i < archive->entry_count; i++) {
        struct hwr_das_entry *entry = &archive->entries[i];
        const uint8_t *record = fat + i * 8u;
        uint32_t multiplier;
        entry->file_offset = hwr_read_u32(record);
        entry->size_base = hwr_read_u16(record + 4);
        entry->fat_flags = hwr_read_u16(record + 6);
        multiplier = (entry->fat_flags & (0x100u | 0x200u)) != 0 ? 16u : 2u;
        entry->payload_size = (uint32_t)entry->size_base * multiplier;
        if ((entry->fat_flags & 0xffu) == 0x20u)
            entry->resource_class = HWR_DAS_CLASS_PLACEHOLDER_SPACE;
        else if ((entry->fat_flags & 0xffu) == 0x24u)
            entry->resource_class = HWR_DAS_CLASS_PLACEHOLDER_DOLLAR;
    }
    free(fat);

    if (archive->flat_color_offset != 0) {
        if (!hwr_range_fits(archive->flat_color_offset,
                            sizeof archive->flat_colors, archive->file_size) ||
            hwr_seek_read(file, archive->flat_color_offset, archive->flat_colors,
                          sizeof archive->flat_colors) != 0) {
            free(archive->entries);
            archive->entries = NULL;
            archive->entry_count = 0;
            return hwr_das_fail(archive, "%s: invalid flat-color table", display_name);
        }
        archive->has_flat_colors = 1;
    }
    if (display_name != NULL) {
        size_t length = strlen(display_name) + 1u;
        archive->path = (char *)malloc(length);
        if (archive->path == NULL) {
            free(archive->entries);
            archive->entries = NULL;
            archive->entry_count = 0;
            return hwr_das_fail(archive, "%s: out of memory copying path", display_name);
        }
        memcpy(archive->path, display_name, length);
    }
    archive->file = file;
    archive->owns_file = owns_file;
    return 0;
}

int hwr_das_open(struct hwr_das_archive *archive, const char *path)
{
    FILE *file;
    int result;
    if (archive == NULL || path == NULL)
        return -1;
    file = fopen(path, "rb");
    if (file == NULL) {
        memset(archive, 0, sizeof *archive);
        return hwr_das_fail(archive, "%s: cannot open archive", path);
    }
    result = hwr_das_finish_open(archive, file, path, 1);
    if (result != 0)
        fclose(file);
    return result;
}

int hwr_das_open_file(struct hwr_das_archive *archive, FILE *file,
                      const char *display_name)
{
    if (archive == NULL || file == NULL)
        return -1;
    return hwr_das_finish_open(archive, file,
                               display_name != NULL ? display_name : "<stream>", 0);
}

void hwr_das_close(struct hwr_das_archive *archive)
{
    size_t i;
    if (archive == NULL)
        return;
    if (archive->entries != NULL) {
        for (i = 0; i < archive->entry_count; i++)
            hwr_das_free_entry(&archive->entries[i]);
    }
    free(archive->entries);
    free(archive->path);
    if (archive->file != NULL && archive->owns_file)
        fclose(archive->file);
    memset(archive, 0, sizeof *archive);
}

int hwr_das_decode(struct hwr_das_archive *archive, size_t id)
{
    struct hwr_das_entry *entry;
    uint64_t pixel_count;
    if (archive == NULL || id >= archive->entry_count)
        return -1;
    entry = &archive->entries[id];
    if (entry->decode_failed)
        return -1;
    if (entry->resource_class != HWR_DAS_CLASS_UNREAD)
        return 0;
    if (entry->file_offset == 0 || entry->payload_size == 0) {
        entry->resource_class = HWR_DAS_CLASS_OTHER;
        return 0;
    }
    if (!hwr_range_fits(entry->file_offset, entry->payload_size, archive->file_size)) {
        entry->resource_class = HWR_DAS_CLASS_OTHER;
        entry->decode_failed = 1;
        return hwr_das_fail(archive, "entry %u has an invalid payload range", (unsigned)id);
    }
    if ((entry->fat_flags & 8u) != 0) {
        uint8_t prefix[4];
        if (entry->file_offset < 4u ||
            hwr_seek_read(archive->file, entry->file_offset - 4u, prefix, 4) != 0) {
            entry->resource_class = HWR_DAS_CLASS_OTHER;
            entry->decode_failed = 1;
            return hwr_das_fail(archive, "entry %u has an invalid size prefix", (unsigned)id);
        }
        entry->size_prefix = hwr_read_u32(prefix);
    }
    entry->raw_payload = (uint8_t *)malloc(entry->payload_size);
    if (entry->raw_payload == NULL) {
        entry->resource_class = HWR_DAS_CLASS_OTHER;
        entry->decode_failed = 1;
        return hwr_das_fail(archive, "entry %u is too large to allocate", (unsigned)id);
    }
    entry->raw_payload_size = entry->payload_size;
    if (hwr_seek_read(archive->file, entry->file_offset, entry->raw_payload,
                      entry->raw_payload_size) != 0) {
        entry->resource_class = HWR_DAS_CLASS_OTHER;
        entry->decode_failed = 1;
        return hwr_das_fail(archive, "entry %u payload read failed", (unsigned)id);
    }
    if ((entry->fat_flags & 2u) != 0) {
        entry->captured_first_byte = entry->raw_payload[0];
        entry->raw_payload[0] = 0;
    }
    if (entry->raw_payload_size < 6u) {
        entry->resource_class = HWR_DAS_CLASS_OTHER;
        return 0;
    }
    entry->payload_flags = hwr_read_u16(entry->raw_payload);
    /* An odd block type marks a compressed animation. The two compression
     * schemes are told apart by NUM_DELTA_FRAMES: the sentinel means the frames
     * are run-length encoded behind sub-image headers, any other value counts
     * the delta frames that follow an uncompressed base image. */
    if ((entry->payload_flags & 0x100u) != 0 && entry->raw_payload_size >= 0x0eu) {
        int result;
        entry->resource_class = HWR_DAS_CLASS_ANIMATED;
        result = hwr_read_u16(entry->raw_payload + 0x0c) == 0xfffeu
               ? hwr_das_decode_rle_animation(archive, entry)
               : hwr_das_decode_delta_animation(archive, entry);
        entry->decode_failed = result != 0;
        return result;
    }
    if ((entry->payload_flags & 0x40u) != 0) {
        entry->resource_class = HWR_DAS_CLASS_GROUPED;
        return 0;
    }
    entry->width = hwr_read_u16(entry->raw_payload + 2);
    entry->height = hwr_read_u16(entry->raw_payload + 4);
    pixel_count = (uint64_t)entry->width * entry->height;
    /* Dimensions need not be powers of two: many world textures are authored to
     * fit their surface exactly, and the engine's mask-based wrap simply never
     * engages on them. The renderer reproduces the same mask behavior. */
    if (entry->width != 0 && entry->height != 0 &&
        entry->width <= HWR_DAS_MAX_DIMENSION && entry->height <= HWR_DAS_MAX_DIMENSION &&
        pixel_count <= entry->raw_payload_size - 6u) {
        entry->resource_class = HWR_DAS_CLASS_PLAIN;
    } else {
        entry->resource_class = HWR_DAS_CLASS_OTHER;
    }
    return 0;
}

int hwr_das_group_child_offset(const struct hwr_das_entry *entry,
                               uint16_t child_word, size_t *child_offset,
                               int *flip_toggle)
{
    size_t offset;
    if (entry == NULL || child_offset == NULL || flip_toggle == NULL ||
        entry->resource_class != HWR_DAS_CLASS_GROUPED ||
        entry->raw_payload == NULL)
        return -1;
    offset = (size_t)(child_word & 0x7fffu) * 16u;
    if (offset > entry->raw_payload_size ||
        entry->raw_payload_size - offset < 6u)
        return -1;
    *child_offset = offset;
    *flip_toggle = (child_word & 0x8000u) != 0;
    return 0;
}

int hwr_das_group_child_view(const struct hwr_das_entry *entry,
                             size_t child_offset,
                             struct hwr_das_texture_view *view)
{
    const uint8_t *child;
    uint16_t width;
    uint16_t height;
    size_t pixel_count;
    if (entry == NULL || view == NULL ||
        entry->resource_class != HWR_DAS_CLASS_GROUPED ||
        entry->raw_payload == NULL || child_offset > entry->raw_payload_size ||
        entry->raw_payload_size - child_offset < 6u)
        return -1;
    child = entry->raw_payload + child_offset;
    width = hwr_read_u16(child + 2u);
    height = hwr_read_u16(child + 4u);
    if (width == 0 || height == 0 ||
        width > HWR_DAS_MAX_DIMENSION || height > HWR_DAS_MAX_DIMENSION)
        return -1;
    pixel_count = (size_t)width * height;
    if (pixel_count > entry->raw_payload_size - child_offset - 6u)
        return -1;
    view->flags = hwr_read_u16(child);
    view->width = width;
    view->height = height;
    view->pixels = child + 6u;
    view->pixel_count = pixel_count;
    return 0;
}

void hwr_das_release_decoded(struct hwr_das_archive *archive, size_t id)
{
    struct hwr_das_entry *entry;
    int had_decoded_storage;
    size_t i;
    if (archive == NULL || id >= archive->entry_count)
        return;
    entry = &archive->entries[id];
    had_decoded_storage =
        entry->raw_payload != NULL || entry->frames != NULL;
    for (i = 0; i < entry->frame_count; i++)
        free(entry->frames[i].pixels);
    free(entry->frames);
    free(entry->raw_payload);
    entry->frames = NULL;
    entry->frame_count = 0;
    entry->raw_payload = NULL;
    entry->raw_payload_size = 0;
    /* A later scene rebuild must actually reload payload-backed resources.
     * Keeping PLAIN/GROUPED/ANIMATED here made hwr_das_decode's classified
     * fast path return with no pixels, turning every old texture into a
     * one-pixel palette fill after the first live material transition. */
    if (had_decoded_storage && !entry->decode_failed)
        entry->resource_class = HWR_DAS_CLASS_UNREAD;
}

void hwr_das_build_census(struct hwr_das_archive *archive,
                          struct hwr_das_census *census)
{
    size_t i;
    memset(census, 0, sizeof *census);
    for (i = 0; i < archive->entry_count; i++) {
        int result = hwr_das_decode(archive, i);
        enum hwr_das_class resource_class = archive->entries[i].resource_class;
        if (result != 0) {
            census->failures++;
            if (resource_class == HWR_DAS_CLASS_PLAIN)
                census->plain_failures++;
        }
        if ((unsigned)resource_class < HWR_DAS_CLASS_COUNT)
            census->classes[resource_class]++;
        if (resource_class == HWR_DAS_CLASS_PLAIN && result == 0)
            census->decoded_textures++;
        if (resource_class == HWR_DAS_CLASS_ANIMATED && result == 0)
            census->animation_frames += archive->entries[i].frame_count;
    }
}

const struct hwr_das_entry *hwr_das_entry(const struct hwr_das_archive *archive,
                                          size_t id)
{
    return archive != NULL && id < archive->entry_count ? &archive->entries[id] : NULL;
}

const char *hwr_das_class_name(enum hwr_das_class resource_class)
{
    static const char *const names[HWR_DAS_CLASS_COUNT] = {
        "unread", "plain", "grouped", "animated", "space", "dollar", "other"
    };
    return (unsigned)resource_class < HWR_DAS_CLASS_COUNT
         ? names[resource_class] : "invalid";
}

const char *hwr_das_error(const struct hwr_das_archive *archive)
{
    return archive != NULL && archive->error[0] != 0 ? archive->error : "unknown DAS error";
}
