#ifndef HWR_UTIL_H
#define HWR_UTIL_H

int hwr_ascii_equal_ci(const char *a, const char *b);

/* HWR_DAS_PERSIST=0 rebuilds everything a DAS file contributes on every map
 * transition: the DAS files are re-opened and re-decoded, and the whole texture
 * store is re-packed and re-uploaded. It is the kill switch for DAS-identity
 * persistence and restores the pre-persistence behaviour exactly.
 * (HWR_ATLAS_PERSIST is accepted as the former spelling.) */
int hwr_das_persist_enabled(void);

#endif
