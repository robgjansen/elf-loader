#include "vdl.h"
#include "alloc.h"
#include "system.h"
#include "avprintf-cb.h"
#include "vdl-utils.h"
#include "vdl-log.h"
#include "vdl-gc.h"
#include "vdl-mem.h"
#include "vdl-array.h"
#include "vdl-list.h"
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


void vdl_context_add_lib_remap (struct VdlContext *context, 
				const char *src, const char *dst)
{
  struct VdlContextLibRemapEntry entry;
  entry.src = vdl_utils_strdup (src);
  entry.dst = vdl_utils_strdup (dst);
  vdl_array_push_back (context->lib_remaps, entry);
}

void vdl_context_add_symbol_remap (struct VdlContext *context, 
				   const char *src_name, 
				   const char *src_ver_name, 
				   const char *src_ver_filename, 
				   const char *dst_name,
				   const char *dst_ver_name,
				   const char *dst_ver_filename)
{
  struct VdlContextSymbolRemapEntry entry;
  entry.src_name = vdl_utils_strdup (src_name);
  entry.src_ver_name = vdl_utils_strdup (src_ver_name);
  entry.src_ver_filename = vdl_utils_strdup (src_ver_filename);
  entry.dst_name = vdl_utils_strdup (dst_name);
  entry.dst_ver_name = vdl_utils_strdup (dst_ver_name);
  entry.dst_ver_filename = vdl_utils_strdup (dst_ver_filename);
  vdl_array_push_back (context->symbol_remaps, entry);
}
void vdl_context_add_callback (struct VdlContext *context,
			       void (*cb) (void *handle, enum VdlEvent event, void *context),
			       void *cb_context)
{
  struct VdlContextEventCallbackEntry entry;
  entry.fn = cb;
  entry.context = cb_context;
  vdl_array_push_back (context->event_callbacks, entry);
}
void vdl_context_notify (struct VdlContext *context,
			 struct VdlFile *file,
			 enum VdlEvent event)
{
  struct VdlContextEventCallbackEntry *i;
  for (i = vdl_array_begin (context->event_callbacks);
       i != vdl_array_end (context->event_callbacks);
       i++)
    {
      i->fn (file, event, i->context);
    }
}


const char *
vdl_context_lib_remap (const struct VdlContext *context, const char *name)
{
  VDL_LOG_FUNCTION ("name=%s", name);
  struct VdlContextLibRemapEntry *i;
  for (i = vdl_array_begin (context->lib_remaps);
       i != vdl_array_end (context->lib_remaps);
       i++)
    {
      if (vdl_utils_strisequal (i->src, name))
	{
	  return i->dst;
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
  struct VdlContextSymbolRemapEntry *i;
  for (i = vdl_array_begin (context->symbol_remaps); i != vdl_array_end (context->symbol_remaps); i++)
    {
      if (!vdl_utils_strisequal (i->src_name, *name))
	{
	  continue;
	}
      else if (i->src_ver_name == 0)
	{
	  goto match;
	}
      else if (*ver_name == 0)
	{
	  continue;
	}
      else if (!vdl_utils_strisequal (i->src_ver_name, *ver_name))
	{
	  continue;
	}
      else if (i->src_ver_filename == 0)
	{
	  goto match;
	}
      else if (*ver_filename == 0)
	{
	  continue;
	}
      else if (vdl_utils_strisequal (i->src_ver_filename, *ver_filename))
	{
	  goto match;
	}
    }
  return;
 match:
  *name = i->dst_name;
  if (ver_name != 0)
    {
      *ver_name = i->dst_ver_name;
    }
  if (ver_filename != 0)
    {
      *ver_filename = i->dst_ver_filename;
    }
  return;
}

struct VdlContext *vdl_context_new (int argc, char **argv, char **envp)
{
  VDL_LOG_FUNCTION ("argc=%d", argc);

  struct VdlContext *context = vdl_utils_new (struct VdlContext);
  context->global_scope = vdl_list_new ();

  vdl_list_push_back (g_vdl.contexts, context);

  context->lib_remaps = vdl_array_new (struct VdlContextLibRemapEntry);
  context->symbol_remaps = vdl_array_new (struct VdlContextSymbolRemapEntry);
  context->event_callbacks = vdl_array_new (struct VdlContextEventCallbackEntry);
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
  vdl_list_delete (context->global_scope);
  context->global_scope = 0;

  vdl_list_remove (g_vdl.contexts, context);
  context->argc = 0;
  context->argv = 0;
  context->envp = 0;

  {
    struct VdlContextLibRemapEntry *i;
    for (i = vdl_array_begin (context->lib_remaps); i != vdl_array_end (context->lib_remaps); i++)
      {
	vdl_utils_free (i->src);
	vdl_utils_free (i->dst);
      }
    vdl_array_delete (context->lib_remaps);
  }

  {
    struct VdlContextSymbolRemapEntry *i;
    for (i = vdl_array_begin (context->symbol_remaps); i != vdl_array_end (context->symbol_remaps); i++)
      {
	vdl_utils_free (i->src_name);
	vdl_utils_free (i->src_ver_name);
	vdl_utils_free (i->src_ver_filename);
	vdl_utils_free (i->dst_name);
	vdl_utils_free (i->dst_ver_name);
	vdl_utils_free (i->dst_ver_filename);	
      }
    vdl_array_delete (context->symbol_remaps);
  }

  vdl_array_delete (context->event_callbacks);

  context->lib_remaps = 0;
  context->symbol_remaps = 0;
  context->event_callbacks = 0;

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
void
file_remove (struct VdlFile *item)
{
  VDL_LOG_FUNCTION ("item=\"%s\"", item->name);

  // first, remove them from the global link_map
  struct VdlFile *next = item->next;
  struct VdlFile *prev = item->prev;
  item->next = 0;
  item->prev = 0;
  if (prev == 0)
    {
      g_vdl.link_map = next;
    }
  else
    {
      prev->next = next;
    }
  if (next != 0)
    {
      next->prev = prev;
    }
  g_vdl.n_removed++;
}

static uint32_t 
vdl_context_get_count (const struct VdlContext *context)
{
  // and count number of files in this context
  uint32_t context_count = 0;
  struct VdlFile *cur;
  for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
    {
      if (cur->context == context)
	{
	  context_count++;
	}
    }
  return context_count;
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
  file->is_main_namespace = (context == vdl_list_front (g_vdl.contexts))?0:1;
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
  file->gc_symbols_resolved_in = vdl_list_new ();
  file->lookup_type = LOOKUP_GLOBAL_LOCAL;
  file->local_scope = vdl_list_new ();
  file->deps = vdl_list_new ();
  file->name = vdl_utils_strdup (name);
  file->depth = 0;

  file_append (file);

  return file;
}

static void
vdl_file_delete (struct VdlFile *file, bool mapping)
{
  if (mapping)
    {
      unsigned long mapping_size = get_total_mapping_size (file->ro_map, file->rw_map);
      int status = system_munmap ((void*)file->ro_map.mem_start_align, mapping_size);
      if (status == -1)
	{
	  VDL_LOG_ERROR ("unable to unmap \"%s\"\n", file->filename);
	}
    }

  // remove from global linkmap
  file_remove (file);

  if (vdl_context_get_count (file->context) == 0)
    {
      vdl_context_delete (file->context);
    }

  vdl_list_delete (file->deps);
  vdl_list_delete (file->local_scope);
  vdl_list_delete (file->gc_symbols_resolved_in);
  vdl_utils_free (file->name);
  vdl_utils_free (file->filename);

  file->deps = 0;
  file->local_scope = 0;
  file->gc_symbols_resolved_in = 0;
  file->name = 0;
  file->filename = 0;
  file->context = 0;

  vdl_utils_delete (file);
}

void vdl_files_delete (struct VdlList *files, bool mapping)
{
  void **i;
  for (i = vdl_list_begin (files); i != vdl_list_end (files); i = vdl_list_next (i))
    {
      vdl_file_delete (*i, mapping);
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

static struct VdlList *vdl_file_get_dt_needed (struct VdlFile *file)
{
  VDL_LOG_FUNCTION ("file=%s", file->name);
  struct VdlList *list = vdl_list_new ();
  unsigned long dt_strtab = vdl_file_get_dynamic_p (file, DT_STRTAB);
  if (dt_strtab == 0)
    {
      return list;
    }
  ElfW(Dyn) *dynamic = (ElfW(Dyn)*)file->dynamic;
  ElfW(Dyn)*cur;
  for (cur = dynamic; cur->d_tag != DT_NULL; cur++)
    {
      if (cur->d_tag == DT_NEEDED)
	{
	  const char *str = (const char *)(dt_strtab + cur->d_un.d_val);
	  VDL_LOG_DEBUG ("needed=%s\n", str);
	  vdl_list_push_back (list, vdl_utils_strdup (str));
	}
    }
  return list;
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
      vdl_utils_free (filename);
      VDL_LOG_DEBUG ("magic %s", new_filename);
      return new_filename;
    }
  return filename;
}
static char *
do_search (const char *name, 
	   struct VdlList *list)
{
  void **i;
  for (i = vdl_list_begin (list); i != vdl_list_end (list); i = vdl_list_next (i))
    {
      char *fullname = vdl_utils_strconcat (*i, "/", name, 0);
      fullname = replace_magic (fullname);
      if (vdl_utils_exists (fullname))
	{
	  return fullname;
	}
      vdl_utils_free (fullname);
    }
  return 0;
}
char *vdl_search_filename (const char *name, 
			   struct VdlList *rpath,
			   struct VdlList *runpath)
{
  VDL_LOG_FUNCTION ("name=%s", name);
  if (name[0] != '/')
    {
      // if the filename we are looking for does not start with a '/',
      // it is a relative filename so, we can try to locate it with the
      // search dirs.
      char *fullname = 0;
      if (!vdl_list_empty (runpath))
	{
	  fullname = do_search (name, runpath);
	}
      else
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
  vdl_utils_free (realname);
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
      vdl_memset ((void*)(load_base + map.mem_zero_start), 0, map.mem_zero_size);
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
  
  vdl_utils_free (phdr);
  system_close (fd);

  vdl_context_notify (context, file, VDL_EVENT_MAPPED);

  return file;
error:
  if (fd >= 0)
    {
      system_close (fd);
    }
  vdl_utils_free (phdr);
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
      if (!vdl_utils_strisequal (cur->name, name))
	{
	  continue;
	}
      if (cur->context == context)
	{
	  return cur;
	}
      if (vdl_utils_strisequal (name, "ldso"))
	{
	  // we want to make sure that all contexts
	  // reuse the same ldso.
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
					   struct VdlList *rpath,
					   struct VdlList *runpath,
					   struct VdlList *loaded)
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
      vdl_utils_free (filename);
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
      vdl_utils_free (filename);
      return file;
    }
  // The file is really not yet mapped so, we have to map it
  file = vdl_file_map_single (context, filename, name);
  VDL_LOG_ASSERT (file != 0, "The file should be there so this should not fail.");

  vdl_list_push_back (loaded, file);

  vdl_utils_free (filename);

  return file;
}

static struct VdlList *
get_path (struct VdlFile *file, int type)
{
  unsigned long dt_path = vdl_file_get_dynamic_v (file, type);
  unsigned long dt_strtab = vdl_file_get_dynamic_p (file, DT_STRTAB);
  if (dt_path == 0 || dt_strtab == 0)
    {
      return vdl_list_new ();
    }
  const char *path = (const char *)(dt_strtab + dt_path);
  struct VdlList *list = vdl_utils_splitpath (path);
  return list;
}

int vdl_file_map_deps_recursive (struct VdlFile *item, 
				 struct VdlList *caller_rpath,
				 struct VdlList *loaded)
{
  VDL_LOG_FUNCTION ("file=%s", item->name);

  if (item->deps_initialized)
    {
      return 1;
    }
  item->deps_initialized = 1;

  struct VdlList *rpath = get_path (item, DT_RPATH);
  struct VdlList *runpath = get_path (item, DT_RUNPATH);
  struct VdlList *current_rpath = vdl_list_copy (rpath);
  vdl_list_insert_range (current_rpath, vdl_list_end (current_rpath),
			 vdl_list_begin (caller_rpath),
			 vdl_list_end (caller_rpath));

  // get list of deps for the input file.
  struct VdlList *dt_needed = vdl_file_get_dt_needed (item);

  // first, map each dep and accumulate them in deps variable
  void **cur;
  for (cur = vdl_list_begin (dt_needed);
       cur != vdl_list_end (dt_needed);
       cur = vdl_list_next (cur))
    {
      struct VdlFile *dep = vdl_file_map_single_maybe (item->context, *cur,
						       current_rpath, runpath, loaded);
      if (dep == 0)
	{
	  // oops, failed to find the requested dt_needed
	  goto error;
	}
      dep->depth = vdl_utils_max (dep->depth, item->depth + 1);
      // add the new file to the list of dependencies
      vdl_list_push_back (item->deps, dep);
    }

  // then, recursively map the deps of each dep.
  for (cur = vdl_list_begin (item->deps); 
       cur != vdl_list_end (item->deps); 
       cur = vdl_list_next (cur))
    {
      if (!vdl_file_map_deps_recursive (*cur, rpath, loaded))
	{
	  goto error;
	}
    }

  vdl_utils_str_list_delete (rpath);
  vdl_list_delete (current_rpath);
  vdl_utils_str_list_delete (runpath);
  vdl_utils_str_list_delete (dt_needed);

  return 1;
 error:
  vdl_utils_str_list_delete (dt_needed);
  vdl_utils_str_list_delete (rpath);
  vdl_list_delete (current_rpath);
  vdl_utils_str_list_delete (runpath);
  vdl_list_clear (loaded);
  return 0;
}
int vdl_file_map_deps (struct VdlFile *item, 
		       struct VdlList *loaded)
{
  struct VdlList *rpath = vdl_list_new ();
  int status = vdl_file_map_deps_recursive (item, rpath, loaded);
  vdl_list_delete (rpath);
  return status;
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
