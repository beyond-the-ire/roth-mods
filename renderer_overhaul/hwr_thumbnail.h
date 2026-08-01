#ifndef HWR_THUMBNAIL_H
#define HWR_THUMBNAIL_H

#include "hwr_composite.h"
#include "hwr_world.h"
#include "roth_sdk.h"

#include <stdint.h>

/* Reduces the submitted world image into the save's thumbnail. It shares the
 * composition's own quantizer, so the reduction reads the palette cube rather
 * than scanning the palette four times per output texel. */
int hwr_thumbnail_capture(struct hwr_composite *composite,
                          const struct roth_api_v1 *api,
                          const struct roth_world_frame_v1 *frame,
                          const struct hwr_world *world,
                          uint32_t dst, uint32_t src, uint32_t lut);

#endif
