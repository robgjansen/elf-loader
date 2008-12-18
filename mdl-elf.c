#include "mdl-elf.h"
#include "mdl.h"
#include "system.h"
#include <unistd.h>
#include <sys/mman.h>

static ElfW(Dyn) *
mdl_elf_file_get_dynamic (const struct MappedFile *file, unsigned long tag)
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

static unsigned long
mdl_elf_file_get_dynamic_v (const struct MappedFile *file, unsigned long tag)
{
  ElfW(Dyn) *dyn = mdl_elf_file_get_dynamic (file, tag);
  if (dyn == 0)
    {
      return 0;
    }
  return dyn->d_un.d_val;
}

static unsigned long
mdl_elf_file_get_dynamic_p (const struct MappedFile *file, unsigned long tag)
{
  ElfW(Dyn) *dyn = mdl_elf_file_get_dynamic (file, tag);
  if (dyn == 0)
    {
      return 0;
    }
  return file->load_base + dyn->d_un.d_val;
}

ElfW(Phdr) *mdl_elf_search_phdr (ElfW(Phdr) *phdr, int phnum, int type)
{
  MDL_LOG_FUNCTION ("phdr=%p, phnum=%d, type=%d", phdr, phnum, type);
  ElfW(Phdr) *cur;
  int i;
  for (cur = phdr, i = 0; i < phnum; cur++, i++)
    {
      if (cur->p_type == type)
	{
	  return cur;
	}
    }
  return 0;
}

struct StringList *mdl_elf_get_dt_needed (struct MappedFile *file)
{
  MDL_LOG_FUNCTION ("file=%s", file->name);
  unsigned long dt_strtab = mdl_elf_file_get_dynamic_p (file, DT_STRTAB);
  if (dt_strtab == 0)
    {
      return 0;
    }
  ElfW(Dyn) *dynamic = (ElfW(Dyn)*)file->dynamic;
  ElfW(Dyn)*cur;
  struct StringList *ret = 0;
  for (cur = dynamic; cur->d_tag != DT_NULL; cur++)
    {
      if (cur->d_tag == DT_NEEDED)
	{
	  const char *str = (const char *)(dt_strtab + cur->d_un.d_val);
	  struct StringList *tmp = mdl_new (struct StringList);
	  tmp->str = mdl_strdup (str);
	  tmp->next = ret;
	  ret = tmp;
	  MDL_LOG_DEBUG ("needed=%s\n", str);
	}
    }
  return ret;
}
char *mdl_elf_search_file (const char *name)
{
  MDL_LOG_FUNCTION ("name=%s", name);
  struct StringList *cur;
  for (cur = g_mdl.search_dirs; cur != 0; cur = cur->next)
    {
      char *fullname = mdl_strconcat (cur->str, "/", name, 0);
      if (mdl_exists (fullname))
	{
	  return fullname;
	}
      mdl_free (fullname, mdl_strlen (fullname)+1);
    }
  if (mdl_exists (name))
    {
      return mdl_strdup (name);
    }
  return 0;
}
#define ALIGN_DOWN(v,align) ((v)-((v)%align))
#define ALIGN_UP(v,align) ((v)+(align-((v)%align)))
struct MappedFile *mdl_elf_map_single (struct Context *context, 
				       const char *filename, 
				       const char *name)
{
  MDL_LOG_FUNCTION ("contex=%p, filename=%s, name=%s", context, filename, name);
  ElfW(Ehdr) header;
  ElfW(Phdr) *phdr = 0;
  size_t bytes_read;
  int fd = -1;
  unsigned long map_start = -1;
  unsigned long map_size = 0;
  struct FileInfo info;

  fd = system_open_ro (filename);
  if (fd == -1)
    {
      MDL_LOG_ERROR ("Could not open ro target file: %s\n", filename);
      goto error;
    }

  bytes_read = system_read (fd, &header, sizeof (header));
  if (bytes_read == -1 || bytes_read != sizeof (header))
    {
      MDL_LOG_ERROR ("Could not read header read=%d\n", bytes_read);
      goto error;
    }
  // check that the header size is correct
  if (header.e_ehsize != sizeof (header))
    {
      MDL_LOG_ERROR ("header size invalid, %d!=%d\n", header.e_ehsize, sizeof(header));
      goto error;
    }
  if (header.e_type != ET_EXEC &&
      header.e_type != ET_DYN)
    {
      MDL_LOG_ERROR ("header type unsupported, type=0x%x\n", header.e_type);
      goto error;
    }

  phdr = mdl_malloc (header.e_phnum * header.e_phentsize);
  if (system_lseek (fd, header.e_phoff, SEEK_SET) == -1)
    {
      MDL_LOG_ERROR ("lseek failed to go to off=0x%x\n", header.e_phoff);
      goto error;
    }
  bytes_read = system_read (fd, phdr, header.e_phnum * header.e_phentsize);
  if (bytes_read == -1 || bytes_read != header.e_phnum * header.e_phentsize)
    {
      MDL_LOG_ERROR ("read failed: read=%d\n", bytes_read);
      goto error;
    }

  if (!mdl_elf_file_get_info (header.e_phnum, phdr, &info))
    {
      MDL_LOG_ERROR ("unable to read data structure for %s\n", filename);
      goto error;
    }

  // we do a single mmap for both ro and rw entries as ro, and, then
  // use mprotect to give the 'w' power to the second entry. This is needed
  // to ensure that the relative position of both entries is preserved for
  // DYN files. For EXEC files, we don't care about the relative position
  // but we need to ensure that the absolute position is correct so we add the 
  // MAP_FIXED flag.

  int fixed = (header.e_type == ET_EXEC)?MAP_FIXED:0;
  map_size = info.ro_size + info.rw_size;
  map_start = (unsigned long) system_mmap ((void*)info.ro_start,
					   info.ro_size + info.rw_size,
					   PROT_READ, MAP_PRIVATE | fixed, fd, 
					   info.ro_file_offset);
  if (map_start == -1)
    {
      MDL_LOG_ERROR ("Unable to perform mapping for %s\n", filename);
      goto error;
    }
  // calculate the offset between the start address we asked for and the one we got
  unsigned long load_base = map_start - info.ro_start;
  if (fixed && load_base != 0)
    {
      MDL_LOG_ERROR ("We need a fixed address and we did not get it in %s\n", filename);
      goto error;
    }

  // make the rw map actually writable.
  if (system_mprotect ((void*)(info.ro_start + info.ro_size + load_base), 
		       info.rw_size, PROT_READ | PROT_WRITE) == -1)
    {
      MDL_LOG_ERROR ("Unable to add w flag to rw mapping for file=%s 0x%x:0x%x\n", 
		     filename, info.ro_start + info.ro_size + load_base, info.rw_size);
      goto error;
    }

  struct stat st_buf;
  if (system_fstat (filename, &st_buf) == -1)
    {
      MDL_LOG_ERROR ("Unable to stat file %s\n", filename);
      goto error;
    }

  struct MappedFile *file = mdl_file_new (load_base, &info, 
					  filename, name,
					  context);
  MDL_LOG_DEBUG ("mapped file %s ro=0x%x:0x%x, rw=0x%x:0x%x\n", filename,
		 file->ro_start, file->ro_size, 
		 file->ro_start + file->ro_size, file->rw_size);
  
  mdl_free (phdr, header.e_phnum * header.e_phentsize);
  system_close (fd);
  return file;
error:
  if (fd >= 0)
    {
      system_close (fd);
    }
  if (phdr != 0)
    {
      mdl_free (phdr, header.e_phnum * header.e_phentsize);
    }
  if (map_start != -1)
    {
      system_munmap ((void*)map_start, map_size);
    }
  return 0;
}

static const char *
convert_name (const char *name)
{
  MDL_LOG_FUNCTION ("name=%s", name);
  // these are hardcoded name conversions to ensure that
  // we can replace the libc loader.
  const struct HardcodedName {
    const char *original;
    const char *converted;
  } hardcoded_names [] = 
      {{"/lib/ld-linux.so.2", "ldso"},
       {"ld-linux.so.2", "ldso"}};
  int i; 
  for (i = 0; i < (sizeof (hardcoded_names)/sizeof (struct HardcodedName)); i++)
    {
      if (mdl_strisequal (hardcoded_names[i].original, name))
	{
	  return hardcoded_names[i].converted;
	}
    }
  return name;
}

static struct MappedFile *
find_by_name (struct Context *context,
	      const char *name)
{
  name = convert_name (name);
  struct MappedFile *cur;
  for (cur = g_mdl.link_map; cur != 0; cur = cur->next)
    {
      if (mdl_strisequal (cur->name, name))
	{
	  return cur;
	}
    }
  return 0;
}
static struct MappedFile *
find_by_dev_ino (struct Context *context, 
		 dev_t dev, ino_t ino)
{
  struct MappedFile *cur;
  for (cur = g_mdl.link_map; cur != 0; cur = cur->next)
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

int mdl_elf_map_deps (struct MappedFile *item)
{
  MDL_LOG_FUNCTION ("file=%s", item->name);
  // get list of deps for the input file.
  struct StringList *dt_needed = mdl_elf_get_dt_needed (item);

  // first, map each dep and accumulate them in deps variable
  struct MappedFileList *deps = 0;
  struct StringList *cur;
  for (cur = dt_needed; cur != 0; cur = cur->next)
    {
      // if the file is already mapped within this context,
      // get it and add it to deps
      struct MappedFile *dep = find_by_name (item->context, cur->str);
      if (dep != 0)
	{
	  deps = mdl_file_list_append_one (deps, dep);
	  continue;
	}
      // Search the file in the filesystem
      char *filename = mdl_elf_search_file (cur->str);
      if (filename == 0)
	{
	  MDL_LOG_ERROR ("Could not find %s\n", cur->str);
	  goto error;
	}
      // get information about file.
      struct stat buf;
      if (system_fstat (filename, &buf) == -1)
	{
	  MDL_LOG_ERROR ("Cannot stat %s\n", filename);
	  mdl_free (filename, mdl_strlen (filename)+1);
	  goto error;
	}
      // If you create a symlink to a binary and link to the
      // symlinks rather than the underlying binary, the DT_NEEDED
      // entries record different names for the same binary so,
      // the search by name above will fail. So, here, we stat
      // the file we found and check that none of the files
      // already mapped in the same context have the same ino/dev
      // pair. If they do, we don't need to re-map the file
      // and can re-use the previous map.
      dep = find_by_dev_ino (item->context, buf.st_dev, buf.st_ino);
      if (dep != 0)
	{
	  deps = mdl_file_list_append_one (deps, dep);
	  mdl_free (filename, mdl_strlen (filename)+1);
	  continue;
	}
      // The file is really not yet mapped so, we have to map it
      dep = mdl_elf_map_single (item->context, filename, cur->str);
      
      // add the new file to the list of dependencies
      deps = mdl_file_list_append_one (deps, dep);

      mdl_free (filename, mdl_strlen (filename)+1);
    }
  mdl_str_list_free (dt_needed);

  // then, recursively map the deps of each dep.
  struct MappedFileList *dep;
  for (dep = deps; dep != 0; dep = dep->next)
    {
      if (!mdl_elf_map_deps (dep->item))
	{
	  goto error;
	}
    }

  // Finally, update the deps
  item->deps = deps;

  return 1;
 error:
  mdl_str_list_free (dt_needed);
  if (deps != 0)
    {
      mdl_file_list_free (deps);
    }
  return 0;
}
int mdl_elf_file_get_info (uint32_t phnum,
			   ElfW(Phdr) *phdr,
			   struct FileInfo *info)
{
  MDL_LOG_FUNCTION ("phnum=%d, phdr=%p", phnum, phdr);
  ElfW(Phdr) *ro = 0, *rw = 0, *interp = 0, *dynamic = 0, *pt_phdr = 0, *cur;
  int i;
  for (i = 0, cur = phdr; i < phnum; i++, cur++)
    {
      if (cur->p_type == PT_LOAD)
	{
	  if (cur->p_flags & PF_W)
	    {
	      if (rw != 0)
		{
		  MDL_LOG_ERROR ("file has more than one RW PT_LOAD\n", 1);
		  goto error;
		}
	      rw = cur;
	    }
	  else
	    {
	      if (ro != 0)
		{
		  MDL_LOG_ERROR ("file has more than one RO PT_LOAD\n", 1);
		  goto error;
		}
	      ro = cur;
	    }
	}
      else if (cur->p_type == PT_INTERP)
	{
	  interp = cur;
	}
      else if (cur->p_type == PT_DYNAMIC)
	{
	  dynamic = cur;
	}
      else if (cur->p_type == PT_PHDR)
	{
	  pt_phdr = cur;
	}
    }
  if (ro == 0 || rw == 0 || interp == 0 || dynamic == 0 || pt_phdr == 0)
    {
      MDL_LOG_ERROR ("file is missing a critical program header "
		     "ro=0x%x, rw=0x%x, interp=0x%x, dynamic=0x%x, pt_phdr=0x%x\n", 
		     ro, rw, interp, dynamic, pt_phdr);
      goto error;
    }
  if (ro->p_memsz != ro->p_filesz)
    {
      MDL_LOG_ERROR ("file and memory size should be equal: 0x%x != 0x%x\n",
		     ro->p_memsz, ro->p_filesz);
      goto error;
    }
  if (ro->p_offset != 0)
    {
      MDL_LOG_ERROR ("The ro map should include the ELF header. off=0x%x\n",
		     ro->p_offset);
      goto error;
    }
  if (ro->p_align != rw->p_align)
    {
      MDL_LOG_ERROR ("something is fishy about the alignment constraints "
		     "ro_align=0x%x, rw_align=0x%x\n", ro->p_align, rw->p_align);
      goto error;
    }
  if (pt_phdr->p_offset < ro->p_offset || pt_phdr->p_offset + pt_phdr->p_filesz > ro->p_filesz)
    {
      MDL_LOG_ERROR ("phdr not included in ro load\n", 1);
      goto error;
    }
  if (dynamic->p_offset < rw->p_offset || dynamic->p_filesz > rw->p_filesz)
    {
      MDL_LOG_ERROR ("dynamic not included in rw load\n", 1);
      goto error;
    }


  unsigned long ro_start = ALIGN_DOWN (ro->p_vaddr, ro->p_align);
  unsigned long ro_size = ALIGN_UP (ro->p_memsz, ro->p_align);
  unsigned long rw_start = ALIGN_DOWN (rw->p_vaddr, rw->p_align);
  unsigned long rw_size = ALIGN_UP (rw->p_memsz, rw->p_align);
  unsigned long ro_file_offset = ALIGN_DOWN (ro->p_offset, ro->p_align);
  if (ro_start + ro_size != rw_start)
    {
      MDL_LOG_ERROR ("ro and rw maps must be adjacent\n", 1);
      goto error;
    }

  info->dynamic = dynamic->p_vaddr;
  info->interpreter_name = interp->p_vaddr;
  info->ro_start = ro_start;
  info->ro_size = ro_size;
  info->rw_size = rw_size;
  info->ro_file_offset = ro_file_offset;;

  return 1;
 error:
  return 0;
}

struct MappedFileList *
mdl_elf_gather_all_deps_breadth_first (struct MappedFile *file)
{
  MDL_LOG_FUNCTION ("file=%s", file->name);

  struct MappedFileList *list, *cur;

  list = mdl_new (struct MappedFileList);
  list->item = file;
  list->next = 0;
  for (cur = list; cur != 0; cur = cur->next)
    {
      struct MappedFileList *copy = mdl_file_list_copy (cur->item->deps);
      cur = mdl_file_list_append (cur, copy);
    }

  return list;
}

unsigned long
mdl_elf_hash (const char *n)
{
  // Copy/paste from the ELF specification (figure 2-9)
  const unsigned char *name = (const unsigned char *) n;
  unsigned long h = 0, g;
  while (*name)
    {
      h = (h << 4) + *name++;
      if ((g = (h & 0xf0000000)))
	h ^= g >> 24;
      h &= ~g;
    }
  return h;
}

static unsigned long 
mdl_elf_symbol_lookup_one (const char *name, unsigned long hash,
			   const struct MappedFile *file)

{
  MDL_LOG_FUNCTION ("name=%s, hash=0x%x, file=%s", name, hash, file->name);
  // first, gather information needed to look into the hash table
  ElfW(Word) *dt_hash = (ElfW(Word)*) mdl_elf_file_get_dynamic_p (file, DT_HASH);
  const char *dt_strtab = (const char *) mdl_elf_file_get_dynamic_p (file, DT_STRTAB);
  ElfW(Sym) *dt_symtab = (ElfW(Sym)*) mdl_elf_file_get_dynamic_p (file, DT_SYMTAB);

  if (dt_hash == 0 || dt_strtab == 0 || dt_symtab == 0)
    {
      return 0;
    }

  // Then, look into the hash table itself.
  // First entry is number of buckets
  // Second entry is number of chains
  ElfW(Word) nbuckets = dt_hash[0];
  unsigned long sym = dt_hash[2+(hash%nbuckets)];
  // The values stored in the hash table are
  // an index in the symbol table.
  if (dt_symtab[sym].st_name != 0 && 
      dt_symtab[sym].st_shndx != SHN_UNDEF)
    {
      // the symbol name is an index in the string table
      // and the symbol value is a virtual address relative to
      // the load base
      if (mdl_strisequal (dt_strtab + dt_symtab[sym].st_name, name))
	{
	  unsigned long v = file->load_base + dt_symtab[sym].st_value;
	  MDL_LOG_DEBUG ("yay ! found symbol=%s in file=%s, value=0x%x\n", 
			 name, file->name, v);
	  return v;
	}
    }

  // The entry in the bucket does not match, so, we search
  // the hash table chains. The chain associated with our bucket
  // starts at the index returned by our bucket
  ElfW(Word) *chain = &dt_hash[2+nbuckets];
  unsigned long index = sym;
  while (chain[index] != 0)
    {
      index = chain[index];
      if (dt_symtab[index].st_name != 0 && 
	  dt_symtab[index].st_shndx != SHN_UNDEF)
	{
	  if (mdl_strisequal (dt_strtab + dt_symtab[index].st_name, name))
	    {
	      unsigned long v = file->load_base + dt_symtab[index].st_value;
	      MDL_LOG_DEBUG ("yay ! found symbol=%s in file=%s, value=0x%x\n", 
			     name, file->name, v);
	      return v;
	    }
	}
    }
  return 0;
}


unsigned long mdl_elf_symbol_lookup (const char *name, unsigned long hash,
				     struct MappedFileList *scope)
{
  MDL_LOG_FUNCTION ("name=%s, hash=0x%x, scope=%p", name, hash, scope);
  // then, iterate scope until we find the requested symbol.
  unsigned long addr;
  struct MappedFileList *cur;
  for (cur = scope; cur != 0; cur = cur->next)
    {
      addr = mdl_elf_symbol_lookup_one (name, hash, cur->item);
      if (addr != 0)
	{
	  return addr;
	}
    }
  return 0;
}

// the glibc elf loader passes all 3 arguments
// to the initialization functions so, we do the
// same for compatibility purposes but:
//   - I am not aware of any specification which
//     describes this requirement
//   - all the initialization functions I have seen
//     take no arguments so, they effectively ignore
//     what the libc is giving them.
// To summarize, I doubt it would make any practical
// difference if we did not pass the 3 arguments below
// but, hey, well, just in case, we do.
typedef void (*init_function) (int, char **, char **);

static void
mdl_elf_call_init_one (struct MappedFile *file)
{
  MDL_LOG_FUNCTION ("file=%s", file->name);
  // Gather information from the .dynamic section
  unsigned long dt_init = mdl_elf_file_get_dynamic_p (file, DT_INIT);
  unsigned long dt_init_array = mdl_elf_file_get_dynamic_p (file, DT_INIT_ARRAY);
  unsigned long dt_init_arraysz = mdl_elf_file_get_dynamic_p (file, DT_INIT_ARRAYSZ);
  // First, invoke the old-style DT_INIT function.
  // The address of the function to call is stored in
  // the DT_INIT tag, here: dt_init.
  if (dt_init != 0)
    {
      init_function init;
      init = (init_function) (dt_init + file->load_base);
      init (file->context->argc, file->context->argv, file->context->envp);
    }

  // Then, invoke the newer DT_INIT_ARRAY functions.
  // The address of the functions to call is stored as
  // an array of pointers pointed to by DT_INIT_ARRAY
  if (dt_init_array != 0)
    {
      init_function *init = (init_function *) (dt_init_array + file->load_base);
      int i;
      for (i = 0; i < dt_init_arraysz / sizeof (init_function); i++, init++)
	{
	  (*init) (file->context->argc, file->context->argv, file->context->envp);
	}
    }
}

void mdl_elf_call_init (struct MappedFile *file)
{
  MDL_LOG_FUNCTION ("file=%s", file->name);
  if (file->init_called)
    {
      // if we are initialized already, no need to do any work
      return;
    }
  // mark the file as initialized
  file->init_called = 1;

  // iterate over all deps first before initialization.
  struct MappedFileList *cur;
  for (cur = file->deps; cur != 0; cur = cur->next)
    {
      mdl_elf_call_init (cur->item);
    }

  // Now that all deps are initialized, initialize ourselves.
  mdl_elf_call_init_one (file);  
}
unsigned long mdl_elf_get_entry_point (struct MappedFile *file)
{
  // This piece of code assumes that the ELF header starts at the
  // first byte of the ro map. This is verified in mdl_elf_file_get_info
  // so we are safe with this assumption.
  ElfW(Ehdr) *header = (ElfW(Ehdr)*) file->ro_start;
  return header->e_entry + file->load_base;
}
void
mdl_elf_iterate_pltrel (struct MappedFile *file, void (*cb)(struct MappedFile *file,
							    ElfW(Rel) *rel,
							    const char *name))
{
  MDL_LOG_FUNCTION ("file=%s", file->name);
  ElfW(Rel) *dt_jmprel = (ElfW(Rel)*)mdl_elf_file_get_dynamic_p (file, DT_JMPREL);
  unsigned long dt_pltrel = mdl_elf_file_get_dynamic_v (file, DT_PLTREL);
  unsigned long dt_pltrelsz = mdl_elf_file_get_dynamic_v (file, DT_PLTRELSZ);
  const char *dt_strtab = (const char *)mdl_elf_file_get_dynamic_p (file, DT_STRTAB);
  ElfW(Sym) *dt_symtab = (ElfW(Sym)*)mdl_elf_file_get_dynamic_p (file, DT_SYMTAB);
  
  if (dt_pltrel != DT_REL || dt_pltrelsz == 0 || 
      dt_jmprel == 0 || dt_strtab == 0 || 
      dt_symtab == 0)
    {
      return;
    }
  int i;
  for (i = 0; i < dt_pltrelsz/sizeof(ElfW(Rel)); i++)
    {
      ElfW(Rel) *rel = &dt_jmprel[i];
      ElfW(Sym) *sym = &dt_symtab[ELFW_R_SYM (rel->r_info)];
      const char *symbol_name;
      if (sym->st_name == 0)
	{
	  symbol_name = 0;
	}
      else
	{
	  symbol_name = dt_strtab + sym->st_name;
	}
      (*cb) (file, rel, symbol_name);
    }
}

void
mdl_elf_iterate_rel (struct MappedFile *file, 
		     void (*cb)(struct MappedFile *file,
				ElfW(Rel) *rel))
{
  MDL_LOG_FUNCTION ("file=%s", file->name);
  ElfW(Rel) *dt_rel = (ElfW(Rel)*)mdl_elf_file_get_dynamic_p (file, DT_REL);
  unsigned long dt_relsz = mdl_elf_file_get_dynamic_v (file, DT_RELSZ);
  unsigned long dt_relent = mdl_elf_file_get_dynamic_v (file, DT_RELENT);
  if (dt_rel == 0 || dt_relsz == 0 || dt_relent == 0)
    {
      return;
    }
  uint32_t i;
  for (i = 0; i < dt_relsz/dt_relent; i++)
    {
      ElfW(Rel) *tmp = &dt_rel[i];
      (*cb) (file, tmp);
    }
}

static void
i386_pltrel_callback (struct MappedFile *file,
		      ElfW(Rel) *rel,
		      const char *symbol_name)
{
  MDL_LOG_FUNCTION ("file=%s, symbol_name=%s, off=0x%x", 
		    file->name, symbol_name, rel->r_offset);
  // Here, we expect only entries of type R_386_JMP_SLOT
  if (ELFW_R_TYPE (rel->r_info) != R_386_JMP_SLOT)
    {
      MDL_LOG_ERROR ("Bwaaah: expected R_386_JMP_SLOT, got=%x\n",
		     ELFW_R_TYPE (rel->r_info));
      return;
    }
  // calculate the hash here to avoid calculating 
  // it twice in both calls to symbol_lookup
  unsigned long hash = mdl_elf_hash (symbol_name);

  // lookup the symbol in the global scope first
  unsigned long addr = mdl_elf_symbol_lookup (symbol_name, hash, file->context->global_scope);
  if (addr == 0)
    {
      // and in the local scope.
      addr = mdl_elf_symbol_lookup (symbol_name, hash, file->local_scope);
      if (addr == 0)
	{
	  MDL_LOG_ERROR ("Cannot resolve symbol=%s\n", symbol_name);
	  return;
	}
    }

  // apply the address to the relocation
  unsigned long offset = file->load_base;
  offset += rel->r_offset;
  unsigned long *p = (unsigned long *)offset;
  *p = addr;
}

static void
i386_rel_callback (struct MappedFile *file,
		   ElfW(Rel) *rel)
{
  MDL_LOG_FUNCTION ("file=%s", file->name);
  // Here, we expect only entries of type R_386_RELATIVE
  if (ELFW_R_TYPE (rel->r_info) != R_386_RELATIVE)
    {
      MDL_LOG_ERROR ("Bwaaah: expected R_386_RELATIVE, got=%x\n",
		     ELFW_R_TYPE (rel->r_info));
      return;
    }
  unsigned long addr = rel->r_offset + file->load_base;
  unsigned long *p = (unsigned long *)addr;
  *p += file->load_base;
}


void mdl_elf_reloc (struct MappedFile *file)
{
  if (file->reloced)
    {
      return;
    }
  file->reloced = 1;

  mdl_elf_iterate_rel (file, i386_rel_callback);

  if (g_mdl.bind_now)
    {
      // force symbol resolution for all PLT entries _right now_
      mdl_elf_iterate_pltrel (file, i386_pltrel_callback);
    }
  else
    {
      // setup lazy binding by setting the GOT entries 2 and 3.
      // Entry 2 is set to a pointer to the associated MappedFile
      // Entry 3 is set to the asm trampoline mdl_symbol_lookup_asm
      // which calls mdl_symbol_lookup.
    }
}

void mdl_elf_file_setup_debug (struct MappedFile *interpreter)
{
  ElfW(Dyn) *dt_debug = mdl_elf_file_get_dynamic (interpreter, DT_DEBUG);
  unsigned long *p = (unsigned long *)&(dt_debug->d_un.d_ptr);
  *p = (unsigned long)&g_mdl;
}
