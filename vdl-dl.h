#ifndef VDL_DL_H
#define VDL_DL_H

#include "export.h"

// the 'public' version is called from libvdl.so
EXPORT void *vdl_dlopen_public (const char *filename, int flag);
EXPORT char *vdl_dlerror_public (void);
EXPORT void *vdl_dlsym_public (void *handle, const char *symbol, unsigned long caller);
EXPORT int vdl_dlclose_public (void *handle);

// the 'private' version is called from ldso itself
// to avoid the pain of calling these functions through
// a PLT indirection which would require ldso to be able
// to relocate its own JMP_SLOT entries which would be a
// bit painful to do.
void *vdl_dlopen_private (const char *filename, int flag);
void *vdl_dlsym_private (void *handle, const char *symbol, unsigned long caller);
int vdl_dlclose_private (void *handle);

#endif /* VDL_DL_H */
