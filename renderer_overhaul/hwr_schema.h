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

/* Which control the settings overlay offers for a row. The table says so rather
 * than the overlay recognising particular keys, which is the whole point of
 * having one description of a setting. AUTO follows from the value type. */
enum hwr_widget {
    HWR_WIDGET_AUTO = 0,
    HWR_WIDGET_RESOLUTION_PAIR, /* this row and the next of its kind are one control */
    HWR_WIDGET_KEY_CAPTURE      /* the value is the next key the player presses */
};

enum {
    HWR_SF_NEEDS_CONFIRM = 1u << 0, /* a change wants confirm/revert */
    HWR_SF_ZERO_IS_AUTO  = 1u << 1, /* zero is legal and means "follow" */
    /* A game config's entry for the map being played may override it. Which
     * rows those are is this flag and nothing else, so adding a look key is
     * adding one flag to one row and no second list can drift from it. */
    HWR_SF_MAP_OVERRIDE  = 1u << 2,
    HWR_SF_RESTART_FREE  = 1u << 3, /* takes effect without a restart */
    HWR_SF_EXCLUSIVE_MIN = 1u << 4, /* the minimum itself is not accepted */
    /* The row is live at one particular value of the row it hangs off, which
     * depends_on_value names, rather than at any value but that row's off. */
    HWR_SF_DEPENDS_ON_VALUE = 1u << 5
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
    /* One adjustment and one page of adjustment. Zero means "derive from the
     * type", so a row only carries them when its own scale differs. */
    union hwr_value step, page_step;
    const struct hwr_enum_value *values;
    size_t value_count;
    int depends_on;                 /* index of the enabling row, or -1 */
    /* The same relationship named by cfg key and resolved at run time, which is
     * how a row says it belongs to something that sits wherever the table
     * happens to put it -- the true-colour foundation, for one. It wins over
     * the index above when both are given. */
    const char *depends_on_key;
    /* Which value of that row brings this one to life, for a parent that is
     * more than a switch. Read only with HWR_SF_DEPENDS_ON_VALUE, so a row
     * whose enabling value happens to be a zero still says so. */
    union hwr_value depends_on_value;
    enum hwr_widget widget;
    const char *label;
    const char *help;
    /* What a zero means in words, for a row whose zero is automatic. */
    const char *auto_label;
    /* Optional. NULL unless the row needs them. */
    int (*available)(void);
    void (*to_field)(struct hwr_settings *, union hwr_value);
    union hwr_value (*from_field)(const struct hwr_settings *);
    int (*read_info)(const struct roth_api_v1 *, char *, size_t);
};

/* The members of one enumeration row, where they are not known until something
 * outside the table has looked: the game config picker's are the files in the
 * game configs folder, and there is no such thing as a compile-time list of
 * those. The array belongs to the caller and has to outlive the row, which is
 * why the one caller keeps it in a module of its own. The row must be an
 * enumeration and the list must hold at least one member; anything else is
 * refused and the row keeps what it had.
 *
 * Nothing else in the table is written after it is built, and every reader
 * outside this file is handed a const view of it. */
void hwr_schema_set_enum_values(const char *key,
                                const struct hwr_enum_value *values,
                                size_t count);

size_t hwr_schema_count(void);
const struct hwr_setting_desc *hwr_schema_at(size_t index);
const struct hwr_setting_desc *hwr_schema_find(const char *key);
/* The table position of a row, or -1 when the row is not from this table. */
int hwr_schema_index_of(const struct hwr_setting_desc *desc);
/* The row this one hangs off, however it named it, or -1 when it stands alone. */
int hwr_schema_depends_on(const struct hwr_setting_desc *desc);
/* Whether that row is at a value which gives this one something to do: the one
 * value it names where it names one, and anything but the parent's own off
 * otherwise. A row that hangs off nothing is always live. This is what tells a
 * dial that reaches only one shading law from one that reaches every law. */
int hwr_schema_dependency_met(const struct hwr_setting_desc *desc,
                              const struct hwr_settings *settings);
/* Two values of one row compared as that row's type, which is the only way two
 * of them can be compared: the union alone does not say which member is live. */
int hwr_schema_value_same(const struct hwr_setting_desc *desc,
                          union hwr_value a, union hwr_value b);
/* Whether a value is the one the Original preset holds for its row, which is
 * what "neutral" means for every row the authentic picture depends on. */
int hwr_schema_is_original(const struct hwr_setting_desc *desc,
                           union hwr_value value);

/* Text to value. Returns 1 when the text is a valid value for the row, and 0
 * with an optional message otherwise. A successful parse is always in range. */
int hwr_schema_parse(const struct hwr_setting_desc *desc, const char *text,
                     union hwr_value *out, char *err, size_t err_size);
int hwr_schema_validate(const struct hwr_setting_desc *desc,
                        union hwr_value value, char *err, size_t err_size);
/* Every row of one whole settings object, plus the rules that span two rows and
 * therefore cannot be checked as a value arrives. Both writers into the store --
 * the configuration file and the settings overlay -- ask this before they
 * commit, so neither can express a state the other would refuse. */
int hwr_schema_validate_all(const struct hwr_settings *settings,
                            char *err, size_t err_size);
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
