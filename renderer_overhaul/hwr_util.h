#ifndef HWR_UTIL_H
#define HWR_UTIL_H

#include <stdint.h>

int hwr_ascii_equal_ci(const char *a, const char *b);
/* Whether a sorts before b, ignoring case, with the case-sensitive comparison
 * as the tie-break so that two names differing only in case still have one
 * settled order. It exists so that a folder listing -- which arrives in
 * whatever order a filesystem feels like -- is shown in the same order on
 * every machine and on every run. */
int hwr_ascii_before_ci(const char *a, const char *b);

/* A set of settings rows, held by table position.
 *
 * Three parts of the mod keep one: which keys the file being replaced carried,
 * which rows the current map overrides, and which rows a panel has already
 * drawn. Each of them used to keep it in a single machine word, which is
 * sixty-four positions -- and the table is already longer than that. A row past
 * the sixty-fourth was therefore outside every one of those sets in silence: its
 * key dropped from the file it was written in, its per-map override refused, and
 * nothing said about either. The width is one number here, and the table asserts
 * it is wide enough for itself (hwr_schema.c), so a row added past the end stops
 * the build rather than quietly stopping working. */
#define HWR_ROW_SET_MAX 128u
#define HWR_ROW_SET_WORDS ((HWR_ROW_SET_MAX + 31u) / 32u)

struct hwr_row_set {
    uint32_t word[HWR_ROW_SET_WORDS];
};

void hwr_row_set_clear(struct hwr_row_set *set);
/* Both take a table position and refuse one outside the set's range, so a caller
 * already holding the -1 that means "not a row of this table" needs no second
 * test for it. */
void hwr_row_set_add(struct hwr_row_set *set, int index);
int hwr_row_set_has(const struct hwr_row_set *set, int index);

/* Cuts a configuration line at its comment and hands the line back. Both files
 * the mod reads -- the settings it is given and the look it is described with --
 * say a hash starts a comment, and they say it in one place so that neither can
 * drift into meaning something else. A colour is therefore written in a file as
 * bare hexadecimal, which is also how one is written back. */
char *hwr_strip_comment(char *line);

/* A surface normal packed into two bytes, octahedral: the direction is folded
 * onto the surface of an octahedron and that fold is unwrapped into a square,
 * which spends both bytes evenly over the sphere and lands within about a degree
 * of the direction it was handed -- finer than any shading law reads.
 *
 * Each byte holds one unwrapped component with 128 as zero and 1 and 255 as the
 * two ends, so the six axis directions -- a floor, a ceiling, a wall on a grid,
 * which is most of a map -- come back exactly. That spends 254 of the 256 values
 * and leaves zero unreachable in either component, which makes the pair (0,0)
 * the free value for "this surface carries no normal": every vertex of the
 * original picture and every class a light never touches. */
void hwr_normal_encode(float x, float y, float z, uint8_t *u, uint8_t *v);
/* The inverse the geometry shader runs, in C, so what a fragment sees can be
 * held against what an emitter meant without a screen in between. Answers
 * (0,0,0) for the reserved pair. */
void hwr_normal_decode(uint8_t u, uint8_t v, float *x, float *y, float *z);

/* HWR_DAS_PERSIST=0 rebuilds everything a DAS file contributes on every map
 * transition: the DAS files are re-opened and re-decoded, and the whole texture
 * store is re-packed and re-uploaded. It is the kill switch for DAS-identity
 * persistence and restores the pre-persistence behaviour exactly.
 * (HWR_ATLAS_PERSIST is accepted as the former spelling.) */
int hwr_das_persist_enabled(void);

#endif
