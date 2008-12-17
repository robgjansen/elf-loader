#include "mdl-elf.h"
#include "mdl.h"
#include "system.h"
#include <unistd.h>
#include <sys/mman.h>

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

struct StringList *mdl_elf_get_dt_needed (unsigned long load_base, ElfW(Dyn) *dynamic)
{
  MDL_LOG_FUNCTION ("load_base=%ld, dynamic=%p", load_base, dynamic);
  ElfW(Dyn) *cur;
  unsigned long dt_strtab = 0;
  struct StringList *ret = 0;
  for (cur = dynamic; cur->d_tag != DT_NULL; cur++)
    {
      if (cur->d_tag == DT_STRTAB)
	{
	  dt_strtab = cur->d_un.d_ptr;
	  break;
	}
    }
  if (dt_strtab == 0)
    {
      return 0;
    }
  for (cur = dynamic; cur->d_tag != DT_NULL; cur++)
    {
      if (cur->d_tag == DT_NEEDED)
	{
	  const char *str = (const char *)(load_base + dt_strtab + cur->d_un.d_val);
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
struct MappedFile *mdl_elf_map_single (uint32_t context, const char *filename, const char *name)
{
  MDL_LOG_FUNCTION ("contex=%d, filename=%s, name=%s", context, filename, name);
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

  struct MappedFile *file = mdl_elf_file_new (load_base, &info, 
					      filename, name);
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
find_by_name (uint32_t context,
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
find_by_dev_ino (uint32_t context, 
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

int mdl_elf_map_deps (uint32_t context, struct MappedFile *item)
{
  MDL_LOG_FUNCTION ("context=%d, item=%p", context, item);
  struct MappedFileList *deps = 0;
  struct StringList *dt_needed = mdl_elf_get_dt_needed (item->load_base, 
							(void*)item->dynamic);

  // first, map each dep and accumulate them in deps variable
  struct StringList *cur;
  for (cur = dt_needed; cur != 0; cur = cur->next)
    {
      MDL_LOG_DEBUG ("needed=%s\n", cur->str);
      struct MappedFile *dep = 0;
      dep = find_by_name (context, cur->str);
      if (dep != 0)
	{
	  deps = mdl_file_list_append_one (deps, dep);
	  continue;
	}
      
      char *filename = mdl_elf_search_file (cur->str);
      if (filename == 0)
	{
	  MDL_LOG_ERROR ("Could not find %s\n", cur->str);
	  goto error;
	}
      MDL_LOG_DEBUG ("found %s\n", filename);
      struct stat buf;
      if (system_fstat (filename, &buf) == -1)
	{
	  MDL_LOG_ERROR ("Cannot stat %s\n", filename);
	  mdl_free (filename, mdl_strlen (filename)+1);
	  goto error;
	}
      dep = find_by_dev_ino (context, buf.st_dev, buf.st_ino);
      if (dep != 0)
	{
	  deps = mdl_file_list_append_one (deps, dep);
	  mdl_free (filename, mdl_strlen (filename)+1);
	  continue;
	}
      dep = mdl_elf_map_single (context, filename, cur->str);
      

      deps = mdl_file_list_append_one (deps, dep);
      mdl_free (filename, mdl_strlen (filename)+1);
    }
  mdl_str_list_free (dt_needed);

  // then, recursively map the deps of each dep.
  struct MappedFileList *dep;
  for (dep = deps; dep != 0; dep = dep->next)
    {
      if (!mdl_elf_map_deps (context, dep->item))
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

static void
append_file (struct MappedFile *item)
{
  MDL_LOG_FUNCTION ("item=%p", item);
  if (g_mdl.link_map == 0)
    {
      g_mdl.link_map = item;
      return;
    }
  struct MappedFile *cur = g_mdl.link_map;
  while (cur->next != 0)
    {
      cur = cur->next;
    }
  cur->next = item;
  item->prev = cur;
  item->next = 0;
}


struct MappedFile *mdl_elf_file_new (unsigned long load_base,
				     const struct FileInfo *info,
				     const char *filename, 
				     const char *name)
{
  struct MappedFile *file = mdl_new (struct MappedFile);

  file->load_base = load_base;
  file->filename = mdl_strdup (filename);
  file->dynamic = info->dynamic + load_base;
  file->next = 0;
  file->prev = 0;
  file->count = 1;
  file->context = 0;
  file->st_dev = 0;
  file->st_ino = 0;
  file->ro_start = info->ro_start + load_base;
  file->ro_size = info->ro_size;
  file->rw_size = info->rw_size;
  file->ro_file_offset = info->ro_file_offset;
  file->scope_set = 0;
  file->init_called = 0;
  file->fini_called = 0;
  file->local_scope = 0;
  file->global_scope = 0;
  file->deps = 0;
  file->name = mdl_strdup (name);

  append_file (file);

  return file;
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
  ElfW(Word) *dt_hash = 0;
  const char *dt_strtab = 0;
  ElfW(Sym) *dt_symtab = 0;
  ElfW(Dyn) *cur = (ElfW(Dyn)*)file->dynamic;
  while (cur->d_tag != DT_NULL && (dt_hash == 0 || 
				   dt_strtab == 0 || 
				   dt_symtab == 0))
    {
      if (cur->d_tag == DT_HASH)
	{
	  dt_hash = (ElfW(Word)*) (file->load_base + cur->d_un.d_val);
	}
      else if (cur->d_tag == DT_STRTAB)
	{
	  dt_strtab = (const char *) (file->load_base + cur->d_un.d_val);
	}
      else if (cur->d_tag == DT_SYMTAB)
	{
	  dt_symtab = (ElfW(Sym)*) (file->load_base + cur->d_un.d_val);
	}
      cur++;
    }
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
