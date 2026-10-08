#ifndef HWR_EXT_H
#define HWR_EXT_H

#include "renderer_overhaul_ext.h"

#include <stddef.h>
#include <stdint.h>

/* The renderer's side of the interface it publishes to other mods
 * (renderer_overhaul_ext.h holds the contract). This module only keeps what the
 * other mods said; the emitter and the GL layer are what act on it. */

/* The table to publish. */
const struct hwr_ext_v1 *hwr_ext_table(void);

/* THE LOOKS OF ONE FRAME.
 *
 * Each mod replaces its own pending set whenever it likes. The world pass
 * latches every pending set once, after the engine's walk has run (which is
 * when a mod riding that walk has just sent this frame's), and the pending
 * sets are emptied by the latch -- so a look is drawn for exactly the frame
 * it was sent for, and a mod that stops sending leaves nothing behind. */
void hwr_ext_latch_looks(void);

/* The latched look for an object record address, or NULL. Valid until the next
 * latch. */
const struct hwr_ext_object_look_v1 *hwr_ext_look_for(uint32_t object);

/* Whether the latched set holds anything at all, so the frame that has none
 * pays nothing for the lookup. */
int hwr_ext_have_looks(void);

/* The block modifier a size step turns a frame's own into, by the engine's own
 * scale law (bit 0x80 alone = half, bit 0x80 with 0x6000 = 2^t). Answers the
 * flags unchanged where the frame cannot take the step. */
uint16_t hwr_ext_scaled_flags(uint16_t frame_flags, int size_step);

/* The dim rectangles in force, copied out coherently; returns how many. */
size_t hwr_ext_dims(struct hwr_ext_dim_v1 *out, size_t max);

/* What world_state() answers. Set from the game thread wherever the picture
 * on screen changes hands. */
void hwr_ext_set_world_state(int live, int32_t x, int32_t y,
                             uint32_t w, uint32_t h);

#endif
