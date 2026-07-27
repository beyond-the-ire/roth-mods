#ifndef HWR_THUMBNAIL_H
#define HWR_THUMBNAIL_H

#include "hwr_world.h"
#include "roth_sdk.h"

#include <stdint.h>

int hwr_thumbnail_capture(const struct roth_api_v1 *api,
                          const struct roth_world_frame_v1 *frame,
                          const struct hwr_world *world,
                          uint32_t dst, uint32_t src, uint32_t lut);

#endif
