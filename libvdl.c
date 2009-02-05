#include "export.h"
#include "vdl-dl.h"

/* We provide these wrappers to be able to export a libvdl.so
 * with the _exact_ version definitions matching the system's
 * libdl.so. Yes, we could do the same thing directly in our
 * ldso but doing this would require that we build the ldso
 * version definition file from a merge of both /lib/ld-linux.so.2 
 * and /lib/libdl.so.2 which did more complicated than writing
 * these trivial wrappers.
 */

EXPORT void *dlopen(const char *filename, int flag)
{
  return vdl_dlopen (filename, flag);
}

EXPORT char *dlerror(void)
{
  return vdl_dlerror ();
}

EXPORT void *dlsym(void *handle, const char *symbol)
{
  return vdl_dlsym (handle, symbol);
}

EXPORT int dlclose(void *handle)
{
  return vdl_dlclose (handle);
}
