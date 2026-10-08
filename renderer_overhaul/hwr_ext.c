#include "hwr_ext.h"

#include <string.h>

enum {
    HWR_EXT_OWNER_MAX = 4,
    HWR_EXT_LOOKS_PER_OWNER = 64,
    HWR_EXT_LOOK_MAX = HWR_EXT_OWNER_MAX * HWR_EXT_LOOKS_PER_OWNER
};

/* ---- object looks: game thread only ---- */

struct hwr_ext_look_set {
    const void *owner;
    uint32_t count;
    struct hwr_ext_object_look_v1 looks[HWR_EXT_LOOKS_PER_OWNER];
};

static struct hwr_ext_look_set g_pending[HWR_EXT_OWNER_MAX];
static struct hwr_ext_object_look_v1 g_latched[HWR_EXT_LOOK_MAX];
static size_t g_latched_count;

static int32_t ROTH_CDECL hwr_ext_submit_object_looks(
    const void *owner, const struct hwr_ext_object_look_v1 *looks,
    uint32_t count, uint32_t look_size)
{
    struct hwr_ext_look_set *set = NULL;
    uint32_t accepted = 0;
    uint32_t i;
    int k;

    if (owner == NULL || (count != 0 && looks == NULL) ||
        look_size < offsetof(struct hwr_ext_object_look_v1, tint_rgb))
        return 0;
    for (k = 0; k < HWR_EXT_OWNER_MAX; k++) {
        if (g_pending[k].owner == owner) {
            set = &g_pending[k];
            break;
        }
    }
    for (k = 0; set == NULL && k < HWR_EXT_OWNER_MAX; k++) {
        if (g_pending[k].owner == NULL) {
            set = &g_pending[k];
            set->owner = owner;
        }
    }
    if (set == NULL)
        return 0;
    for (i = 0; i < count && accepted < HWR_EXT_LOOKS_PER_OWNER; i++) {
        struct hwr_ext_object_look_v1 look;
        const uint8_t *source = (const uint8_t *)looks + (size_t)i * look_size;

        /* A caller built against an earlier revision sends a shorter record;
         * what it does not know about is nought. */
        memset(&look, 0, sizeof look);
        memcpy(&look, source, look_size < sizeof look ? look_size : sizeof look);
        if (look.object == 0)
            continue;
        set->looks[accepted++] = look;
    }
    set->count = accepted;
    return (int32_t)accepted;
}

void hwr_ext_latch_looks(void)
{
    int k;

    g_latched_count = 0;
    for (k = 0; k < HWR_EXT_OWNER_MAX; k++) {
        struct hwr_ext_look_set *set = &g_pending[k];

        if (set->count != 0)
            memcpy(&g_latched[g_latched_count], set->looks,
                   set->count * sizeof set->looks[0]);
        g_latched_count += set->count;
        set->count = 0;
    }
}

int hwr_ext_have_looks(void)
{
    return g_latched_count != 0;
}

const struct hwr_ext_object_look_v1 *hwr_ext_look_for(uint32_t object)
{
    size_t i;

    if (object == 0)
        return NULL;
    for (i = 0; i < g_latched_count; i++)
        if (g_latched[i].object == object)
            return &g_latched[i];
    return NULL;
}

uint16_t hwr_ext_scaled_flags(uint16_t frame_flags, int size_step)
{
    uint16_t flags = frame_flags;
    int exponent;

    if (size_step == 0 || (flags & 0x8000u) != 0)
        return frame_flags;                 /* a 3D-model record has no scale bits */
    if ((flags & 0x80u) == 0 && (flags & 0x6000u) != 0)
        return frame_flags;                 /* bits this law does not describe */
    exponent = (flags & 0x80u) == 0 ? 0
             : (flags & 0x6000u) != 0 ? (int)((flags >> 13) & 3u) : -1;
    exponent += size_step;
    if (exponent < -1 || exponent > 3)
        return frame_flags;
    flags = (uint16_t)(flags & ~(0x80u | 0x6000u));
    if (exponent != 0)
        flags |= 0x80u;
    if (exponent > 0)
        flags |= (uint16_t)(exponent << 13);
    return flags;
}

/* ---- dims: written from the game thread or the compose tick, read on the
 * game thread ----
 *
 * The tick may interrupt the reader, so each slot carries a sequence the
 * writer advances around its update: a copy taken across a change is seen to
 * be one and is taken again. */

struct hwr_ext_dim_slot {
    volatile uint32_t sequence;
    volatile uint32_t active;
    struct hwr_ext_dim_v1 dim;
};

struct hwr_ext_dim_owner {
    const void *owner;
    struct hwr_ext_dim_slot slots[HWR_EXT_DIM_SLOTS];
};

static struct hwr_ext_dim_owner g_dims[HWR_EXT_OWNER_MAX];

static int32_t ROTH_CDECL hwr_ext_set_world_dim(
    const void *owner, uint32_t slot, const struct hwr_ext_dim_v1 *dim)
{
    struct hwr_ext_dim_owner *row = NULL;
    struct hwr_ext_dim_slot *entry;
    struct hwr_ext_dim_v1 wanted;
    int k;

    if (owner == NULL || slot >= HWR_EXT_DIM_SLOTS)
        return 0;
    for (k = 0; k < HWR_EXT_OWNER_MAX; k++) {
        if (__atomic_load_n(&g_dims[k].owner, __ATOMIC_ACQUIRE) == owner) {
            row = &g_dims[k];
            break;
        }
    }
    for (k = 0; row == NULL && k < HWR_EXT_OWNER_MAX; k++) {
        const void *expected = NULL;

        if (__atomic_compare_exchange_n(&g_dims[k].owner, &expected, owner, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            row = &g_dims[k];
    }
    if (row == NULL)
        return 0;
    entry = &row->slots[slot];
    if (dim == NULL || dim->strength == 0 ||
        (!dim->whole_world && (dim->w == 0 || dim->h == 0))) {
        if (entry->active) {
            __atomic_add_fetch(&entry->sequence, 1u, __ATOMIC_ACQ_REL);
            entry->active = 0;
            __atomic_add_fetch(&entry->sequence, 1u, __ATOMIC_ACQ_REL);
        }
        return 1;
    }
    wanted = *dim;
    if (wanted.strength > 255u)
        wanted.strength = 255u;
    if (wanted.whole_world) {
        wanted.whole_world = 1u;
        wanted.x = 0;
        wanted.y = 0;
        wanted.w = 0;
        wanted.h = 0;
    }
    if (entry->active &&
        memcmp(&entry->dim, &wanted, sizeof entry->dim) == 0)
        return 1;                            /* nothing moved: no churn */
    __atomic_add_fetch(&entry->sequence, 1u, __ATOMIC_ACQ_REL);
    entry->dim = wanted;
    entry->active = 1;
    __atomic_add_fetch(&entry->sequence, 1u, __ATOMIC_ACQ_REL);
    return 1;
}

size_t hwr_ext_dims(struct hwr_ext_dim_v1 *out, size_t max)
{
    size_t count = 0;
    int k;
    unsigned s;

    for (k = 0; k < HWR_EXT_OWNER_MAX; k++) {
        if (g_dims[k].owner == NULL)
            continue;
        for (s = 0; s < HWR_EXT_DIM_SLOTS && count < max; s++) {
            struct hwr_ext_dim_slot *entry = &g_dims[k].slots[s];
            struct hwr_ext_dim_v1 copy;
            uint32_t active = 0;
            int attempt;

            for (attempt = 0; attempt < 4; attempt++) {
                uint32_t before =
                    __atomic_load_n(&entry->sequence, __ATOMIC_ACQUIRE);

                active = entry->active;
                copy = entry->dim;
                if ((before & 1u) == 0 &&
                    __atomic_load_n(&entry->sequence, __ATOMIC_ACQUIRE) == before)
                    break;
                active = 0;                  /* never settled: leave it out */
            }
            if (active)
                out[count++] = copy;
        }
    }
    return count;
}

/* ---- world state: written on the game thread, read from anywhere ----
 *
 * Two copies and an index. The writer fills the copy nobody is told to read
 * and then points the index at it, so a reader always copies a whole state. */

static struct hwr_ext_world_state_v1 g_state[2];
static volatile uint32_t g_state_index;

void hwr_ext_set_world_state(int live, int32_t x, int32_t y,
                             uint32_t w, uint32_t h)
{
    uint32_t current = __atomic_load_n(&g_state_index, __ATOMIC_ACQUIRE) & 1u;
    struct hwr_ext_world_state_v1 next;

    memset(&next, 0, sizeof next);
    next.live = live ? 1u : 0u;
    if (live) {
        next.x = x;
        next.y = y;
        next.w = w;
        next.h = h;
    }
    if (memcmp(&g_state[current], &next, sizeof next) == 0)
        return;
    g_state[current ^ 1u] = next;
    __atomic_store_n(&g_state_index, current ^ 1u, __ATOMIC_RELEASE);
}

static int32_t ROTH_CDECL hwr_ext_world_state(
    struct hwr_ext_world_state_v1 *out)
{
    struct hwr_ext_world_state_v1 state =
        g_state[__atomic_load_n(&g_state_index, __ATOMIC_ACQUIRE) & 1u];

    if (out != NULL)
        *out = state;
    return (int32_t)state.live;
}

static const struct hwr_ext_v1 g_table = {
    .struct_size = sizeof(struct hwr_ext_v1),
    .world_state = hwr_ext_world_state,
    .submit_object_looks = hwr_ext_submit_object_looks,
    .set_world_dim = hwr_ext_set_world_dim,
};

const struct hwr_ext_v1 *hwr_ext_table(void)
{
    return &g_table;
}
