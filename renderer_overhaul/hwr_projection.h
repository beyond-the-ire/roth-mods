#ifndef HWR_PROJECTION_H
#define HWR_PROJECTION_H

#include "hwr_gl.h"

#include <stdint.h>

/* Retargets the engine-staged camera to a different framebuffer while retaining
 * the source projection for engine-space portal windows and intrinsic object
 * sizing. Returns 0 when the dimensions or scaled fields are not representable. */
int hwr_projection_scale_camera(struct hwr_gl_camera *camera,
                                uint32_t source_width,
                                uint32_t source_height,
                                uint32_t target_width,
                                uint32_t target_height,
                                float fov_deg);

double hwr_projection_map_x(const struct hwr_gl_camera *camera,
                            double source_x);
int64_t hwr_projection_map_x_fixed(const struct hwr_gl_camera *camera,
                                   int32_t source_x);

/* Returns the target framebuffer edges expressed in the engine projection's
 * screen-X coordinates. A wider target therefore produces bounds outside the
 * original [0, source_width) interval, which the portal walk can traverse
 * without changing its fixed-point projection law. */
int hwr_projection_source_x_bounds(const struct hwr_gl_camera *camera,
                                   uint32_t target_width,
                                   int *left, int *right);

#endif
