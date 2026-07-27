#ifndef HWR_BACKDROP_H
#define HWR_BACKDROP_H

#include <stddef.h>
#include <stdint.h>

/* BACKDROP.RAW is the full-screen chrome image the engine paints over the whole
 * framebuffer whenever it repaints the HUD. The 3D view is drawn on top of it,
 * so wherever the world covers no pixel the backdrop's own art stays visible.
 * Reproducing that means seeding the palette-index render target with the part
 * of the backdrop the world viewport sits on. */

struct hwr_backdrop {
    int valid;
    uint32_t width;
    uint32_t height;
    uint8_t *pixels;      /* palette indices, width * height, top row first */
    char error[256];
};

/* HWR_BACKDROP=0 disables the seed and leaves the flat clear in place. */
int hwr_backdrop_enabled(void);

/* Reads and decodes the image at `path`. Returns 0 on success; on failure the
 * struct is left cleared and hwr_backdrop_error() describes why. */
int hwr_backdrop_load(struct hwr_backdrop *backdrop, const char *path);

void hwr_backdrop_clear(struct hwr_backdrop *backdrop);

const char *hwr_backdrop_error(const struct hwr_backdrop *backdrop);

/* Scales the decoded image to the engine screen, extracts the engine viewport,
 * then nearest-scales that rectangle to the requested output dimensions.
 * Returns 0 on success. Rows the engine would leave untouched are index 0. */
int hwr_backdrop_view_indices(const struct hwr_backdrop *backdrop,
                              uint32_t screen_width, uint32_t screen_height,
                              uint32_t view_x, uint32_t view_y,
                              uint32_t view_width, uint32_t view_height,
                              uint32_t output_width, uint32_t output_height,
                              uint8_t *out);

#endif
