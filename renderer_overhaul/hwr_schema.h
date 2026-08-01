#ifndef HWR_SCHEMA_H
#define HWR_SCHEMA_H

#include "hwr_settings.h"

#include <stddef.h>
#include <stdint.h>

/* The one authoritative description of every configurable option: cfg key,
 * type, default, bounds, category, label/help, and how a changed value reaches
 * the renderer. The parser, the defaults, the writer, and the settings overlay
 * all read this table instead of keeping their own copy of the same rule. */

struct roth_api_v1;

enum hwr_value_type {
    HWR_VT_BOOL = 0,
    HWR_VT_U32,
    HWR_VT_F32,
    HWR_VT_ENUM,
    HWR_VT_COLOR
};

enum hwr_category {
    HWR_CAT_DISPLAY = 0,
    HWR_CAT_VIEW,
    HWR_CAT_RENDERING,
    HWR_CAT_ENHANCEMENTS,
    HWR_CAT_SYSTEM
};

enum hwr_exposure {
    HWR_EXP_CONSUMER = 0, /* parsed, written, shown in the settings overlay */
    HWR_EXP_ADVANCED,     /* parsed, written only when already present */
    HWR_EXP_ALIAS,        /* parsed through a transform onto another row */
    HWR_EXP_RETIRED,      /* parsed, value discarded, never written */
    HWR_EXP_READONLY      /* no cfg key; reported state only */
};

enum hwr_apply_kind {
    HWR_APPLY_HOOK = 0,   /* the store raises change_bit and a hook reacts */
    HWR_APPLY_LIVE,       /* consumers read the store directly as they draw */
    HWR_APPLY_NONE        /* nothing consumes it yet */
};

enum {
    HWR_SF_NEEDS_CONFIRM = 1u << 0, /* a change wants confirm/revert */
    HWR_SF_ZERO_IS_AUTO  = 1u << 1, /* zero is legal and means "follow" */
    HWR_SF_MAP_OVERRIDE  = 1u << 2, /* a per-map profile may override it */
    HWR_SF_RESTART_FREE  = 1u << 3, /* takes effect without a restart */
    HWR_SF_EXCLUSIVE_MIN = 1u << 4  /* the minimum itself is not accepted */
};

/* A NULL label means the token is presentable as it stands. */
struct hwr_enum_value {
    const char *token;
    int32_t value;
    const char *label;
};

union hwr_value {
    int b;
    uint32_t u;
    float f;
    int32_t e;
    uint32_t rgb;
};

#define HWR_SCHEMA_NO_FIELD SIZE_MAX

struct hwr_setting_desc {
    const char *key;                /* cfg key; NULL for reported-state rows */
    enum hwr_value_type type;
    enum hwr_category category;
    enum hwr_exposure exposure;
    enum hwr_apply_kind apply_kind;
    /* The change bit the store raises for this field today. Rows that no hook
     * reacts to still carry theirs so the mask keeps its present meaning. */
    uint32_t change_bit;
    size_t offset;                  /* offsetof in struct hwr_settings */
    uint32_t flags;
    union hwr_value def;            /* built-in default */
    union hwr_value original;       /* the Original preset */
    union hwr_value min, max;       /* numeric range; unused for BOOL/ENUM */
    const struct hwr_enum_value *values;
    size_t value_count;
    int depends_on;                 /* index of the enabling row, or -1 */
    const char *label;
    const char *help;
    /* Optional. NULL unless the row needs them. */
    int (*available)(void);
    void (*to_field)(struct hwr_settings *, union hwr_value);
    union hwr_value (*from_field)(const struct hwr_settings *);
    int (*read_info)(const struct roth_api_v1 *, char *, size_t);
};

size_t hwr_schema_count(void);
const struct hwr_setting_desc *hwr_schema_at(size_t index);
const struct hwr_setting_desc *hwr_schema_find(const char *key);

/* Text to value. Returns 1 when the text is a valid value for the row, and 0
 * with an optional message otherwise. A successful parse is always in range. */
int hwr_schema_parse(const struct hwr_setting_desc *desc, const char *text,
                     union hwr_value *out, char *err, size_t err_size);
int hwr_schema_validate(const struct hwr_setting_desc *desc,
                        union hwr_value value, char *err, size_t err_size);
/* Value to text, in the spelling the cfg file uses. Returns 1 on success. */
int hwr_schema_format(const struct hwr_setting_desc *desc,
                      union hwr_value value, char *out, size_t out_size);

union hwr_value hwr_schema_get(const struct hwr_settings *settings,
                               const struct hwr_setting_desc *desc);
void hwr_schema_set(struct hwr_settings *settings,
                    const struct hwr_setting_desc *desc,
                    union hwr_value value);

void hwr_schema_defaults(struct hwr_settings *settings);
void hwr_schema_original(struct hwr_settings *settings);
uint32_t hwr_schema_all_change_bits(void);

#endif
