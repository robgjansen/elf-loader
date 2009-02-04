#include "vdl-elf.h"
#include "vdl.h"
#include "vdl-utils.h"
#include "system.h"
#include "machine.h"
#include <unistd.h>
#include <sys/mman.h>

#define vdl_max(a,b)(((a)>(b))?(a):(b))

ElfW(Dyn) *
vdl_elf_file_get_dynamic (const struct VdlFile *file, unsigned long tag)
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
vdl_elf_file_get_dynamic_v (const struct VdlFile *file, unsigned long tag)
{
  ElfW(Dyn) *dyn = vdl_elf_file_get_dynamic (file, tag);
  if (dyn == 0)
    {
      return 0;
    }
  return dyn->d_un.d_val;
}

static unsigned long
vdl_elf_file_get_dynamic_p (const struct VdlFile *file, unsigned long tag)
{
  ElfW(Dyn) *dyn = vdl_elf_file_get_dynamic (file, tag);
  if (dyn == 0)
    {
      return 0;
    }
  return file->load_base + dyn->d_un.d_ptr;
}

ElfW(Phdr) *vdl_elf_search_phdr (ElfW(Phdr) *phdr, int phnum, int type)
{
  VDL_LOG_FUNCTION ("phdr=%p, phnum=%d, type=%d", phdr, phnum, type);
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

struct VdlStringList *vdl_elf_get_dt_needed (struct VdlFile *file)
{
  VDL_LOG_FUNCTION ("file=%s", file->name);
  unsigned long dt_strtab = vdl_elf_file_get_dynamic_p (file, DT_STRTAB);
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
	  struct VdlStringList *tmp = vdl_new (struct VdlStringList);
	  tmp->str = vdl_strdup (str);
	  tmp->next = ret;
	  ret = tmp;
	  VDL_LOG_DEBUG ("needed=%s\n", str);
	}
    }
  return ret;
}
char *vdl_elf_search_file (const char *name)
{
  VDL_LOG_FUNCTION ("name=%s", name);
  if (vdl_strisequal (name, "libdl.so") ||
      vdl_strisequal (name, "libdl.so.2"))
    {
      // we replace libdl.so with our own libvdl.so
      name = "libvdl.so";
    }

  struct VdlStringList *cur;
  for (cur = g_vdl.search_dirs; cur != 0; cur = cur->next)
    {
      char *fullname = vdl_strconcat (cur->str, "/", name, 0);
      if (vdl_exists (fullname))
	{
	  return fullname;
	}
      vdl_free (fullname, vdl_strlen (fullname)+1);
    }
  if (vdl_exists (name))
    {
      return vdl_strdup (name);
    }
  return 0;
}
struct VdlFile *vdl_elf_map_single (struct VdlContext *context, 
				       const char *filename, 
				       const char *name)
{
  VDL_LOG_FUNCTION ("contex=%p, filename=%s, name=%s", context, filename, name);
  ElfW(Ehdr) header;
  ElfW(Phdr) *phdr = 0;
  size_t bytes_read;
  int fd = -1;
  unsigned long ro_start = 0;
  unsigned long rw_start = 0;
  unsigned long zero_start = 0;
  unsigned long map_size = 0;
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

  phdr = vdl_malloc (header.e_phnum * header.e_phentsize);
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

  if (!vdl_elf_file_get_info (header.e_phnum, phdr, &info))
    {
      VDL_LOG_ERROR ("unable to read data structure for %s\n", filename);
      goto error;
    }
  if (header.e_phoff <info.ro_file_offset || 
      header.e_phoff + header.e_phnum * header.e_phentsize > info.ro_file_offset + info.ro_size)
    {
      VDL_LOG_ERROR ("program header table not included in ro map in %s\n", filename);
      goto error;
    }

  // calculate the size of the total mapping required
  unsigned long end = info.ro_start + info.ro_size;
  end = vdl_max (end, info.rw_start + info.rw_size);
  end = vdl_max (end, info.zero_start + info.zero_size);
  map_size = end-info.ro_start;

  // We perform a single initial mmap to reserve all the virtual space we need
  // and, then, we map again portions of the space to make sure we get
  // the mappings we need
  int fixed = (header.e_type == ET_EXEC)?MAP_FIXED:0;
  ro_start = (unsigned long) system_mmap ((void*)info.ro_start,
					  map_size,
					  PROT_READ, 
					  MAP_PRIVATE | fixed, 
					  fd, info.ro_file_offset);
  if (ro_start == -1)
    {
      VDL_LOG_ERROR ("Unable to perform mapping for %s\n", filename);
      goto error;
    }
  // calculate the offset between the start address we asked for and the one we got
  unsigned long load_base = ro_start - info.ro_start;
  if (fixed && load_base != 0)
    {
      VDL_LOG_ERROR ("We need a fixed address and we did not get it in %s\n", filename);
      goto error;
    }
  // Now, unmap the rw area
  system_munmap ((void*)(load_base + info.rw_start),
		 info.rw_size);
  // Now, map again the rw area at the right location.
  rw_start = (unsigned long) system_mmap ((void*)load_base + info.rw_start,
					  info.rw_size,
					  PROT_READ | PROT_WRITE, 
					  MAP_PRIVATE | MAP_FIXED, 
					  fd, info.rw_file_offset);
  if (rw_start == -1)
    {
      VDL_LOG_ERROR ("Unable to perform rw mapping for %s\n", filename);
      goto error;
    }

  // zero the end of rw map
  vdl_memset ((void*)(load_base + info.memset_zero_start), 0, info.memset_zero_size);

  // first, unmap the extended file mapping for the zero pages.
  system_munmap ((void*)(load_base + info.zero_start),
		 info.zero_size);
  // then, map zero pages.
  zero_start = (unsigned long) system_mmap ((void*)load_base + info.zero_start,
					    info.zero_size, 
					    PROT_READ | PROT_WRITE, 
					    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
					    -1, 0);
  VDL_LOG_DEBUG ("maps: ro=0x%x:0x%x, rw=0x%x:0x%x, zero=0x%x:0x%x, memset_zero_start=0x%x:0x%x, "
		 "ro_offset=0x%x, rw_offset=0x%x\n", 
		 load_base + info.ro_start, info.ro_size,
		 load_base + info.rw_start, info.rw_size,
		 load_base + info.zero_start, info.zero_size,
		 load_base + info.memset_zero_start, info.memset_zero_size,
		 info.ro_file_offset, info.rw_file_offset);
  if (zero_start == -1)
    {
      VDL_LOG_ERROR ("Unable to map zero pages for %s\n", filename);
      goto error;
    }

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
  
  vdl_free (phdr, header.e_phnum * header.e_phentsize);
  system_close (fd);
  return file;
error:
  if (fd >= 0)
    {
      system_close (fd);
    }
  if (phdr != 0)
    {
      vdl_free (phdr, header.e_phnum * header.e_phentsize);
    }
  if (ro_start != 0)
    {
      system_munmap ((void*)ro_start, map_size);
    }
  return 0;
}

static const char *
convert_name (const char *name)
{
  VDL_LOG_FUNCTION ("name=%s", name);
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
      if (vdl_strisequal (hardcoded_names[i].original, name))
	{
	  return hardcoded_names[i].converted;
	}
    }
  return name;
}

static struct VdlFile *
find_by_name (struct VdlContext *context,
	      const char *name)
{
  name = convert_name (name);
  struct VdlFile *cur;
  for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
    {
      if (vdl_strisequal (cur->name, name))
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

int vdl_elf_map_deps (struct VdlFile *item)
{
  VDL_LOG_FUNCTION ("file=%s", item->name);

  if (item->deps_initialized)
    {
      return 1;
    }
  item->deps_initialized = 1;

  // get list of deps for the input file.
  struct VdlStringList *dt_needed = vdl_elf_get_dt_needed (item);

  // first, map each dep and accumulate them in deps variable
  struct VdlFileList *deps = 0;
  struct VdlStringList *cur;
  for (cur = dt_needed; cur != 0; cur = cur->next)
    {
      // if the file is already mapped within this context,
      // get it and add it to deps
      struct VdlFile *dep = find_by_name (item->context, cur->str);
      if (dep != 0)
	{
	  deps = vdl_file_list_append_one (deps, dep);
	  continue;
	}
      // Search the file in the filesystem
      char *filename = vdl_elf_search_file (cur->str);
      if (filename == 0)
	{
	  VDL_LOG_ERROR ("Could not find %s\n", cur->str);
	  goto error;
	}
      // get information about file.
      struct stat buf;
      if (system_fstat (filename, &buf) == -1)
	{
	  VDL_LOG_ERROR ("Cannot stat %s\n", filename);
	  vdl_free (filename, vdl_strlen (filename)+1);
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
	  deps = vdl_file_list_append_one (deps, dep);
	  vdl_free (filename, vdl_strlen (filename)+1);
	  continue;
	}
      // The file is really not yet mapped so, we have to map it
      dep = vdl_elf_map_single (item->context, filename, cur->str);
      
      // add the new file to the list of dependencies
      deps = vdl_file_list_append_one (deps, dep);

      vdl_free (filename, vdl_strlen (filename)+1);
    }
  vdl_str_list_free (dt_needed);

  // then, recursively map the deps of each dep.
  struct VdlFileList *dep;
  for (dep = deps; dep != 0; dep = dep->next)
    {
      if (!vdl_elf_map_deps (dep->item))
	{
	  goto error;
	}
    }

  // Finally, update the deps
  item->deps = deps;

  return 1;
 error:
  vdl_str_list_free (dt_needed);
  if (deps != 0)
    {
      vdl_file_list_free (deps);
    }
  return 0;
}
int vdl_elf_file_get_info (uint32_t phnum,
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
  if (ro->p_memsz != ro->p_filesz)
    {
      VDL_LOG_ERROR ("file and memory size should be equal: 0x%x != 0x%x\n",
		     ro->p_memsz, ro->p_filesz);
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


  unsigned long ro_start = vdl_align_down (ro->p_vaddr, ro->p_align);
  unsigned long ro_size = vdl_align_up (ro->p_memsz, ro->p_align);
  unsigned long rw_start = vdl_align_down (rw->p_vaddr, rw->p_align);
  unsigned long rw_size = vdl_align_up (rw->p_vaddr+rw->p_filesz-rw_start, rw->p_align);
  unsigned long zero_size = vdl_align_up (rw->p_vaddr+rw->p_memsz-rw_start, rw->p_align) - rw_size;
  unsigned long zero_start = vdl_align_up (rw->p_vaddr + rw->p_filesz, rw->p_align);
  unsigned long ro_file_offset = vdl_align_down (ro->p_offset, ro->p_align);
  unsigned long rw_file_offset = vdl_align_down (rw->p_offset, rw->p_align);
  unsigned long memset_zero_start = rw->p_vaddr+rw->p_filesz;
  unsigned long memset_zero_size = rw_start+rw_size-memset_zero_start;
  if (ro_start + ro_size != rw_start)
    {
      VDL_LOG_ERROR ("ro and rw maps must be adjacent\n", 1);
      goto error;
    }

  info->dynamic = dynamic->p_vaddr;
  info->ro_start = ro_start;
  info->ro_size = ro_size;
  info->rw_start = rw_start;
  info->rw_size = rw_size;
  info->ro_file_offset = ro_file_offset;
  info->rw_file_offset = rw_file_offset;
  info->zero_start = zero_start;
  info->zero_size = zero_size;
  info->memset_zero_start = memset_zero_start;
  info->memset_zero_size = memset_zero_size;

  return 1;
 error:
  return 0;
}

struct VdlFileList *
vdl_elf_gather_all_deps_breadth_first (struct VdlFile *file)
{
  VDL_LOG_FUNCTION ("file=%s", file->name);

  struct VdlFileList *list, *cur;

  list = vdl_new (struct VdlFileList);
  list->item = file;
  list->next = 0;
  for (cur = list; cur != 0; cur = cur->next)
    {
      struct VdlFileList *copy = vdl_file_list_copy (cur->item->deps);
      cur = vdl_file_list_append (cur, copy);
    }

  return list;
}

unsigned long
vdl_elf_hash (const char *n)
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

struct LookupIterator
{
  const char *name;
  signed long current;
  const char *dt_strtab;
  ElfW(Sym) *dt_symtab;
  ElfW(Word) *chain;
};

static struct LookupIterator 
vdl_elf_lookup_begin (const char *name, unsigned long hash,
		      const struct VdlFile *file)
{
  VDL_LOG_FUNCTION ("name=%s, hash=0x%x, file=%s", name, hash, file->filename);
  struct LookupIterator i;
  i.name = name;
  // first, gather information needed to look into the hash table
  ElfW(Word) *dt_hash = (ElfW(Word)*) vdl_elf_file_get_dynamic_p (file, DT_HASH);
  i.dt_strtab = (const char *) vdl_elf_file_get_dynamic_p (file, DT_STRTAB);
  i.dt_symtab = (ElfW(Sym)*) vdl_elf_file_get_dynamic_p (file, DT_SYMTAB);

  if (dt_hash == 0 || i.dt_strtab == 0 || i.dt_symtab == 0)
    {
      i.dt_strtab = 0;
      i.dt_symtab = 0;
      return i;
    }

  // Then, look into the hash table itself.
  // First entry is number of buckets
  // Second entry is number of chains
  ElfW(Word) nbuckets = dt_hash[0];
  i.chain = &dt_hash[2+nbuckets];
  // the code below is tricky: normally, the index of the
  // first entry we want to look at in the hash table is
  // 2+(hash%nbuckets) relative to the dt_hash pointer.
  // what we calculate below is the index in the hash table
  // relative to the chain pointer and the reason we do
  // this is that all other indexes in the hash chain
  // are relative to the chain pointer so, using an index
  // relative to the chain pointer all the time allows us
  // to use the same logic in has_next all the time.
  i.current = -(nbuckets-(hash%nbuckets));
  return i;
}

static int
vdl_elf_lookup_has_next (const struct LookupIterator *i)
{
  if (i->dt_strtab == 0)
    {
      return 0;
    }
  unsigned long prev = i->current;
  unsigned long current = i->chain[i->current];
  while (current != 0)
    {
      // The values stored in the hash table are
      // an index in the symbol table.
      if (i->dt_symtab[current].st_name != 0 && 
	  i->dt_symtab[current].st_shndx != SHN_UNDEF)
	{
	  // the symbol name is an index in the string table
	  if (vdl_strisequal (i->dt_strtab + i->dt_symtab[current].st_name, i->name))
	    {
	      // as an optimization, to save us from iterating again
	      // in the _next function, we set the current position
	      // to the previous entry to find the matching entry 
	      // immediately upon our call to _next.
	      ((struct LookupIterator *)i)->current = prev;
	      return 1;
	    }
	}
      prev = current;
      current = i->chain[current];
    }
  return 0;
}

// return index in dt_symtab
static unsigned long
vdl_elf_lookup_next (struct LookupIterator *i)
{
  // We return the entry immediately following the
  // 'current' index and update the 'current' index
  // to point to the next entry.
  unsigned long next = i->chain[i->current];
  i->current = next;
  return next;
}

// we have a matching symbol but we have a version
// requirement so, we must check that the matching 
// symbol's version also matches.	      
static int
symbol_version_matches (const struct VdlFile *in,
			const struct VdlFile *from,
			const ElfW(Vernaux) *ver_needed,
			unsigned long index)
{
  if (ver_needed == 0)
    {
      // if we have no version requirement, the first matching symbol is ok.
      return 1;
    }
  const char *dt_strtab = (const char *)vdl_elf_file_get_dynamic_p (in, DT_STRTAB);
  ElfW(Half) *dt_versym = (ElfW(Half)*)vdl_elf_file_get_dynamic_p (in, DT_VERSYM);
  ElfW(Verdef) *dt_verdef = (ElfW(Verdef)*)vdl_elf_file_get_dynamic_p (in, DT_VERDEF);
  unsigned long dt_verdefnum = vdl_elf_file_get_dynamic_v (in, DT_VERDEFNUM);
  if (dt_versym == 0)
    {
      // if we have no version definition which could match the requested
      // version, this is not a matching symbol.
      return 0;
    }
  uint16_t ver_index = dt_versym[index];
  if (ver_index == 0)
    {
      // this is a symbol with local scope
      // it's ok if the reference is within the same file.
      return in == from;
    }
  if (ver_index == 1)
    {
      // this is a symbol with global scope. 'base' version definition.
      // XXX: I don't know what this means.
      return 0;
    }
  if (dt_verdef == 0 || dt_verdefnum == 0)
    {
      // if there is no verdef array which could match the requested
      // version, this is not a matching symbol.
      return 0;
    }
  if (ver_index & 0x8000)
    {
      // if the high bit is set, this means that it is a 'hidden' symbol
      // which means that it can't be referenced from outside of its binary.
      if (in != from)
	{
	  // the matching symbol we found is hidden and is located
	  // in a different binary. Not ok.
	  return 0;
	}
    }
  VDL_LOG_DEBUG ("index=%x/%x, %x:%x\n", ver_index, dt_verdefnum,
		 dt_versym, dt_verdef);
  ElfW(Verdef) *cur, *prev;
  for (prev = 0, cur = dt_verdef; cur != prev;
       prev = cur, cur = (ElfW(Verdef)*)(((unsigned long)cur)+cur->vd_next))
    {
      VDL_ASSERT (cur->vd_version == 1, "version number invalid for Verdef");
      if (cur->vd_ndx == ver_index &&
	  cur->vd_hash == ver_needed->vna_hash)
	{
	  // the hash values of the version names are equal.
	  ElfW(Verdaux) *verdaux = (ElfW(Verdaux)*)(((unsigned long)cur)+cur->vd_aux);
	  const char *from_dt_strtab = (const char *)vdl_elf_file_get_dynamic_p (from, DT_STRTAB);
	  if (vdl_strisequal (dt_strtab + verdaux->vda_name, 
			      from_dt_strtab + ver_needed->vna_name))
	    {
	      // the version names are equal.
	      return 1;
	    }
	}
    }
  // the versions don't match.
  return 0;
}

static int
do_symbol_lookup_scope (const char *name, 
			unsigned long hash,
			const struct VdlFile *file,
			const ElfW(Vernaux) *ver_needed,
			enum LookupFlag flags,
			struct VdlFileList *scope,
			struct SymbolMatch *match)
{
  VDL_LOG_FUNCTION ("name=%s, hash=0x%x, scope=%p", name, hash, scope);

  // then, iterate scope until we find the requested symbol.
  struct VdlFileList *cur;
  for (cur = scope; cur != 0; cur = cur->next)
    {
      if (flags & LOOKUP_NO_EXEC && 
	  cur->item->is_executable)
	{
	  // this flag specifies that we should not lookup symbols
	  // in the main executable binary. see the definition of LOOKUP_NO_EXEC
	  continue;
	}
      struct LookupIterator i = vdl_elf_lookup_begin (name, hash, cur->item);
      while (vdl_elf_lookup_has_next (&i))
	{
	  unsigned long index = vdl_elf_lookup_next (&i);
	  if (symbol_version_matches (cur->item, file, ver_needed, index))
	    {
	      match->file = cur->item;
	      match->symbol = &i.dt_symtab[index];
	      return 1;
	    }
	}
    }
  return 0;
}

int 
vdl_elf_symbol_lookup (const char *name, 
		       const struct VdlFile *file,
		       const ElfW(Vernaux) *ver,
		       enum LookupFlag flags,
		       struct SymbolMatch *match)
{
  // calculate the hash here to avoid calculating 
  // it twice in both calls to symbol_lookup
  unsigned long hash = vdl_elf_hash (name);

  // lookup the symbol in the global scope first
  int ok = do_symbol_lookup_scope (name, hash, file, ver,
				   flags, file->context->global_scope, match);
  if (!ok)
    {
      // and in the local scope.
      ok = do_symbol_lookup_scope (name, hash, file, ver,
				   flags, file->local_scope, match);
    }
  return ok;
}
unsigned long 
vdl_elf_symbol_lookup_local (const char *name, const struct VdlFile *file)
{
  unsigned long hash = vdl_elf_hash (name);
  struct LookupIterator i = vdl_elf_lookup_begin (name, hash, file);
  while (vdl_elf_lookup_has_next (&i))
    {
      unsigned long index = vdl_elf_lookup_next (&i);
      return file->load_base + i.dt_symtab[index].st_value;
    }
  return 0;
}


// the glibc elf loader passes all 3 arguments
// to the initialization functions and the libc initializer
// function makes use of these arguments to initialize
// __libc_argc, __libc_argv, and, __environ so, we do the
// same for compatibility purposes.
typedef void (*init_function) (int, char **, char **);

static void
vdl_elf_call_init_one (struct VdlFile *file)
{
  VDL_LOG_FUNCTION ("file=%s", file->name);
  // Gather information from the .dynamic section
  unsigned long dt_init = vdl_elf_file_get_dynamic_p (file, DT_INIT);
  unsigned long dt_init_array = vdl_elf_file_get_dynamic_p (file, DT_INIT_ARRAY);
  unsigned long dt_init_arraysz = vdl_elf_file_get_dynamic_v (file, DT_INIT_ARRAYSZ);
  // First, invoke the old-style DT_INIT function.
  // The address of the function to call is stored in
  // the DT_INIT tag, here: dt_init.
  if (dt_init != 0)
    {
      init_function init = (init_function) dt_init;
      init (file->context->argc, file->context->argv, file->context->envp);
    }

  // Then, invoke the newer DT_INIT_ARRAY functions.
  // The address of the functions to call is stored as
  // an array of pointers pointed to by DT_INIT_ARRAY
  if (dt_init_array != 0)
    {
      init_function *init = (init_function *) dt_init_array;
      int i;
      for (i = 0; i < dt_init_arraysz / sizeof (init_function); i++, init++)
	{
	  (*(init[i])) (file->context->argc, file->context->argv, file->context->envp);
	}
    }
}

void vdl_elf_call_init (struct VdlFile *file)
{
  VDL_LOG_FUNCTION ("file=%s", file->name);
  if (file->init_called)
    {
      // if we are initialized already, no need to do any work
      return;
    }
  // mark the file as initialized
  file->init_called = 1;

  // iterate over all deps first before initialization.
  struct VdlFileList *cur;
  for (cur = file->deps; cur != 0; cur = cur->next)
    {
      vdl_elf_call_init (cur->item);
    }

  // Now that all deps are initialized, initialize ourselves.
  vdl_elf_call_init_one (file);  
}
unsigned long vdl_elf_get_entry_point (struct VdlFile *file)
{
  // This piece of code assumes that the ELF header starts at the
  // first byte of the ro map. This is verified in vdl_elf_file_get_info
  // so we are safe with this assumption.
  ElfW(Ehdr) *header = (ElfW(Ehdr)*) file->ro_start;
  return header->e_entry + file->load_base;
}
static ElfW(Vernaux) *
sym_to_vernaux (unsigned long index,
		ElfW(Half) *dt_versym, 
		ElfW(Verneed) *dt_verneed,
		unsigned long dt_verneednum)
{
  if (dt_versym != 0 && dt_verneed != 0 && dt_verneednum != 0)
    {
      // the same offset used to look in the symbol table (dt_symtab)
      // is an offset in the version table (dt_versym).
      // dt_versym contains a set of 15bit indexes and 
      // 1bit flags packed into 16 bits. When the upper bit is
      // set, the associated symbol is 'hidden', that is, it
      // cannot be referenced from outside of the object.
      ElfW(Half) ver_ndx = dt_versym[index];
      if (ver_ndx & 0x8000)
	{
	  return 0;
	}
      // search the version needed whose vd_ndx is equal to ver_ndx.
      ElfW(Verneed) *cur, *prev;
      for (cur = dt_verneed, prev = 0; 
	   cur != prev; 
	   prev = cur, cur = (ElfW(Verneed) *)(((unsigned long)cur)+cur->vn_next))
	{
	  VDL_ASSERT (cur->vn_version == 1, "version number invalid for Verneed");
	  ElfW(Vernaux) *cur_aux, *prev_aux;
	  for (cur_aux = (ElfW(Vernaux)*)(((unsigned long)cur)+cur->vn_aux), prev_aux = 0;
	       cur_aux != prev_aux; 
	       prev_aux = cur_aux, cur_aux = (ElfW(Vernaux)*)(((unsigned long)cur_aux)+cur_aux->vna_next))
	    {
	      if (cur_aux->vna_other == ver_ndx)
		{
		  return cur_aux;
		}
	    }
	}
    }
  return 0;
}

void
vdl_elf_iterate_pltrel (struct VdlFile *file, 
			void (*cb)(const struct VdlFile *file,
				   const ElfW(Rel) *rel,
				   const ElfW(Sym) *sym,
				   const ElfW(Vernaux) *ver,
				   const char *name))
{
  VDL_LOG_FUNCTION ("file=%s", file->name);
  ElfW(Rel) *dt_jmprel = (ElfW(Rel)*)vdl_elf_file_get_dynamic_p (file, DT_JMPREL);
  unsigned long dt_pltrel = vdl_elf_file_get_dynamic_v (file, DT_PLTREL);
  unsigned long dt_pltrelsz = vdl_elf_file_get_dynamic_v (file, DT_PLTRELSZ);
  const char *dt_strtab = (const char *)vdl_elf_file_get_dynamic_p (file, DT_STRTAB);
  ElfW(Sym) *dt_symtab = (ElfW(Sym)*)vdl_elf_file_get_dynamic_p (file, DT_SYMTAB);
  ElfW(Half) *dt_versym = (ElfW(Half)*)vdl_elf_file_get_dynamic_p (file, DT_VERSYM);
  ElfW(Verneed) *dt_verneed = (ElfW(Verneed)*)vdl_elf_file_get_dynamic_p (file, DT_VERNEED);
  unsigned long dt_verneednum = vdl_elf_file_get_dynamic_v (file, DT_VERNEEDNUM);
  
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
      ElfW(Vernaux) *ver = sym_to_vernaux (ELFW_R_SYM (rel->r_info),
					   dt_versym, dt_verneed, dt_verneednum);
      const char *symbol_name;
      if (sym->st_name == 0)
	{
	  symbol_name = 0;
	}
      else
	{
	  symbol_name = dt_strtab + sym->st_name;
	}
      (*cb) (file, rel, sym, ver, symbol_name);
    }
}

void
vdl_elf_iterate_rel (struct VdlFile *file, 
		     void (*cb)(const struct VdlFile *file,
				const ElfW(Rel) *rel,
				const ElfW(Sym) *sym,
				const ElfW(Vernaux) *ver,
				const char *symbol_name))
{
  VDL_LOG_FUNCTION ("file=%s", file->name);
  ElfW(Rel) *dt_rel = (ElfW(Rel)*)vdl_elf_file_get_dynamic_p (file, DT_REL);
  unsigned long dt_relsz = vdl_elf_file_get_dynamic_v (file, DT_RELSZ);
  unsigned long dt_relent = vdl_elf_file_get_dynamic_v (file, DT_RELENT);
  const char *dt_strtab = (const char *)vdl_elf_file_get_dynamic_p (file, DT_STRTAB);
  ElfW(Sym) *dt_symtab = (ElfW(Sym)*)vdl_elf_file_get_dynamic_p (file, DT_SYMTAB);
  ElfW(Half) *dt_versym = (ElfW(Half)*)vdl_elf_file_get_dynamic_p (file, DT_VERSYM);
  ElfW(Verneed) *dt_verneed = (ElfW(Verneed)*)vdl_elf_file_get_dynamic_p (file, DT_VERNEED);
  unsigned long dt_verneednum = vdl_elf_file_get_dynamic_v (file, DT_VERNEEDNUM);
  if (dt_rel == 0 || dt_relsz == 0 || dt_relent == 0 ||
      dt_strtab == 0 || dt_symtab == 0)
    {
      return;
    }
  uint32_t i;
  for (i = 0; i < dt_relsz/dt_relent; i++)
    {
      ElfW(Rel) *rel = &dt_rel[i];
      ElfW(Sym) *sym = &dt_symtab[ELFW_R_SYM (rel->r_info)];
      ElfW(Vernaux) *ver = sym_to_vernaux (ELFW_R_SYM (rel->r_info),
					   dt_versym, dt_verneed, dt_verneednum);
      const char *symbol_name;
      if (sym->st_name == 0)
	{
	  symbol_name = 0;
	}
      else
	{
	  symbol_name = dt_strtab + sym->st_name;
	}
      (*cb) (file, rel, sym, ver, symbol_name);
    }
}

void vdl_elf_reloc (struct VdlFile *file)
{
  if (file->reloced)
    {
      return;
    }
  file->reloced = 1;

  // relocate dependencies first:
  struct VdlFileList *cur;
  for (cur = file->deps; cur != 0; cur = cur->next)
    {
      vdl_elf_reloc (cur->item);
    }

  vdl_elf_iterate_rel (file, machine_perform_relocation);
  if (g_vdl.bind_now)
    {
      // perform PLT relocs _now_
      vdl_elf_iterate_pltrel (file, machine_perform_relocation);
    }
  else
    {
      // setup lazy binding by setting the GOT entries 2 and 3.
      // Entry 2 is set to a pointer to the associated VdlFile
      // Entry 3 is set to the asm trampoline vdl_symbol_lookup_asm
      // which calls vdl_symbol_lookup.
    }
}

static unsigned long
vdl_elf_allocate_tls_index (void)
{
  // This is the slowest but simplest implementation possible
  // For each possible module index, we try to find a module
  // which has been already assigned that module index.
  // If it has been already assigned, we try another one, otherwise,
  // we return it.
  unsigned long i;
  unsigned long ul_max = 0;
  ul_max = ~ul_max;
  for (i = 1; i < ul_max; i++)
    {
      struct VdlFile *cur;
      for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
	{
	  if (cur->has_tls && cur->tls_index == i)
	    {
	      break;
	    }
	}
      if (cur == 0)
	{
	  return i;
	}
    }
  VDL_ASSERT (0, "All tls module indexes are used up ? impossible !");
  return 0; // quiet compiler
}

void vdl_elf_tls (struct VdlFile *file)
{
  if (file->tls_initialized)
    {
      return;
    }
  file->tls_initialized = 1;

  ElfW(Ehdr) *header = (ElfW(Ehdr) *)file->ro_start;
  ElfW(Phdr) *phdr = (ElfW(Phdr) *) (file->ro_start + header->e_phoff);
  ElfW(Phdr) *pt_tls = vdl_elf_search_phdr (phdr, header->e_phnum, PT_TLS);
  if (pt_tls == 0)
    {
      file->has_tls = 0;
      return;
    }
  file->has_tls = 1;
  file->tls_tmpl_start = file->ro_start + pt_tls->p_offset;
  file->tls_tmpl_size = pt_tls->p_filesz;
  file->tls_init_zero_size = pt_tls->p_memsz - pt_tls->p_filesz;
  file->tls_align = pt_tls->p_align;
  file->tls_index = vdl_elf_allocate_tls_index ();

  struct VdlFileList *cur;
  for (cur = file->deps; cur != 0; cur = cur->next)
    {
      vdl_elf_tls (cur->item);
    }
}
