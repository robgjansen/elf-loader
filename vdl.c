#include "vdl.h"
#include "alloc.h"
#include "system.h"
#include "avprintf-cb.h"
#include "vdl-utils.h"
#include "vdl-log.h"
#include "vdl-file-list.h"
#include "vdl-gc.h"
#include "machine.h"
#include <stdarg.h>
#include <unistd.h>
#include <sys/mman.h>

struct Vdl g_vdl;

static void
print_debug_map (const char *filename, const char *type,
		 struct VdlFileMap map)
{
  VDL_LOG_DEBUG ("%s %s file=0x%llx/0x%llx mem=0x%llx/0x%llx zero=0x%llx/0x%llx anon=0x%llx/0x%llx\n",
		 filename, type,
		 map.file_start_align, map.file_size_align,
		 map.mem_start_align, map.mem_size_align,
		 map.mem_zero_start, map.mem_zero_size,
		 map.mem_anon_start_align, map.mem_anon_size_align);
}
static unsigned long 
get_total_mapping_size (struct VdlFileMap ro_map, struct VdlFileMap rw_map)
{
  unsigned long end = ro_map.mem_start_align + ro_map.mem_size_align;
  end = vdl_utils_max (end, rw_map.mem_start_align + rw_map.mem_size_align);
  unsigned long mapping_size = end - ro_map.mem_start_align;
  return mapping_size;
}


void vdl_context_add_lib_remap (struct VdlContext *context, const char *src, const char *dst)
{
  int old_n_entries = context->n_lib_remaps;
  struct VdlContextLibRemapEntry *old_entries = context->lib_remaps;
  struct VdlContextLibRemapEntry *new_entries =  (struct VdlContextLibRemapEntry *)
    vdl_utils_malloc (sizeof (struct VdlContextLibRemapEntry)*(old_n_entries + 1));
  if (old_entries != 0)
    {
      vdl_utils_memcpy (new_entries, old_entries, sizeof (struct VdlContextLibRemapEntry)*(old_n_entries));
      vdl_utils_free (old_entries, sizeof (struct VdlContextLibRemapEntry)*old_n_entries);
    }
  context->lib_remaps = new_entries;
  new_entries[old_n_entries].src = vdl_utils_strdup (src);
  new_entries[old_n_entries].dst = vdl_utils_strdup (dst);
  context->n_lib_remaps++;
}

void vdl_context_add_symbol_remap (struct VdlContext *context, 
				   const char *src_name, 
				   const char *src_ver_name, 
				   const char *src_ver_filename, 
				   const char *dst_name,
				   const char *dst_ver_name,
				   const char *dst_ver_filename)
{
  int old_n_entries = context->n_symbol_remaps;
  struct VdlContextSymbolRemapEntry *old_entries = context->symbol_remaps;
  struct VdlContextSymbolRemapEntry *new_entries =  (struct VdlContextSymbolRemapEntry *)
    vdl_utils_malloc (sizeof (struct VdlContextSymbolRemapEntry)*(old_n_entries + 1));
  if (old_entries != 0)
    {
      vdl_utils_memcpy (new_entries, old_entries, 
			sizeof (struct VdlContextSymbolRemapEntry)*(old_n_entries));
      vdl_utils_free (old_entries, sizeof (struct VdlContextSymbolRemapEntry)*old_n_entries);
    }
  context->symbol_remaps = new_entries;
  new_entries[old_n_entries].src_name = vdl_utils_strdup (src_name);
  new_entries[old_n_entries].src_ver_name = vdl_utils_strdup (src_ver_name);
  new_entries[old_n_entries].src_ver_filename = vdl_utils_strdup (src_ver_filename);
  new_entries[old_n_entries].dst_name = vdl_utils_strdup (dst_name);
  new_entries[old_n_entries].dst_ver_name = vdl_utils_strdup (dst_ver_name);
  new_entries[old_n_entries].dst_ver_filename = vdl_utils_strdup (dst_ver_filename);
  context->n_symbol_remaps++;
}
void vdl_context_add_callback (struct VdlContext *context,
			       void (*cb) (void *handle, enum VdlEvent event, void *context),
			       void *cb_context)
{
  int old_n_entries = context->n_event_callbacks;
  struct VdlContextCallbackEntry *old_entries = context->event_callbacks;
  struct VdlContextCallbackEntry *new_entries =  (struct VdlContextCallbackEntry *)
    vdl_utils_malloc (sizeof (struct VdlContextCallbackEntry)*(old_n_entries + 1));
  if (old_entries != 0)
    {
      vdl_utils_memcpy (new_entries, old_entries, 
			sizeof (struct VdlContextCallbackEntry)*(old_n_entries));
      vdl_utils_free (old_entries, sizeof (struct VdlContextCallbackEntry)*old_n_entries);
    }
  context->event_callbacks = new_entries;
  new_entries[old_n_entries].fn = cb;
  new_entries[old_n_entries].context = cb_context;
  context->n_event_callbacks++;
}
void vdl_context_notify (struct VdlContext *context,
			 struct VdlFile *file,
			 enum VdlEvent event)
{
  int i;
  for (i = 0; i < context->n_event_callbacks; i++)
    {
      struct VdlContextCallbackEntry *entry = &context->event_callbacks[i];
      entry->fn (file, event, entry->context);
    }
}


const char *
vdl_context_lib_remap (const struct VdlContext *context, const char *name)
{
  VDL_LOG_FUNCTION ("name=%s", name);
  struct VdlContextLibRemapEntry *entries = context->lib_remaps;
  int nentries = context->n_lib_remaps;
  int i; 
  for (i = 0; i < nentries; i++)
    {
      if (vdl_utils_strisequal (entries[i].src, name))
	{
	  return entries[i].dst;
	}
    }
  return name;
}
void
vdl_context_symbol_remap (const struct VdlContext *context, 
			  const char **name, const char **ver_name, const char **ver_filename)
{
  VDL_LOG_FUNCTION ("name=%s, ver_name=%s, ver_filename=%s", *name, 
		    (ver_name != 0 && *ver_name != 0)?*ver_name:"", 
		    (ver_filename != 0 && *ver_filename != 0)?*ver_filename:"");
  struct VdlContextSymbolRemapEntry *entries = context->symbol_remaps;
  int nentries = context->n_symbol_remaps;
  int i; 
  for (i = 0; i < nentries; i++)
    {
      if (!vdl_utils_strisequal (entries[i].src_name, *name))
	{
	  continue;
	}
      else if (entries[i].src_ver_name == 0)
	{
	  goto match;
	}
      else if (*ver_name == 0)
	{
	  continue;
	}
      else if (!vdl_utils_strisequal (entries[i].src_ver_name, *ver_name))
	{
	  continue;
	}
      else if (entries[i].src_ver_filename == 0)
	{
	  goto match;
	}
      else if (*ver_filename == 0)
	{
	  continue;
	}
      else if (vdl_utils_strisequal (entries[i].src_ver_filename, *ver_filename))
	{
	  goto match;
	}
    }
  return;
 match:
  *name = entries[i].dst_name;
  if (ver_name != 0)
    {
      *ver_name = entries[i].dst_ver_name;
    }
  if (ver_filename != 0)
    {
      *ver_filename = entries[i].dst_ver_filename;
    }
  return;
}

struct VdlContext *vdl_context_new (int argc, char **argv, char **envp)
{
  VDL_LOG_FUNCTION ("argc=%d", argc);

  struct VdlContext *context = vdl_utils_new (struct VdlContext);
  context->global_scope = 0;
  // prepend to context list.
  if (g_vdl.contexts != 0)
    {
      g_vdl.contexts->prev = context;
    }
  context->next = g_vdl.contexts;
  context->prev = 0;
  context->n_lib_remaps = 0;
  context->lib_remaps = 0;
  context->n_symbol_remaps = 0;
  context->symbol_remaps = 0;
  context->n_event_callbacks = 0;
  context->event_callbacks = 0;
  g_vdl.contexts = context;
  // keep a reference to argc, argv and envp.
  context->argc = argc;
  context->argv = argv;
  context->envp = envp;

  // these are hardcoded name conversions to ensure that
  // we can replace the libc loader.
  vdl_context_add_lib_remap (context, "/lib/ld-linux.so.2", "ldso");
  vdl_context_add_lib_remap (context, "/lib64/ld-linux-x86-64.so.2", "ldso");
  vdl_context_add_lib_remap (context, "ld-linux.so.2", "ldso");
  vdl_context_add_lib_remap (context, "ld-linux-x86-64.so.2", "ldso");
  vdl_context_add_lib_remap (context, "libdl.so.2", "libvdl.so");
  vdl_context_add_symbol_remap (context, 
				"dl_iterate_phdr", 0, 0,
				"vdl_dl_iterate_phdr_public", "VDL_DL", "ldso");

  return context;
}
void 
vdl_context_delete (struct VdlContext *context)
{
  VDL_LOG_FUNCTION ("context=%p", context);
  // get rid of associated global scope
  vdl_file_list_free (context->global_scope);
  context->global_scope = 0;
  // unlink from main context list
  if (context->prev != 0)
    {
      context->prev->next = context->next;
    }
  if (context->next != 0)
    {
      context->next->prev = context->prev;
    }
  context->prev = 0;
  context->next = 0;
  context->argc = 0;
  context->argv = 0;
  context->envp = 0;

  // delete lib remap entries
  int i;
  for (i = 0; i < context->n_lib_remaps; i++)
    {
      vdl_utils_strfree (context->lib_remaps[i].src);
      vdl_utils_strfree (context->lib_remaps[i].dst);
    }
  vdl_utils_free (context->lib_remaps, context->n_lib_remaps*sizeof(struct VdlContextLibRemapEntry));

  // delete symbol remap entries
  for (i = 0; i < context->n_symbol_remaps; i++)
    {
      struct VdlContextSymbolRemapEntry *entry = &context->symbol_remaps[i];
      vdl_utils_strfree (entry->src_name);
      vdl_utils_strfree (entry->src_ver_name);
      vdl_utils_strfree (entry->src_ver_filename);
      vdl_utils_strfree (entry->dst_name);
      vdl_utils_strfree (entry->dst_ver_name);
      vdl_utils_strfree (entry->dst_ver_filename);
    }
  vdl_utils_free (context->symbol_remaps, context->n_symbol_remaps*sizeof(struct VdlContextSymbolRemapEntry));

  // delete event callback entries
    vdl_utils_free (context->event_callbacks, context->n_event_callbacks*sizeof(struct VdlContextCallbackEntry));

  // finally, delete context itself
  vdl_utils_delete (context);
}
static void
file_append (struct VdlFile *item)
{
  VDL_LOG_FUNCTION ("item=\"%s\"", item->name);
  if (g_vdl.link_map == 0)
    {
      g_vdl.link_map = item;
      return;
    }
  struct VdlFile *cur = g_vdl.link_map;
  while (cur->next != 0)
    {
      cur = cur->next;
    }
  cur->next = item;
  item->prev = cur;
  item->next = 0;
  g_vdl.n_added++;
}
static struct VdlFileMap
file_map_add_load_base (struct VdlFileMap map, unsigned long load_base)
{
  struct VdlFileMap result = map;
  result.mem_start_align += load_base;
  result.mem_zero_start += load_base;
  result.mem_anon_start_align += load_base;
  return result;
}
struct VdlFile *vdl_file_new (unsigned long load_base,
			      const struct VdlFileInfo *info,
			      const char *filename, 
			      const char *name,
			      struct VdlContext *context)
{
  struct VdlFile *file = vdl_utils_new (struct VdlFile);

  file->load_base = load_base;
  file->filename = vdl_utils_strdup (filename);
  file->dynamic = info->dynamic + load_base;
  file->next = 0;
  file->prev = 0;
  file->is_main_namespace = (context == g_vdl.contexts)?0:1;
  file->count = 0;
  file->context = context;
  file->st_dev = 0;
  file->st_ino = 0;
  file->ro_map = file_map_add_load_base (info->ro_map, load_base);
  file->rw_map = file_map_add_load_base (info->rw_map, load_base);
  file->deps_initialized = 0;
  file->tls_initialized = 0;
  file->init_called = 0;
  file->fini_called = 0;
  file->reloced = 0;
  file->patched = 0;
  file->is_executable = 0;
  // no need to initialize gc_color because it is always 
  // initialized when needed by vdl_gc
  file->gc_symbols_resolved_in = 0;
  file->lookup_type = LOOKUP_GLOBAL_LOCAL;
  file->local_scope = 0;
  file->deps = 0;
  file->name = vdl_utils_strdup (name);
  file->depth = 0;

  file_append (file);

  return file;
}

static void
file_delete (struct VdlFile *file)
{
  unsigned long mapping_size = get_total_mapping_size (file->ro_map, file->rw_map);
  int status = system_munmap ((void*)file->ro_map.mem_start_align, mapping_size);
  if (status == -1)
    {
      VDL_LOG_ERROR ("unable to unmap \"%s\"\n", file->filename);
    }

  file->next = 0;
  file->prev = 0;
  vdl_file_list_free (file->deps);
  file->deps = 0;
  vdl_file_list_free (file->local_scope);
  file->local_scope = 0;
  vdl_file_list_free (file->gc_symbols_resolved_in);
  file->gc_symbols_resolved_in = 0;
  vdl_utils_strfree (file->name);
  file->name = 0;
  vdl_utils_strfree (file->filename);
  file->filename = 0;
  file->context = 0;
  vdl_utils_delete (file);
  g_vdl.n_removed++;
}

void vdl_files_delete (struct VdlFileList *files)
{
  struct VdlFileList *cur;
  for (cur = files; cur != 0; cur = cur->next)
    {
      file_delete (cur->item);
    }
}


ElfW(Dyn) *
vdl_file_get_dynamic (const struct VdlFile *file, unsigned long tag)
{
  ElfW(Dyn) *cur = (ElfW(Dyn)*)file->dynamic;
  while (cur->d_tag != DT_NULL)
    {
      if (cur->d_tag == tag)
	{
	  return cur;
	}
      cur++;
    }
  return 0;
}

unsigned long
vdl_file_get_dynamic_v (const struct VdlFile *file, unsigned long tag)
{
  ElfW(Dyn) *dyn = vdl_file_get_dynamic (file, tag);
  if (dyn == 0)
    {
      return 0;
    }
  return dyn->d_un.d_val;
}

unsigned long
vdl_file_get_dynamic_p (const struct VdlFile *file, unsigned long tag)
{
  ElfW(Dyn) *dyn = vdl_file_get_dynamic (file, tag);
  if (dyn == 0)
    {
      return 0;
    }
  return file->load_base + dyn->d_un.d_ptr;
}

static struct VdlStringList *vdl_file_get_dt_needed (struct VdlFile *file)
{
  VDL_LOG_FUNCTION ("file=%s", file->name);
  unsigned long dt_strtab = vdl_file_get_dynamic_p (file, DT_STRTAB);
  if (dt_strtab == 0)
    {
      return 0;
    }
  ElfW(Dyn) *dynamic = (ElfW(Dyn)*)file->dynamic;
  ElfW(Dyn)*cur;
  struct VdlStringList *ret = 0;
  for (cur = dynamic; cur->d_tag != DT_NULL; cur++)
    {
      if (cur->d_tag == DT_NEEDED)
	{
	  const char *str = (const char *)(dt_strtab + cur->d_un.d_val);
	  struct VdlStringList *tmp = vdl_utils_new (struct VdlStringList);
	  tmp->str = vdl_utils_strdup (str);
	  tmp->next = ret;
	  ret = tmp;
	  VDL_LOG_DEBUG ("needed=%s\n", str);
	}
    }
  return vdl_utils_str_list_reverse (ret);
}
static char *
replace_magic (char *filename)
{
  char *lib = vdl_utils_strfind (filename, "$LIB");
  if (lib != 0)
    {
      char saved = lib[0];
      lib[0] = 0;
      char *new_filename = vdl_utils_strconcat (filename,
						machine_get_lib (),
						lib+4, 0);
      lib[0] = saved;
      vdl_utils_strfree (filename);
      VDL_LOG_DEBUG ("magic %s", new_filename);
      return new_filename;
    }
  return filename;
}
static char *
do_search (const char *name, 
	   struct VdlStringList *list)
{
  struct VdlStringList *cur;
  for (cur = list; cur != 0; cur = cur->next)
    {
      char *fullname = vdl_utils_strconcat (cur->str, "/", name, 0);
      fullname = replace_magic (fullname);
      if (vdl_utils_exists (fullname))
	{
	  return fullname;
	}
      vdl_utils_strfree (fullname);
    }
  return 0;
}
char *vdl_search_filename (const char *name, 
			   struct VdlStringList *rpath,
			   struct VdlStringList *runpath)
{
  VDL_LOG_FUNCTION ("name=%s", name);
  if (name[0] != '/')
    {
      // if the filename we are looking for does not start with a '/',
      // it is a relative filename so, we can try to locate it with the
      // search dirs.
      char *fullname = 0;
      if (runpath != 0)
	{
	  fullname = do_search (name, runpath);
	}
      else if (rpath != 0)
	{
	  fullname = do_search (name, rpath);
	}
      if (fullname != 0)
	{
	  return fullname;
	}
      fullname = do_search (name, g_vdl.search_dirs);
      if (fullname != 0)
	{
	  return fullname;
	}
    }
  char *realname = replace_magic (vdl_utils_strdup (name));
  if (vdl_utils_exists (realname))
    {
      return realname;
    }
  vdl_utils_strfree (realname);
  return 0;
}
static void
file_map_do (struct VdlFileMap map,
	     int fd, int prot,
	     unsigned long load_base)
{
  VDL_LOG_FUNCTION ("fd=0x%x, prot=0x%x, load_base=0x%lx", fd, prot, load_base);
  int int_result;
  unsigned long address;
  // Now, map again the area at the right location.
  address = (unsigned long) system_mmap ((void*)load_base + map.mem_start_align,
					 map.mem_size_align,
					 prot,
					 MAP_PRIVATE | MAP_FIXED,
					 fd, map.file_start_align);
  VDL_LOG_ASSERT (address == load_base + map.mem_start_align, "Unable to perform remapping");
  if (map.mem_zero_size != 0)
    {
      // make sure that the last partly zero page is PROT_WRITE
      if (!(prot & PROT_WRITE))
	{
	  int_result = system_mprotect ((void *)vdl_utils_align_down (load_base + map.mem_zero_start,
								      system_getpagesize ()),
					system_getpagesize (),
					prot | PROT_WRITE);
	  VDL_LOG_ASSERT (int_result == 0, "Unable to change protection to zeroify last page");
	}
      // zero the end of map
      vdl_utils_memset ((void*)(load_base + map.mem_zero_start), 0, map.mem_zero_size);
      // now, restore the previous protection if needed
      if (!(prot & PROT_WRITE))
	{
	  int_result = system_mprotect ((void*)vdl_utils_align_down (load_base + map.mem_zero_start,
								     system_getpagesize ()),
					system_getpagesize (),
					prot);
	  VDL_LOG_ASSERT (int_result == 0, "Unable to restore protection from last page of mapping");
	}
    }

  if (map.mem_anon_size_align > 0)
    {
      // now, unmap the extended file mapping for the zero pages.
      int_result = system_munmap ((void*)(load_base + map.mem_anon_start_align),
				  map.mem_anon_size_align);
      VDL_LOG_ASSERT (int_result == 0, "again, munmap can't possibly fail here");
      // then, map zero pages.
      address = (unsigned long) system_mmap ((void*)load_base + map.mem_anon_start_align,
					     map.mem_anon_size_align, 
					     prot,
					     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
					     -1, 0);
      VDL_LOG_ASSERT (address != -1, "Unable to map zero pages\n");
    }
}
struct VdlFile *vdl_file_map_single (struct VdlContext *context, 
				     const char *filename, 
				     const char *name)
{
  VDL_LOG_FUNCTION ("context=%p, filename=%s, name=%s", context, filename, name);
  ElfW(Ehdr) header;
  ElfW(Phdr) *phdr = 0;
  size_t bytes_read;
  unsigned long mapping_start = 0;
  unsigned long mapping_size = 0;
  int fd = -1;
  struct VdlFileInfo info;

  fd = system_open_ro (filename);
  if (fd == -1)
    {
      VDL_LOG_ERROR ("Could not open ro target file: %s\n", filename);
      goto error;
    }

  bytes_read = system_read (fd, &header, sizeof (header));
  if (bytes_read == -1 || bytes_read != sizeof (header))
    {
      VDL_LOG_ERROR ("Could not read header read=%d\n", bytes_read);
      goto error;
    }
  // check that the header size is correct
  if (header.e_ehsize != sizeof (header))
    {
      VDL_LOG_ERROR ("header size invalid, %d!=%d\n", header.e_ehsize, sizeof(header));
      goto error;
    }
  if (header.e_type != ET_EXEC &&
      header.e_type != ET_DYN)
    {
      VDL_LOG_ERROR ("header type unsupported, type=0x%x\n", header.e_type);
      goto error;
    }

  phdr = vdl_utils_malloc (header.e_phnum * header.e_phentsize);
  if (system_lseek (fd, header.e_phoff, SEEK_SET) == -1)
    {
      VDL_LOG_ERROR ("lseek failed to go to off=0x%x\n", header.e_phoff);
      goto error;
    }
  bytes_read = system_read (fd, phdr, header.e_phnum * header.e_phentsize);
  if (bytes_read == -1 || bytes_read != header.e_phnum * header.e_phentsize)
    {
      VDL_LOG_ERROR ("read failed: read=%d\n", bytes_read);
      goto error;
    }

  if (!vdl_get_file_info (header.e_phnum, phdr, &info))
    {
      VDL_LOG_ERROR ("unable to read data structure for %s\n", filename);
      goto error;
    }
  print_debug_map (filename, "ro", info.ro_map);
  print_debug_map (filename, "rw", info.rw_map);
  if (header.e_phoff <info.ro_map.file_start_align || 
      header.e_phoff + header.e_phnum * header.e_phentsize > 
      info.ro_map.file_start_align + info.ro_map.file_size_align)
    {
      VDL_LOG_ERROR ("program header table not included in ro map in %s\n", filename);
      goto error;
    }

  mapping_size = get_total_mapping_size (info.ro_map, info.rw_map);

  // If this is an executable, we try to map it exactly at its base address
  int fixed = (header.e_type == ET_EXEC)?MAP_FIXED:0;
  // We perform a single initial mmap to reserve all the virtual space we need
  // and, then, we map again portions of the space to make sure we get
  // the mappings we need
  mapping_start = (unsigned long) system_mmap ((void*)info.ro_map.mem_start_align,
					       mapping_size,
					       PROT_NONE,
					       MAP_PRIVATE | fixed,
					       fd, info.ro_map.file_start_align);
  if (mapping_start == -1)
    {
      VDL_LOG_ERROR ("Unable to allocate complete mapping for %s\n", filename);
      goto error;
    }
  VDL_LOG_ASSERT (!fixed || (fixed && mapping_start == info.ro_map.mem_start_align),
		  "We need a fixed address and we did not get it but this should have failed mmap");
  // calculate the offset between the start address we asked for and the one we got
  unsigned long load_base = mapping_start - info.ro_map.mem_start_align;

  // unmap the area before mapping it again.
  int int_result = system_munmap ((void*)mapping_start, mapping_size);
  VDL_LOG_ASSERT (int_result == 0, "munmap can't possibly fail here");

  // remap the portions we want.
  file_map_do (info.ro_map, fd, PROT_READ | PROT_EXEC, load_base);
  file_map_do (info.rw_map, fd, PROT_READ | PROT_WRITE, load_base);

  struct stat st_buf;
  if (system_fstat (filename, &st_buf) == -1)
    {
      VDL_LOG_ERROR ("Unable to stat file %s\n", filename);
      goto error;
    }

  struct VdlFile *file = vdl_file_new (load_base, &info, 
				       filename, name,
				       context);
  file->st_dev = st_buf.st_dev;
  file->st_ino = st_buf.st_ino;
  
  vdl_utils_free (phdr, header.e_phnum * header.e_phentsize);
  system_close (fd);

  vdl_context_notify (context, file, VDL_EVENT_MAPPED);

  return file;
error:
  if (fd >= 0)
    {
      system_close (fd);
    }
  if (phdr != 0)
    {
      vdl_utils_free (phdr, header.e_phnum * header.e_phentsize);
    }
  if (mapping_start != 0)
    {
      system_munmap ((void*)mapping_start, mapping_size);
    }
  return 0;
}


static struct VdlFile *
find_by_name (struct VdlContext *context,
	      const char *name)
{
  struct VdlFile *cur;
  for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
    {
      if (vdl_utils_strisequal (cur->name, name))
	{
	  return cur;
	}
    }
  return 0;
}
static struct VdlFile *
find_by_dev_ino (struct VdlContext *context, 
		 dev_t dev, ino_t ino)
{
  struct VdlFile *cur;
  for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
    {
      if (cur->context == context &&
	  cur->st_dev == dev &&
	  cur->st_ino == ino)
	{
	  return cur;
	}
    }
  return 0;
}

struct VdlFile *vdl_file_map_single_maybe (struct VdlContext *context,
					   const char *requested_filename,
					   struct VdlStringList *rpath,
					   struct VdlStringList *runpath,
					   struct VdlFileList **loaded)
{
  // Try to see if we don't have a hardcoded name conversion
  const char *name = vdl_context_lib_remap (context, requested_filename);
  // if the file is already mapped within this context,
  // get it and add it to deps
  struct VdlFile *file = find_by_name (context, name);
  if (file != 0)
    {
      return file;
    }
  // Search the file in the filesystem
  char *filename = vdl_search_filename (name, rpath, runpath);
  if (filename == 0)
    {
      VDL_LOG_ERROR ("Could not find %s\n", name);
      return 0;
    }
  // get information about file.
  struct stat buf;
  if (system_fstat (filename, &buf) == -1)
    {
      VDL_LOG_ERROR ("Cannot stat %s\n", filename);
      vdl_utils_strfree (filename);
      return 0;
    }
  // If you create a symlink to a binary and link to the
  // symlinks rather than the underlying binary, the DT_NEEDED
  // entries record different names for the same binary so,
  // the search by name above will fail. So, here, we stat
  // the file we found and check that none of the files
  // already mapped in the same context have the same ino/dev
  // pair. If they do, we don't need to re-map the file
  // and can re-use the previous map.
  file = find_by_dev_ino (context, buf.st_dev, buf.st_ino);
  if (file != 0)
    {
      vdl_utils_strfree (filename);
      return file;
    }
  // The file is really not yet mapped so, we have to map it
  file = vdl_file_map_single (context, filename, name);

  if (loaded != 0)
    {
      *loaded = vdl_file_list_append_one (*loaded, file);
    }

  vdl_utils_strfree (filename);

  return file;
}

static struct VdlStringList *
get_path (struct VdlFile *file, int type)
{
  unsigned long dt_path = vdl_file_get_dynamic_v (file, type);
  unsigned long dt_strtab = vdl_file_get_dynamic_p (file, DT_STRTAB);
  if (dt_path == 0 || dt_strtab == 0)
    {
      return 0;
    }
  const char *path = (const char *)(dt_strtab + dt_path);
  struct VdlStringList *list = vdl_utils_splitpath (path);
  return list;
}

int vdl_file_map_deps_recursive (struct VdlFile *item, 
				 struct VdlStringList *caller_rpath,
				 struct VdlFileList **loaded)
{
  VDL_LOG_FUNCTION ("file=%s", item->name);

  if (item->deps_initialized)
    {
      return 1;
    }
  item->deps_initialized = 1;

  struct VdlStringList *rpath = get_path (item, DT_RPATH);
  struct VdlStringList *runpath = get_path (item, DT_RUNPATH);
  rpath = vdl_utils_str_list_prepend (caller_rpath, rpath);

  // get list of deps for the input file.
  struct VdlStringList *dt_needed = vdl_file_get_dt_needed (item);

  // first, map each dep and accumulate them in deps variable
  struct VdlFileList *deps = 0;
  struct VdlStringList *cur;
  for (cur = dt_needed; cur != 0; cur = cur->next)
    {
      struct VdlFile *dep = vdl_file_map_single_maybe (item->context, cur->str,
						       rpath, runpath, loaded);
      if (dep == 0)
	{
	  // oops, failed to find the requested dt_needed
	  goto error;
	}
      dep->depth = vdl_utils_max (dep->depth, item->depth + 1);
      // add the new file to the list of dependencies
      deps = vdl_file_list_append_one (deps, dep);
    }

  // then, recursively map the deps of each dep.
  struct VdlFileList *dep;
  for (dep = deps; dep != 0; dep = dep->next)
    {
      if (!vdl_file_map_deps_recursive (dep->item, rpath, loaded))
	{
	  goto error;
	}
    }

  rpath = vdl_utils_str_list_split (rpath, caller_rpath);
  vdl_utils_str_list_free (rpath);
  vdl_utils_str_list_free (runpath);
  vdl_utils_str_list_free (dt_needed);

  // Finally, update the deps
  item->deps = deps;

  return 1;
 error:
  vdl_utils_str_list_free (dt_needed);
  rpath = vdl_utils_str_list_split (rpath, caller_rpath);
  vdl_utils_str_list_free (rpath);
  vdl_utils_str_list_free (runpath);
  vdl_file_list_free (deps);
  vdl_file_list_free (*loaded);
  *loaded = 0;
  return 0;
}
int vdl_file_map_deps (struct VdlFile *item, 
		       struct VdlFileList **loaded)
{
  return vdl_file_map_deps_recursive (item, 0, loaded);
}
static struct VdlFileMap 
pt_load_to_file_map (const ElfW(Phdr) *phdr)
{
  struct VdlFileMap map;
  unsigned long page_size = system_getpagesize ();
  VDL_LOG_ASSERT (phdr->p_type == PT_LOAD, "Invalid program header");
  map.file_start_align = vdl_utils_align_down (phdr->p_offset, page_size);
  map.file_size_align = vdl_utils_align_up (phdr->p_offset+phdr->p_filesz, 
					    page_size) - map.file_start_align;
  map.mem_start_align = vdl_utils_align_down (phdr->p_vaddr, page_size);
  map.mem_size_align = vdl_utils_align_up (phdr->p_vaddr+phdr->p_memsz, 
					   page_size) - map.mem_start_align;
  map.mem_anon_start_align = vdl_utils_align_up (phdr->p_vaddr + phdr->p_filesz,
						 page_size);
  map.mem_anon_size_align = vdl_utils_align_up (phdr->p_vaddr + phdr->p_memsz,
						page_size) - map.mem_anon_start_align;
  map.mem_zero_start = phdr->p_vaddr + phdr->p_filesz;
  if (map.mem_anon_size_align > 0)
    {
      map.mem_zero_size = map.mem_anon_start_align - map.mem_zero_start;
    }
  else
    {
      map.mem_zero_size = phdr->p_memsz - phdr->p_filesz;
    }
  return map;
}
int vdl_get_file_info (uint32_t phnum,
		       ElfW(Phdr) *phdr,
		       struct VdlFileInfo *info)
{
  VDL_LOG_FUNCTION ("phnum=%d, phdr=%p", phnum, phdr);
  ElfW(Phdr) *ro = 0, *rw = 0, *dynamic = 0, *cur;
  int i;
  for (i = 0, cur = phdr; i < phnum; i++, cur++)
    {
      if (cur->p_type == PT_LOAD)
	{
	  if (cur->p_flags & PF_W)
	    {
	      if (rw != 0)
		{
		  VDL_LOG_ERROR ("file has more than one RW PT_LOAD\n", 1);
		  goto error;
		}
	      rw = cur;
	    }
	  else
	    {
	      if (ro != 0)
		{
		  VDL_LOG_ERROR ("file has more than one RO PT_LOAD\n", 1);
		  goto error;
		}
	      ro = cur;
	    }
	}
      else if (cur->p_type == PT_DYNAMIC)
	{
	  dynamic = cur;
	}
    }
  if (ro == 0 || rw == 0 || dynamic == 0)
    {
      VDL_LOG_ERROR ("file is missing a critical program header "
		     "ro=0x%x, rw=0x%x, dynamic=0x%x\n",
		     ro, rw, dynamic);
      goto error;
    }
  if (ro->p_offset != 0 || ro->p_filesz < sizeof (ElfW(Ehdr)))
    {
      VDL_LOG_ERROR ("ro load area does not include elf header\n", 1);
      goto error;
    }
  if (ro->p_offset != 0)
    {
      VDL_LOG_ERROR ("The ro map should include the ELF header. off=0x%x\n",
		     ro->p_offset);
      goto error;
    }
  if (ro->p_align != rw->p_align)
    {
      VDL_LOG_ERROR ("something is fishy about the alignment constraints "
		     "ro_align=0x%x, rw_align=0x%x\n", ro->p_align, rw->p_align);
      goto error;
    }
  if (dynamic->p_offset < rw->p_offset || dynamic->p_filesz > rw->p_filesz)
    {
      VDL_LOG_ERROR ("dynamic not included in rw load\n", 1);
      goto error;
    }

  info->ro_map = pt_load_to_file_map (ro);
  info->rw_map = pt_load_to_file_map (rw);
  info->dynamic = dynamic->p_vaddr;

  return 1;
 error:
  return 0;
}

unsigned long vdl_file_get_entry_point (struct VdlFile *file)
{
  // This piece of code assumes that the ELF header starts at the
  // first byte of the ro map. This is verified in vdl_get_file_info
  // so we are safe with this assumption.
  ElfW(Ehdr) *header = (ElfW(Ehdr)*) file->ro_map.mem_start_align;
  return header->e_entry + file->load_base;
}
