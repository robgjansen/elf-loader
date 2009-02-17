#include "vdl.h"
#include "vdl-log.h"
#include "vdl-utils.h"
#include "vdl-file-list.h"
#include "gdb.h"
#include "glibc.h"
#include "vdl-dl.h"
#include "vdl-gc.h"
#include "export.h"


EXPORT void *vdl_dlopen(const char *filename, int flag)
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
  if (flag & RTLD_GLOBAL)
    {
      // add this object as well as its dependencies to the global scope.
      g_vdl.contexts->global_scope = vdl_file_list_append (g_vdl.contexts->global_scope, deps);
      vdl_file_list_unicize (g_vdl.contexts->global_scope);
    }
  else
    {
      // otherwise, setup that object's local scope.
      mapped_file->local_scope = deps;
    }

  vdl_file_tls (mapped_file);

  vdl_file_reloc (mapped_file);

  gdb_notify ();

  glibc_patch (mapped_file);

  vdl_file_call_init (mapped_file);

  return mapped_file;
 error:
  return 0;
}

EXPORT char *vdl_dlerror(void)
{
  //XXX
  return "";
}

EXPORT void *vdl_dlsym(void *handle, const char *symbol)
{
  // XXX handle RTLD_DEFAULT and RTLD_NEXT
  struct VdlFile *file = (struct VdlFile*)handle;
  // XXX: the lookup should be a lookup in local scope, not
  // only in this binary.
  unsigned long v = vdl_file_symbol_lookup_local (file, symbol);
  return (void*)v;
}

EXPORT int vdl_dlclose(void *handle)
{
  struct VdlFile *file = (struct VdlFile*)handle;
  file->count--;
  vdl_gc ();
  gdb_notify ();
  return 0;
}
