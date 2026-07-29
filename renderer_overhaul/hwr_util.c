#include "hwr_util.h"

#include <ctype.h>
#include <stdlib.h>

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
