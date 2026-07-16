#ifndef HWR_GL_H
#define HWR_GL_H

#include <stdint.h>

int hwr_gl_init(void);
const uint32_t *hwr_gl_render_frame(uint32_t width, uint32_t height,
                                    uint32_t color);
void hwr_gl_resize(uint32_t width, uint32_t height);
void hwr_gl_shutdown(void);
const char *hwr_gl_last_error(void);

#endif
