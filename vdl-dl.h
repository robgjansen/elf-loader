#ifndef VDL_DL_H
#define VDL_DL_H

#include "macros.h"
#define _GNU_SOURCE 
#include <dlfcn.h> // for Dl_info
#include <link.h> // for struct dl_phdr_info

// the 'public' version is called from libvdl.so
EXPORT void *vdl_dlopen_public (const char *filename, int flag);
EXPORT char *vdl_dlerror_public (void);
EXPORT void *vdl_dlsym_public (void *handle, const char *symbol, unsigned long caller);
EXPORT int vdl_dlclose_public (void *handle);
EXPORT int vdl_dladdr_public (const void *addr, Dl_info *info);
EXPORT void *vdl_dlvsym_public (void *handle, const char *symbol, const char *version, unsigned long caller);
EXPORT int vdl_dl_iterate_phdr_public (int (*callback) (struct dl_phdr_info *info,
							size_t size, void *data),
				       void *data);
EXPORT void *vdl_dlmopen_public (Lmid_t lmid, const char *filename, int flag);
// create a new linkmap
EXPORT Lmid_t vdl_dl_lmid_new_public (int argc, const char **argv, const char **envp);
EXPORT int vdl_dl_add_callback_public (Lmid_t lmid, 
					void (*cb) (void *handle, int event, void *context),
					void *cb_context);
EXPORT int vdl_dl_add_lib_remap_public (Lmid_t lmid, const char *src, const char *dst);
EXPORT int vdl_dl_add_symbol_remap_public (Lmid_t lmid,
					   const char *src_name, 
					   const char *src_ver_name, 
					   const char *src_ver_filename, 
					   const char *dst_name,
					   const char *dst_ver_name,
					   const char *dst_ver_filename);




// the 'private' version is called from ldso itself
// to avoid the pain of calling these functions through
// a PLT indirection which would require ldso to be able
// to relocate its own JMP_SLOT entries which would be a
// bit painful to do.
void *vdl_dlopen_private (const char *filename, int flag);
void *vdl_dlsym_private (void *handle, const char *symbol, unsigned long caller);
int vdl_dlclose_private (void *handle);
int vdl_dladdr_private (const void *addr, Dl_info *info);
void *vdl_dlvsym_private (void *handle, const char *symbol, const char *version, unsigned long caller);
int vdl_dl_iterate_phdr_private (int (*callback) (struct dl_phdr_info *info,
						  size_t size, void *data),
				 void *data,
				 unsigned long caller);

#endif /* VDL_DL_H */
