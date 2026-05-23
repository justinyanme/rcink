#ifndef TLSR_LAYOUT_INCLUDE_OVERRIDE_STRING_H
#define TLSR_LAYOUT_INCLUDE_OVERRIDE_STRING_H

#ifndef NULL
#define NULL 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

void *memset(void *d, int c, unsigned int n);
void *memcpy(void *des_ptr, const void *src_ptr, unsigned int n);
void memcpy4(void *dest, const void *src, unsigned int n);
int memcmp(const void *_s1, const void *_s2, unsigned int _n);
unsigned int strlen(const char *_s);
int ismemzero4(void *data, unsigned int len);

#ifdef __cplusplus
}
#endif

#endif
