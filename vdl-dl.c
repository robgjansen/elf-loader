#define _GNU_SOURCE
#include "vdl.h"
#include "vdl-log.h"
#include "vdl-utils.h"
#include "vdl-file-list.h"
#include "gdb.h"
#include "glibc.h"
#include "vdl-dl.h"
#include "vdl-gc.h"
#include "vdl-reloc.h"
#include "vdl-lookup.h"
#include "vdl-tls.h"
#include "machine.h"
#include "macros.h"
#include "vdl-init-fini.h"
#include "vdl-sort.h"

static struct ErrorList *find_error (void)
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

static void set_error (const char *str, ...)
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

static struct VdlFile *
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

static struct VdlContext *search_context (struct VdlContext *context)
{
  struct VdlContext *cur;
  for (cur = g_vdl.contexts; cur != 0; cur = cur->next)
    {
      if (context == cur)
	{
	  return context;
	}
    }
  set_error ("Can't find requested lmid 0x%x", context);
  return 0;
}

static struct VdlFile *search_file (void *handle)
{
  struct VdlFile *cur;
  for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
    {
      if (cur == handle)
	{
	  return cur;
	}
    }
  set_error ("Can't find requested file 0x%x", handle);
  return 0;
}


// assumes caller has lock
static void *dlopen_with_context (struct VdlContext *context, const char *filename, int flags)
{
  VDL_LOG_FUNCTION ("filename=%s, flags=0x%x", filename, flags);

  if (filename == 0)
    {
      struct VdlFileList *cur;
      for (cur = context->global_scope; cur != 0; cur = cur->next)
	{
	  if (cur->item->is_executable)
	    {
	      cur->item->count++;
	      return cur;
	    }
	}
      VDL_LOG_ASSERT (false, "Could not find main executable within linkmap");
    }

  // map it in memory using the normal context, that is, the
  // first context in the context list.
  struct VdlFileList *loaded = 0;
  struct VdlFile *mapped_file = vdl_file_map_single_maybe (context,
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
      context->global_scope = vdl_file_list_append (context->global_scope, 
						    vdl_file_list_copy (scope));
      vdl_file_list_unicize (context->global_scope);
    }

  // setup the local scope of each newly-loaded file.
  struct VdlFileList *cur;
  for (cur = loaded; cur != 0; cur = cur->next)
    {
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

  vdl_reloc (loaded, g_vdl.bind_now || flags & RTLD_NOW);
  
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
  }
  return 0;
}
void *vdl_dlopen_private (const char *filename, int flags)
{
  futex_lock (&g_vdl.futex);
  void *handle = dlopen_with_context (g_vdl.contexts, filename, flags);
  futex_unlock (&g_vdl.futex);
  return handle;
}

void *vdl_dlsym_private (void *handle, const char *symbol, unsigned long caller)
{
  VDL_LOG_FUNCTION ("handle=0x%llx, symbol=%s, caller=0x%llx", handle, symbol, caller);
  return vdl_dlvsym_private (handle, symbol, 0, caller);
}

int vdl_dlclose_private (void *handle)
{
  VDL_LOG_FUNCTION ("handle=0x%llx", handle);
  futex_lock (&g_vdl.futex);

  struct VdlFile *file = search_file (handle);
  if (file == 0)
    {
      futex_unlock (&g_vdl.futex);
      return -1;
    }
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
  futex_lock (&g_vdl.futex);
  struct ErrorList *error = find_error ();
  char *error_string = error->error;
  if (error_string != 0)
    {
      vdl_utils_strfree (error_string);
    }
  // clear the error we are about to report to the user
  error->error = 0;
  futex_unlock (&g_vdl.futex);
  return error_string;
}

int vdl_dladdr_private (const void *addr, Dl_info *info)
{
  futex_lock (&g_vdl.futex);
  set_error ("dladdr unimplemented");
  futex_unlock (&g_vdl.futex);
  return 0;
}
void *vdl_dlvsym_private (void *handle, const char *symbol, const char *version, unsigned long caller)
{
  VDL_LOG_FUNCTION ("handle=0x%llx, symbol=%s, version=%s, caller=0x%llx", 
		    handle, symbol, version, caller);
  futex_lock (&g_vdl.futex);
  struct VdlFileList *scope;
  struct VdlFile *caller_file = caller_to_file (caller);
  struct VdlContext *context;
  if (caller_file == 0)
    {
      set_error ("Can't find caller");
      goto error;
    }
  if (handle == RTLD_DEFAULT)
    {
      scope = vdl_file_list_copy (caller_file->context->global_scope);
      context = caller_file->context;
    }
  else if (handle == RTLD_NEXT)
    {
      scope = caller_file->context->global_scope;
      context = caller_file->context;
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
      struct VdlFile *file = search_file (handle);
      if (file == 0)
	{
	  goto error;
	}
      scope = vdl_sort_deps_breadth_first (file);
      context = file->context;
    }
  struct VdlLookupResult result;
  
  result = vdl_lookup_with_scope (context, symbol, version, 0, scope);
  if (!result.found)
    {
      set_error ("Could not find requested symbol \"%s\"", symbol);
      goto error;
    }
  vdl_file_list_free (scope);
  futex_unlock (&g_vdl.futex);
  return (void*)(result.file->load_base + result.symbol->st_value);
 error:
  futex_unlock (&g_vdl.futex);
  return 0;
}
int vdl_dl_iterate_phdr_private (int (*callback) (struct dl_phdr_info *info,
						  size_t size, void *data),
				 void *data,
				 unsigned long caller)
{
  int ret = 0;
  futex_lock (&g_vdl.futex);
  struct VdlFile *file = caller_to_file (caller);

  // report all objects within the global scope/context of the caller
  struct VdlFileList *cur;
  for (cur = file->context->global_scope; 
       cur != 0; cur = cur->next)
    {
      struct dl_phdr_info info;
      ElfW(Ehdr) *header = (ElfW(Ehdr) *)cur->item->ro_map.mem_start_align;
      ElfW(Phdr) *phdr = (ElfW(Phdr) *) (cur->item->ro_map.mem_start_align + header->e_phoff);
      info.dlpi_addr = cur->item->load_base;
      info.dlpi_name = cur->item->name;
      info.dlpi_phdr = phdr;
      info.dlpi_phnum = header->e_phnum;
      info.dlpi_adds = g_vdl.n_added;
      info.dlpi_subs = g_vdl.n_removed;
      if (cur->item->has_tls)
	{
	  info.dlpi_tls_modid = cur->item->tls_index;
	  info.dlpi_tls_data = (void*)vdl_tls_get_addr_fast (cur->item->tls_index, 0);
	}
      else
	{
	  info.dlpi_tls_modid = 0;
	  info.dlpi_tls_data = 0;
	}
      futex_unlock (&g_vdl.futex);
      ret = callback (&info, sizeof (struct dl_phdr_info), data);
      futex_lock (&g_vdl.futex);
      if (ret != 0)
	{
	  break;
	}
    }
  futex_unlock (&g_vdl.futex);
  return ret;
}
void *vdl_dlmopen_private (Lmid_t lmid, const char *filename, int flag)
{
  futex_lock (&g_vdl.futex);
  struct VdlContext *context;
  if (lmid == LM_ID_BASE)
    {
      context = g_vdl.contexts;
    }
  else if (lmid == LM_ID_NEWLM)
    {
      context = vdl_context_new (g_vdl.contexts->argc,
				 (const char **)g_vdl.contexts->argv,
				 (const char **)g_vdl.contexts->envp);
    }
  else
    {
      context = (struct VdlContext *) lmid;
      if (search_context (context) == 0)
	{
	  return 0;
	}
    }
  void *handle = dlopen_with_context (context, filename, flag);
  futex_unlock (&g_vdl.futex);
  return handle;
}
int vdl_dlinfo_private (void *handle, int request, void *p)
{
  futex_lock (&g_vdl.futex);
  struct VdlFile *file = search_file (handle);
  if (file == 0)
    {
      goto error;
    }
  if (request == RTLD_DI_LMID)
    {
      Lmid_t *plmid = (Lmid_t*)p;
      *plmid = (Lmid_t)file->context;
    }
  else if (request == RTLD_DI_LINKMAP)
    {
      struct link_map **pmap = (struct link_map **)p;
      *pmap = (struct link_map*) file;
    }
  else if (request == RTLD_DI_TLS_MODID)
    {
      size_t *pmodid = (size_t *)p;
      if (file->has_tls)
	{
	  *pmodid = file->tls_index;
	}
      else
	{
	  *pmodid = 0;
	}
    }
  else if (request == RTLD_DI_TLS_DATA)
    {
      void **ptls = (void**)p;
      if (file->has_tls)
	{
	  *ptls = (void*)vdl_tls_get_addr_fast (file->tls_index, 0);
	}
      else
	{
	  *ptls = 0;
	}
    }
  else
    {
      set_error ("dlinfo: unsupported request=%u", request);
      goto error;
    }
  
  futex_unlock (&g_vdl.futex);
  return 0;
 error:
  futex_unlock (&g_vdl.futex);
  return -1;
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
EXPORT int vdl_dladdr_public (const void *addr, Dl_info *info)
{
  return vdl_dladdr_private (addr, info);
}
EXPORT void *vdl_dlvsym_public (void *handle, const char *symbol, const char *version, unsigned long caller)
{
  return vdl_dlvsym_private (handle, symbol, version, caller);
}
EXPORT int vdl_dl_iterate_phdr_public (int (*callback) (struct dl_phdr_info *info,
							size_t size, void *data),
				       void *data)
{
  return vdl_dl_iterate_phdr_private (callback, data, RETURN_ADDRESS);
}
EXPORT int vdl_dlinfo_public (void *handle, int request, void *p)
{
  return vdl_dlinfo_private (handle, request, p);
}
EXPORT void *vdl_dlmopen_public (Lmid_t lmid, const char *filename, int flag)
{
  return vdl_dlmopen_private (lmid, filename, flag);
}
EXPORT Lmid_t vdl_dl_lmid_new_public (int argc, const char **argv, const char **envp)
{
  futex_lock (&g_vdl.futex);
  struct VdlContext *context = vdl_context_new (argc, argv, envp);
  Lmid_t lmid = (Lmid_t) context;
  futex_unlock (&g_vdl.futex);
  return lmid;
}
EXPORT int vdl_dl_add_callback_public (Lmid_t lmid, 
				       void (*cb) (void *handle, int event, void *context),
				       void *cb_context)
{
  futex_lock (&g_vdl.futex);
  struct VdlContext *context = (struct VdlContext *)lmid;
  if (search_context (context) == 0)
    {
      goto error;
    }
  vdl_context_add_callback (context, 
			    (void (*) (void *, enum VdlEvent, void *))cb, 
			    cb_context);
  futex_unlock (&g_vdl.futex);
  return 0;
 error:
  futex_unlock (&g_vdl.futex);
  return -1;
}
EXPORT int vdl_dl_add_lib_remap_public (Lmid_t lmid, const char *src, const char *dst)
{
  futex_lock (&g_vdl.futex);
  struct VdlContext *context = (struct VdlContext *)lmid;
  if (search_context (context) == 0)
    {
      goto error;
    }
  vdl_context_add_lib_remap (context, src, dst);
  futex_unlock (&g_vdl.futex);
  return 0;
 error:
  futex_unlock (&g_vdl.futex);
  return -1;
}
EXPORT int vdl_dl_add_symbol_remap_public (Lmid_t lmid,
					    const char *src_name, 
					    const char *src_ver_name, 
					    const char *src_ver_filename, 
					    const char *dst_name,
					    const char *dst_ver_name,
					    const char *dst_ver_filename)
{
  futex_lock (&g_vdl.futex);
  struct VdlContext *context = (struct VdlContext *)lmid;
  if (search_context (context) == 0)
    {
      goto error;
    }
  vdl_context_add_symbol_remap (context,
				src_name, src_ver_name, src_ver_filename,
				dst_name, dst_ver_name, dst_ver_filename);
  futex_unlock (&g_vdl.futex);
  return 0;
 error:
  futex_unlock (&g_vdl.futex);
  return -1;
}
