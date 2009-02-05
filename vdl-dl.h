#ifndef VDL_DL_H
#define VDL_DL_H

void *vdl_dlopen(const char *filename, int flag);

char *vdl_dlerror(void);

void *vdl_dlsym(void *handle, const char *symbol);

int vdl_dlclose(void *handle);

#endif /* VDL_DL_H */
