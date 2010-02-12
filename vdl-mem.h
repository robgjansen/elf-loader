#ifndef VDL_MEM_H
#define VDL_MEM_H

#include <unistd.h>

void vdl_memcpy (void *dst, const void *src, size_t len);
void vdl_memmove (void *dst, const void *src, size_t len);
void vdl_memset(void *s, int c, size_t n);

#endif /* VDL_MEM_H */
