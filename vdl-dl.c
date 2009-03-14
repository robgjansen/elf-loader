#include "vdl.h"
#include "vdl-log.h"
#include "vdl-utils.h"
#include "vdl-file-list.h"
#include "gdb.h"
#include "glibc.h"
#include "vdl-dl.h"
#include "vdl-gc.h"
#include "vdl-file-reloc.h"
#include "vdl-file-symbol.h"
#include "export.h"


void *vdl_dlopen_private (const char *filename, int flags)
{
  char *full_filename = vdl_search_filename (filename);
  if (full_filename == 0)
    {
      VDL_LOG_ERROR ("Could not find %s\n", filename);
      goto error;
    }
  // map it in memory using the normal context, that is, the
  // first context in the context list.
  struct VdlFile *mapped_file = vdl_file_map_single (g_vdl.contexts,
						     full_filename, 
						     filename);
  if (mapped_file == 0)
    {
      VDL_LOG_ERROR ("Unable to load: \"%s\"\n", full_filename);
      goto error;
    }
  if (!vdl_file_map_deps (mapped_file))
    {
      VDL_LOG_ERROR ("Unable to map dependencies of \"%s\"\n", full_filename);
      goto error;
    }
  struct VdlFileList *deps = vdl_file_gather_all_deps_breadth_first (mapped_file);
  mapped_file->local_scope = deps;
  if (flags & RTLD_GLOBAL)
    {
      // add this object as well as its dependencies to the global scope.
      struct VdlFileList *copy = vdl_file_list_copy (deps);
      g_vdl.contexts->global_scope = vdl_file_list_append (g_vdl.contexts->global_scope, copy);
      vdl_file_list_unicize (g_vdl.contexts->global_scope);
    }
  if (flags & RTLD_DEEPBIND)
    {
      mapped_file->lookup_type = LOOKUP_LOCAL_GLOBAL;
    }
  else
    {
      mapped_file->lookup_type = LOOKUP_GLOBAL_LOCAL;
    }

  vdl_file_tls (mapped_file);

  vdl_file_reloc (mapped_file, g_vdl.bind_now || flags & RTLD_NOW);

  gdb_notify ();

  glibc_patch (mapped_file);

  vdl_file_call_init (mapped_file);

  mapped_file->count++;

  return mapped_file;
 error:
  return 0;
}

void *vdl_dlsym_private (void *handle, const char *symbol)
{
  // XXX handle RTLD_DEFAULT and RTLD_NEXT
  struct VdlFile *file = (struct VdlFile*)handle;
  // XXX: the lookup should be a lookup in local scope, not
  // only in this binary.
  unsigned long v = vdl_file_symbol_lookup_local (file, symbol);
  return (void*)v;
}

int vdl_dlclose_private (void *handle)
{
  struct VdlFile *file = (struct VdlFile*)handle;
  file->count--;
  vdl_gc ();
  gdb_notify ();
  return 0;
}

char *vdl_dlerror_private (void)
{
  // indicates that no error happened
  return 0;
}

EXPORT void *vdl_dlopen_public (const char *filename, int flag)
{
  return vdl_dlopen_private (filename, flag);
}

EXPORT char *vdl_dlerror_public (void)
{
  return vdl_dlerror_private ();
}

EXPORT void *vdl_dlsym_public (void *handle, const char *symbol)
{
  return vdl_dlsym_private (handle, symbol);
}

EXPORT int vdl_dlclose_public (void *handle)
{
  return vdl_dlclose_private (handle);
}
