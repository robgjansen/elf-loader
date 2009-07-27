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
#include "vdl-tls.h"
#include "machine.h"
#include "export.h"
#include "vdl-init-fini.h"
#include "vdl-sort.h"

void *vdl_dlopen_private (const char *filename, int flags)
{
  futex_lock (&g_vdl.futex);

  // map it in memory using the normal context, that is, the
  // first context in the context list.
  struct VdlFileList *loaded = 0;
  struct VdlFile *mapped_file = vdl_file_map_single_maybe (g_vdl.contexts,
							   filename,
							   &loaded);
  if (mapped_file == 0)
    {
      VDL_LOG_ERROR ("Unable to load: \"%s\"\n", filename);
      goto error;
    }

  if (!vdl_file_map_deps (mapped_file, &loaded))
    {
      VDL_LOG_ERROR ("Unable to map dependencies of \"%s\"\n", filename);
      goto error;
    }

  struct VdlFileList *deps = vdl_sort_deps_breadth_first_one (mapped_file);
  if (flags & RTLD_GLOBAL)
    {
      // add this object as well as its dependencies to the global scope.
      struct VdlFileList *copy = vdl_file_list_copy (deps);
      g_vdl.contexts->global_scope = vdl_file_list_append (g_vdl.contexts->global_scope, copy);
      vdl_file_list_unicize (g_vdl.contexts->global_scope);
    }

  // setup the local scope of each newly-loaded file.
  struct VdlFileList *cur;
  for (cur = loaded; cur != 0; cur = cur->next)
    {
      cur->item->local_scope = vdl_file_list_copy (deps);
      if (flags & RTLD_DEEPBIND)
	{
	  cur->item->lookup_type = LOOKUP_LOCAL_GLOBAL;
	}
      else
	{
	  cur->item->lookup_type = LOOKUP_GLOBAL_LOCAL;
	}
    }

  vdl_file_list_free (deps);

  vdl_tls_file_initialize (loaded);

  vdl_file_reloc (loaded, g_vdl.bind_now || flags & RTLD_NOW);
  
  if (vdl_tls_file_list_has_static (loaded))
    {
      // damn-it, one of the files we loaded
      // has indeed a static tls block. we don't know
      // how to handle them because that would require
      // adding space to the already-allocated static tls
      // which, by definition, can't be deallocated.
      goto static_error;
    }

  gdb_notify ();

  glibc_patch (loaded);

  mapped_file->count++;

  struct VdlFileList *call_init = vdl_sort_call_init (loaded);

  // we need to release the lock before calling the initializers 
  // to avoid a deadlock if one of them calls dlopen or
  // a symbol resolution function
  futex_unlock (&g_vdl.futex);
  vdl_init_fini_call_init (call_init);
  futex_lock (&g_vdl.futex);

  // must hold the lock to call free
  vdl_file_list_free (call_init);
  vdl_file_list_free (loaded);

  futex_unlock (&g_vdl.futex);
  return mapped_file;

 static_error:
  vdl_tls_file_deinitialize (loaded);

 error:
  vdl_files_delete (loaded);
  vdl_file_list_free (loaded);
  futex_unlock (&g_vdl.futex);
  return 0;
}

void *vdl_dlsym_private (void *handle, const char *symbol)
{
  // XXX handle RTLD_DEFAULT and RTLD_NEXT
  struct VdlFile *file = (struct VdlFile*)handle;
  // XXX: the lookup should be a lookup in local scope, not
  // only in this binary.
  unsigned long size;
  unsigned long v = vdl_file_symbol_lookup_local (file, symbol, &size);
  return (void*)v;
}

int vdl_dlclose_private (void *handle)
{
  futex_lock (&g_vdl.futex);

  struct VdlFile *file = (struct VdlFile*)handle;
  file->count--;

  // first, we gather the list of all objects to unload/delete
  struct VdlFileList *unload = vdl_gc_get_objects_to_unload ();
  struct VdlFileList *call_fini = vdl_sort_call_fini (unload);

  // must not hold the lock to call fini
  futex_unlock (&g_vdl.futex);
  vdl_init_fini_call_fini (call_fini);
  futex_lock (&g_vdl.futex);

  vdl_file_list_free (call_fini);

  vdl_tls_file_deinitialize (unload);

  vdl_files_delete (unload);

  vdl_file_list_free (unload);

  gdb_notify ();

  futex_unlock (&g_vdl.futex);
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
