#include "vdl-map.h"
#include "vdl-log.h"
#include "vdl-alloc.h"
#include "vdl-context.h"
#include "vdl-file.h"
#include "vdl-utils.h"
#include "vdl-mem.h"
#include "machine.h"
#include <sys/mman.h>

struct VdlFileInfo
{
  // vaddr of DYNAMIC program header
  unsigned long dynamic;

  struct VdlFileMap ro_map;
  struct VdlFileMap rw_map;
};

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
static struct VdlFileMap
file_map_add_load_base (struct VdlFileMap map, unsigned long load_base)
{
  struct VdlFileMap result = map;
  result.mem_start_align += load_base;
  result.mem_zero_start += load_base;
  result.mem_anon_start_align += load_base;
  return result;
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

static struct VdlList *
vdl_file_get_dt_needed (struct VdlFile *file)
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
      vdl_alloc_free (filename);
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
      vdl_alloc_free (fullname);
    }
  return 0;
}
static char *
search_filename (const char *name, 
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
  vdl_alloc_free (realname);
  return 0;
}

static struct VdlFile *
find_by_name (struct VdlContext *context,
	      const char *name)
{
  if (vdl_utils_strisequal (name, "ldso"))
    {
      // we want to make sure that all contexts
      // reuse the same ldso.
      return g_vdl.ldso;
    }
  void **i;
  for (i = vdl_list_begin (context->loaded);
       i != vdl_list_end (context->loaded);
       i = vdl_list_next (i))
    {
      struct VdlFile *cur = *i;
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
  void **i;
  for (i = vdl_list_begin (context->loaded);
       i != vdl_list_end (context->loaded);
       i = vdl_list_next (i))
    {
      struct VdlFile *cur = *i;
      if (cur->st_dev == dev &&
	  cur->st_ino == ino)
	{
	  return cur;
	}
    }
  return 0;
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


static int 
get_file_info (uint32_t phnum,
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

static struct VdlFile *
file_new (unsigned long load_base,
	  const struct VdlFileInfo *info,
	  const char *filename, 
	  const char *name,
	  struct VdlContext *context)
{
  struct VdlFile *file = vdl_alloc_new (struct VdlFile);

  vdl_context_add_file (context, file);

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
  file->lookup_type = FILE_LOOKUP_GLOBAL_LOCAL;
  file->local_scope = vdl_list_new ();
  file->deps = vdl_list_new ();
  file->name = vdl_utils_strdup (name);
  file->depth = 0;

  return file;
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

static struct VdlFile *
vdl_file_map_single (struct VdlContext *context, 
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

  phdr = vdl_alloc_malloc (header.e_phnum * header.e_phentsize);
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

  if (!get_file_info (header.e_phnum, phdr, &info))
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

  struct VdlFile *file = file_new (load_base, &info, 
				   filename, name,
				   context);
  file->st_dev = st_buf.st_dev;
  file->st_ino = st_buf.st_ino;
  
  vdl_alloc_free (phdr);
  system_close (fd);

  vdl_context_notify (context, file, VDL_EVENT_MAPPED);

  return file;
error:
  if (fd >= 0)
    {
      system_close (fd);
    }
  vdl_alloc_free (phdr);
  if (mapping_start != 0)
    {
      system_munmap ((void*)mapping_start, mapping_size);
    }
  return 0;
}

struct SingleMapResult
{
  struct VdlFile *file;
  char *error_string;
  bool newly_mapped;
};


static struct SingleMapResult
vdl_file_map_single_maybe (struct VdlContext *context,
			   const char *requested_filename,
			   struct VdlList *rpath,
			   struct VdlList *runpath)
{
  struct SingleMapResult result;
  result.error_string = 0;
  result.newly_mapped = false;
  result.file = 0;
  // Try to see if we don't have a hardcoded name conversion
  const char *name = vdl_context_lib_remap (context, requested_filename);
  // if the file is already mapped within this context,
  // get it and add it to deps
  result.file = find_by_name (context, name);
  if (result.file != 0)
    {
      return result;
    }
  // Search the file in the filesystem
  char *filename = search_filename (name, rpath, runpath);
  if (filename == 0)
    {
      result.error_string = vdl_utils_sprintf ("Could not find %s\n", 
					       name);
      return result;
    }
  // get information about file.
  struct stat buf;
  if (system_fstat (filename, &buf) == -1)
    {
      result.error_string = vdl_utils_sprintf ("Could not stat %s as %s\n", 
					       name, filename);
      vdl_alloc_free (filename);
      return result;
    }
  // If you create a symlink to a binary and link to the
  // symlinks rather than the underlying binary, the DT_NEEDED
  // entries record different names for the same binary so,
  // the search by name above will fail. So, here, we stat
  // the file we found and check that none of the files
  // already mapped in the same context have the same ino/dev
  // pair. If they do, we don't need to re-map the file
  // and can re-use the previous map.
  result.file = find_by_dev_ino (context, buf.st_dev, buf.st_ino);
  if (result.file != 0)
    {
      vdl_alloc_free (filename);
      return result;
    }
  // The file is really not yet mapped so, we have to map it
  result.file = vdl_file_map_single (context, filename, name);
  VDL_LOG_ASSERT (result.file != 0, "The file should be there so this should not fail.");
  result.newly_mapped = true;

  vdl_alloc_free (filename);

  return result;
}

char *
vdl_file_map_deps_recursive (struct VdlFile *item, 
			     struct VdlList *caller_rpath,
			     struct VdlList *newly_mapped)
{
  VDL_LOG_FUNCTION ("file=%s", item->name);
  char *error = 0;

  if (item->deps_initialized)
    {
      return error;
    }
  item->deps_initialized = 1;

  struct VdlList *rpath = get_path (item, DT_RPATH);
  struct VdlList *runpath = get_path (item, DT_RUNPATH);
  struct VdlList *current_rpath = vdl_list_copy (rpath);
  vdl_list_insert_range (current_rpath,
			 vdl_list_end (current_rpath),
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
      struct SingleMapResult tmp_result;
      tmp_result = vdl_file_map_single_maybe (item->context, *cur,
					      current_rpath, runpath);
      if (tmp_result.file == 0)
	{
	  // oops, failed to find the requested dt_needed
	  error = tmp_result.error_string;
	  goto out;
	}
      if (tmp_result.newly_mapped)
	{
	  vdl_list_push_back (newly_mapped, tmp_result.file);
	}
      tmp_result.file->depth = vdl_utils_max (tmp_result.file->depth, item->depth + 1);
      // add the new file to the list of dependencies
      vdl_list_push_back (item->deps, tmp_result.file);
    }

  // then, recursively map the deps of each dep.
  for (cur = vdl_list_begin (item->deps); 
       cur != vdl_list_end (item->deps); 
       cur = vdl_list_next (cur))
    {
      error = vdl_file_map_deps_recursive (*cur, rpath, newly_mapped);
      if (error != 0)
	{
	  goto out;
	}
    }

 out:
  vdl_utils_str_list_delete (rpath);
  vdl_list_delete (current_rpath);
  vdl_utils_str_list_delete (runpath);
  vdl_utils_str_list_delete (dt_needed);
  return error;
}




struct VdlMapResult 
vdl_map_from_memory (unsigned long load_base,
		     uint32_t phnum,
		     ElfW(Phdr) *phdr,
		     // a fully-qualified path to the file
		     // represented by the phdr
		     const char *path, 
		     // a non-fully-qualified filename
		     const char *filename,
		     struct VdlContext *context)
{
  struct VdlMapResult result;
  struct VdlFileInfo info;
  int status;

  result.requested = 0;
  result.newly_mapped = vdl_list_new ();
  result.error_string = 0;
  status = get_file_info (phnum, phdr, &info);
  if (status == 0)
    {
      result.error_string = vdl_utils_sprintf ("Unable to obtain mapping information for %s/%s",
					       path, filename);
      goto out;
    }
  struct VdlFile *file = file_new (load_base, &info,
				   path, filename, context);
  vdl_list_push_back (result.newly_mapped, file);  

  struct VdlList *empty = vdl_list_new ();
  result.error_string = vdl_file_map_deps_recursive (file, empty, 
						     result.newly_mapped);
  if (result.error_string == 0)
    {
      result.requested = file;
    }
  vdl_list_delete (empty);

 out:
  return result;
}

struct VdlMapResult 
vdl_map_from_filename (struct VdlContext *context, 
		       const char *filename)
{
  struct VdlMapResult result;
  struct SingleMapResult single;
  struct VdlList *empty = vdl_list_new ();
  result.requested = 0;
  result.error_string = 0;
  result.newly_mapped = vdl_list_new ();
  single = vdl_file_map_single_maybe (context,
				      filename,
				      empty,
				      empty);
  if (single.file == 0)
    {
      result.error_string = single.error_string;
      vdl_list_delete (empty);
      return result;
    }
  if (single.newly_mapped)
    {
      vdl_list_push_back (result.newly_mapped, single.file);
    }
  result.error_string = vdl_file_map_deps_recursive (single.file, empty, 
						     result.newly_mapped);
  if (result.error_string == 0)
    {
      result.requested = single.file;
    }
      vdl_list_delete (empty);
  return result;
}
