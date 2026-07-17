#ifndef HWR_DAS_H
#define HWR_DAS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum hwr_das_class {
    HWR_DAS_CLASS_UNREAD = 0,
    HWR_DAS_CLASS_PLAIN,
    HWR_DAS_CLASS_ANIMATED,
    HWR_DAS_CLASS_PLACEHOLDER_SPACE,
    HWR_DAS_CLASS_PLACEHOLDER_DOLLAR,
    HWR_DAS_CLASS_OTHER,
    HWR_DAS_CLASS_COUNT
};

struct hwr_das_frame {
    uint16_t width;
    uint16_t height;
    uint8_t *pixels;
};

struct hwr_das_entry {
    uint32_t file_offset;
    uint32_t payload_size;
    uint32_t size_prefix;
    uint16_t size_base;
    uint16_t fat_flags;
    uint16_t payload_flags;
    uint16_t width;
    uint16_t height;
    uint8_t captured_first_byte;
    int decode_failed;
    enum hwr_das_class resource_class;
    uint8_t *raw_payload;
    size_t raw_payload_size;
    struct hwr_das_frame *frames;
    size_t frame_count;
};

struct hwr_das_archive {
    FILE *file;
    char *path;
    uint16_t version;
    uint16_t fat_size;
    uint32_t fat_offset;
    uint32_t palette_offset;
    uint32_t flat_color_offset;
    uint32_t file_size;
    struct hwr_das_entry *entries;
    size_t entry_count;
    uint8_t flat_colors[0x1000];
    int has_flat_colors;
    int owns_file;
    char error[192];
};

struct hwr_das_census {
    size_t classes[HWR_DAS_CLASS_COUNT];
    size_t decoded_textures;
    size_t animation_frames;
    size_t failures;
    size_t plain_failures;
};

/* Opens an archive, validates its header and FAT, and retains the file for lazy decoding. */
int hwr_das_open(struct hwr_das_archive *archive, const char *path);

/* Opens an archive from an existing stream. The stream remains owned by the caller. */
int hwr_das_open_file(struct hwr_das_archive *archive, FILE *file,
                      const char *display_name);

/* Frees every decoded payload and closes a path-opened archive. Safe on zeroed objects. */
void hwr_das_close(struct hwr_das_archive *archive);

/* Loads and classifies one FAT entry. Unknown resource kinds retain their raw bytes. */
int hwr_das_decode(struct hwr_das_archive *archive, size_t id);

/* Decodes every entry and returns aggregate class and failure counts. */
void hwr_das_build_census(struct hwr_das_archive *archive,
                          struct hwr_das_census *census);

const struct hwr_das_entry *hwr_das_entry(const struct hwr_das_archive *archive,
                                          size_t id);
const char *hwr_das_class_name(enum hwr_das_class resource_class);
const char *hwr_das_error(const struct hwr_das_archive *archive);

#endif
