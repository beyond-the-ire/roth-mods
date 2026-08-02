#include "hwr_util.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

int hwr_ascii_equal_ci(const char *a, const char *b)
{
    while (*a != 0 && *b != 0) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

void hwr_row_set_clear(struct hwr_row_set *set)
{
    if (set != NULL)
        memset(set, 0, sizeof *set);
}

void hwr_row_set_add(struct hwr_row_set *set, int index)
{
    if (set == NULL || index < 0 || (unsigned)index >= HWR_ROW_SET_MAX)
        return;
    set->word[(unsigned)index / 32u] |= UINT32_C(1) << ((unsigned)index % 32u);
}

int hwr_row_set_has(const struct hwr_row_set *set, int index)
{
    if (set == NULL || index < 0 || (unsigned)index >= HWR_ROW_SET_MAX)
        return 0;
    return (set->word[(unsigned)index / 32u] &
            (UINT32_C(1) << ((unsigned)index % 32u))) != 0;
}

char *hwr_strip_comment(char *line)
{
    char *comment;

    if (line == NULL)
        return line;
    comment = strchr(line, '#');
    if (comment != NULL)
        *comment = 0;
    return line;
}

/* Zero counts as positive on both sides of the fold, in C and in the shader
 * alike. The language's own sign() answers zero for zero, which would collapse
 * an axis-aligned direction onto the origin of the unwrapped square and lose the
 * fold it belongs to, so neither side uses it. */
static double hwr_normal_sign(double v)
{
    return v < 0.0 ? -1.0 : 1.0;
}

/* The intermediate is double on purpose: the two lanes this builds for differ in
 * how much precision a float expression carries, and the encoded byte must be
 * the same byte on both. */
void hwr_normal_encode(float x, float y, float z, uint8_t *u, uint8_t *v)
{
    double nx = x;
    double ny = y;
    double nz = z;
    double sum = fabs(nx) + fabs(ny) + fabs(nz);
    double px;
    double py;
    int iu;
    int iv;

    if (u == NULL || v == NULL)
        return;
    if (!(sum > 0.0)) {
        *u = 0;
        *v = 0;
        return;
    }
    nx /= sum;
    ny /= sum;
    nz /= sum;
    if (nz >= 0.0) {
        px = nx;
        py = ny;
    } else {
        px = (1.0 - fabs(ny)) * hwr_normal_sign(nx);
        py = (1.0 - fabs(nx)) * hwr_normal_sign(ny);
    }
    /* 128 is zero and 1 and 255 are the two ends, which spends 254 of the 256
     * values and leaves zero unreachable in EITHER component. Spreading the
     * range over all 256 instead would put zero half a step off centre, and the
     * six axis directions -- a floor, a ceiling, a wall on a grid, which is most
     * of a map -- would come back a fifth of a degree out for nothing. */
    iu = (int)floor(px * 127.0 + 128.5);
    iv = (int)floor(py * 127.0 + 128.5);
    if (iu < 1)
        iu = 1;
    if (iu > 255)
        iu = 255;
    if (iv < 1)
        iv = 1;
    if (iv > 255)
        iv = 255;
    *u = (uint8_t)iu;
    *v = (uint8_t)iv;
}

void hwr_normal_decode(uint8_t u, uint8_t v, float *x, float *y, float *z)
{
    double px = ((double)u - 128.0) / 127.0;
    double py = ((double)v - 128.0) / 127.0;
    double nx = px;
    double ny = py;
    double nz = 1.0 - fabs(px) - fabs(py);
    double length;

    if (x == NULL || y == NULL || z == NULL)
        return;
    if (u == 0 && v == 0) {
        *x = 0.0f;
        *y = 0.0f;
        *z = 0.0f;
        return;
    }
    if (nz < 0.0) {
        nx = (1.0 - fabs(py)) * hwr_normal_sign(px);
        ny = (1.0 - fabs(px)) * hwr_normal_sign(py);
    }
    length = sqrt(nx * nx + ny * ny + nz * nz);
    if (length > 0.0) {
        nx /= length;
        ny /= length;
        nz /= length;
    }
    *x = (float)nx;
    *y = (float)ny;
    *z = (float)nz;
}

int hwr_das_persist_enabled(void)
{
    static int state = -1;
    if (state < 0) {
        const char *setting = getenv("HWR_DAS_PERSIST");
        if (setting == NULL)
            setting = getenv("HWR_ATLAS_PERSIST");
        state = setting != NULL && setting[0] == '0' ? 0 : 1;
    }
    return state;
}
