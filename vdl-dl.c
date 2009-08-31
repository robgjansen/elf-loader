#define _GNU_SOURCE
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
#include "macros.h"
#include "vdl-init-fini.h"
#include "vdl-sort.h"

struct ErrorList *find_error (void)
{
  unsigned long thread_pointer = machine_thread_pointer_get ();
  struct ErrorList *cur;
  for (cur = g_vdl.error; cur != 0; cur = cur->next)
    {
      if (cur->thread_pointer == thread_pointer)
	{
	  return cur;
	}
    }
  struct ErrorList * item = vdl_utils_new (struct ErrorList);
  item->thread_pointer = thread_pointer;
  item->error = 0;
  item->next = g_vdl.error;
  g_vdl.error = item;
  return item;
}

void set_error (const char *str, ...)
{
  va_list list;
  va_start (list, str);
  char *error_string = vdl_utils_vprintf (str, list);
  va_end (list);
  struct ErrorList *error = find_error ();
  if (error->error != 0)
    {
      vdl_utils_strfree (error->error);
    }
  error->error = error_string;
}

struct VdlFile *
caller_to_file (unsigned long caller)
{
  struct VdlFile *cur;
  for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
    {
      if (caller >= cur->ro_map.mem_start_align &&
	  caller <= (cur->ro_map.mem_start_align + cur->ro_map.mem_size_align))
	{
	  return cur;
	}
      if (caller >= cur->rw_map.mem_start_align &&
	  caller <= (cur->rw_map.mem_start_align + cur->rw_map.mem_size_align))
	{
	  return cur;
	}
    }
  return 0;
}

void *vdl_dlopen_private (const char *filename, int flags)
{
  VDL_LOG_FUNCTION ("filename=%s, flags=0x%x", filename, flags);
  futex_lock (&g_vdl.futex);

  if (filename == 0)
    {
      struct VdlFile *cur;
      for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
	{
	  if (cur->is_executable)
	    {
	      cur->count++;
	      futex_unlock (&g_vdl.futex);
	      return cur;
	    }
	}
      VDL_LOG_ASSERT (false, "Could not find main executable within linkmap");
    }

  // map it in memory using the normal context, that is, the
  // first context in the context list.
  struct VdlFileList *loaded = 0;
  struct VdlFile *mapped_file = vdl_file_map_single_maybe (g_vdl.contexts,
							   filename,
							   0, 0,
							   &loaded);
  if (mapped_file == 0)
    {
      set_error ("Unable to load: \"%s\"", filename);
      goto error;
    }

  if (!vdl_file_map_deps (mapped_file, &loaded))
    {
      set_error ("Unable to map dependencies of \"%s\"", filename);
      goto error;
    }

  struct VdlFileList *scope = vdl_sort_deps_breadth_first (mapped_file);
  if (flags & RTLD_GLOBAL)
    {
      // add this object as well as its dependencies to the global scope.
      // Note that it's not a big deal if the file has already been
      // added to the global scope in the past. We call unicize so
      // any duplicate entries appended here will be removed immediately.
      g_vdl.contexts->global_scope = vdl_file_list_append (g_vdl.contexts->global_scope, 
							   vdl_file_list_copy (scope));
      vdl_file_list_unicize (g_vdl.contexts->global_scope);
    }

  // setup the local scope of each newly-loaded file.
  struct VdlFileList *cur;
  for (cur = loaded; cur != 0; cur = cur->next)
    {
      // XXX: below, replace mapped_file by cur->item. everything fails.
      cur->item->local_scope = vdl_file_list_copy (scope);
      if (flags & RTLD_DEEPBIND)
	{
	  cur->item->lookup_type = LOOKUP_LOCAL_GLOBAL;
	}
      else
	{
	  cur->item->lookup_type = LOOKUP_GLOBAL_LOCAL;
	}
    }
  vdl_file_list_free (scope);

  vdl_tls_file_initialize (loaded);

  vdl_file_reloc (loaded, g_vdl.bind_now || flags & RTLD_NOW);
  
  if (vdl_tls_file_list_has_static (loaded))
    {
      // damn-it, one of the files we loaded
      // has indeed a static tls block. we don't know
      // how to handle them because that would require
      // adding space to the already-allocated static tls
      // which, by definition, can't be deallocated.
      set_error ("Attempting to dlopen a file with a static tls block");
      goto error;
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

 error:
  {
    // we don't need to call_fini here because we have not yet
    // called call_init.
    struct VdlFileList *unload = vdl_gc_get_objects_to_unload ();

    vdl_tls_file_deinitialize (unload);

    vdl_files_delete (unload);

    vdl_file_list_free (unload);

    gdb_notify ();

    futex_unlock (&g_vdl.futex);
  }
  return 0;
}

void *vdl_dlsym_private (void *handle, const char *symbol, unsigned long caller)
{
  VDL_LOG_FUNCTION ("handle=0x%llx, symbol=%s, caller=0x%llx", handle, symbol, caller);
  futex_lock (&g_vdl.futex);
  struct VdlFileList *scope;
  struct VdlFile *caller_file = caller_to_file (caller);
  struct VdlFile *file = (struct VdlFile*)handle;
  if (caller_file == 0)
    {
      set_error ("Can't find caller");
      goto error;
    }
  if (handle == RTLD_DEFAULT)
    {
      scope = vdl_file_list_copy (caller_file->context->global_scope);
    }
  else if (handle == RTLD_NEXT)
    {
      scope = caller_file->context->global_scope;
      // skip all objects before the caller object
      bool found = false;
      struct VdlFileList *cur;
      for (cur = scope; cur != 0; cur = cur->next)
	{
	  if (cur->item == caller_file)
	    {
	      // go to the next object
	      scope = vdl_file_list_copy (cur->next);
	      scope = cur->next;
	      found = true;
	      break;
	    }
	}
      if (!found)
	{
	  set_error ("Can't find caller in current local scope");
	  goto error;
	}
    }
  else
    {
      if (file == 0)
	{
	  set_error ("Invalid handle");
	  goto error;
	}
      scope = vdl_sort_deps_breadth_first (file);
    }
  struct SymbolMatch match;
  if (!vdl_file_symbol_lookup_scope (symbol, scope, &match))
    {
      set_error ("Could not find requested symbol \"%s\"", symbol);
      goto error;
    }
  vdl_file_list_free (scope);
  futex_unlock (&g_vdl.futex);
  return (void*)(match.file->load_base + match.symbol->st_value);
 error:
  futex_unlock (&g_vdl.futex);
  return 0;
}

int vdl_dlclose_private (void *handle)
{
  VDL_LOG_FUNCTION ("handle=0x%llx", handle);
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
  VDL_LOG_FUNCTION ("", 0);
  struct ErrorList *error = find_error ();
  char *error_string = error->error;
  if (error_string != 0)
    {
      vdl_utils_strfree (error_string);
    }
  // clear the error we are about to report to the user
  error->error = 0;
  return error_string;
}

int vdl_dladdr_private (void *addr, Dl_info *info)
{
  set_error ("dladdr unimplemented");
  return 0;
}
void *vdl_dlvsym_private (void *handle, char *symbol, char *version, unsigned long caller)
{
  VDL_LOG_FUNCTION ("handle=0x%llx, symbol=%s, version=%s, caller=0x%llx", 
		    handle, symbol, version, caller);
  set_error ("dlvsym unimplemented");
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

EXPORT void *vdl_dlsym_public (void *handle, const char *symbol, unsigned long caller)
{
  return vdl_dlsym_private (handle, symbol, caller);
}

EXPORT int vdl_dlclose_public (void *handle)
{
  return vdl_dlclose_private (handle);
}
EXPORT int vdl_dladdr_public (void *addr, Dl_info *info)
{
  return vdl_dladdr_private (addr, info);
}
EXPORT void *vdl_dlvsym_public (void *handle, char *symbol, char *version, unsigned long caller)
{
  return vdl_dlvsym_private (handle, symbol, version, caller);
}
