#include "macros.h"
#include "vdl-dl.h"

/* We provide these wrappers to be able to export a libvdl.so
 * with the _exact_ version definitions matching the system's
 * libdl.so. Yes, we could do the same thing directly in our
 * ldso but doing this would require that we build the ldso
 * version definition file from a merge of both /lib/ld-linux.so.2 
 * and /lib/libdl.so.2 which is more complicated than writing
 * these trivial wrappers.
 */

EXPORT void *dlopen(const char *filename, int flag)
{
  return vdl_dlopen_public (filename, flag);
}

EXPORT char *dlerror(void)
{
  return vdl_dlerror_public ();
}

EXPORT void *dlsym(void *handle, const char *symbol)
{
  return vdl_dlsym_public (handle, symbol, RETURN_ADDRESS);
}

EXPORT int dlclose(void *handle)
{
  return vdl_dlclose_public (handle);
}
EXPORT int dladdr (const void *addr, Dl_info *info)
{
  return vdl_dladdr_public (addr, info);
}
EXPORT void *dlvsym (void *handle, const char *symbol, const char *version)
{
  return vdl_dlvsym_public (handle, symbol, version, RETURN_ADDRESS);
}

