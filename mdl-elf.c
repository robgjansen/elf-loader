#include "mdl-elf.h"
#include "mdl.h"
#include "system.h"
#include <unistd.h>
#include <sys/mman.h>

ElfW(Phdr) *mdl_elf_search_phdr (ElfW(Phdr) *phdr, int phnum, int type)
{
  MDL_LOG_FUNCTION;
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
  MDL_LOG_FUNCTION;
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
  MDL_LOG_FUNCTION;
  struct StringList *cur;
  for (cur = g_mdl.search_dirs; cur != 0; cur = cur->next)
    {
      char *fullname = mdl_strconcat (cur->str, "/", name, 0);
      MDL_LOG_DEBUG ("test file=%s\n", fullname);
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
struct MappedFile *mdl_elf_load_single (const char *filename, const char *name)
{
  MDL_LOG_FUNCTION;
  ElfW(Ehdr) header;
  ElfW(Phdr) *phdr = 0;
  int i;
  size_t bytes_read;
  int fd = -1;
  unsigned long map_start = -1;
  unsigned long map_size = 0;

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

  ElfW(Phdr) *ro = 0;
  ElfW(Phdr) *rw = 0;
  ElfW(Phdr) *pt_phdr = 0;
  ElfW(Phdr) *dynamic = 0;
  for (i = 0; i != header.e_phnum; i++)
    {
      ElfW(Phdr) *tmp = &(phdr[i]);
      if (tmp->p_type == PT_LOAD)
	{
	  if (tmp->p_flags & PF_W)
	    {
	      if (rw != 0)
		{
		  MDL_LOG_ERROR ("More than one rw header in %s\n", filename);
		  goto error;
		}
	      rw = tmp;
	    }
	  else
	    {
	      if (rw != 0)
		{
		  MDL_LOG_ERROR ("More than one ro header in %s\n", filename);
		  goto error;
		}
	      ro = tmp;
	    }
	} 
      else if (tmp->p_type == PT_PHDR)
	{
	  pt_phdr = tmp;
	}
      else if (tmp->p_type == PT_DYNAMIC)
	{
	  dynamic = tmp;
	}
    }
  if (ro == 0 || rw == 0 || dynamic == 0 || pt_phdr == 0)
    {
      MDL_LOG_ERROR ("missing program entries in %s\n", filename);
      goto error;
    }
  if (ro->p_align != rw->p_align)
    {
      MDL_LOG_ERROR ("something is fishy about the alignment constraints in %s\n", filename);
      goto error;
    }
  if (ro->p_offset + ro->p_filesz > rw->p_offset ||
      ALIGN_UP (ro->p_vaddr+ro->p_memsz, ro->p_align) != ALIGN_DOWN (rw->p_vaddr, rw->p_align))
    {
      // ro must be located exactly before rw.
      MDL_LOG_ERROR ("rw/ro error in %s\n", filename);
      goto error;
    }
  if (pt_phdr->p_offset < ro->p_offset || pt_phdr->p_filesz > ro->p_filesz)
    {
      MDL_LOG_ERROR ("phdr not included in ro load in %s\n", filename);
      goto error;
    }
  if (dynamic->p_offset < rw->p_offset || dynamic->p_filesz > rw->p_filesz)
    {
      MDL_LOG_ERROR ("dynamic not included in rw load in %s\n", filename);
      goto error;
    }
  // we do a single mmap for both ro and rw entries as ro, and, then
  // use mprotect to give the 'w' power to the second entry. This is needed
  // to ensure that the relative position of both entries is preserved for
  // DYN files. For EXEC files, we don't care about the relative position
  // but we need to ensure that the absolute position is correct so we add the 
  // MAP_FIXED flag.

  int fixed = (header.e_type == ET_EXEC)?MAP_FIXED:0;
  unsigned long requested_map_start = ALIGN_DOWN (ro->p_vaddr, ro->p_align);
  unsigned long map_offset = ALIGN_DOWN(ro->p_offset, ro->p_align);
  map_size = ALIGN_UP (rw->p_vaddr+rw->p_memsz-ro->p_vaddr, ro->p_align);
  map_start = (unsigned long) system_mmap ((void*)requested_map_start,
					   map_size,
					   PROT_READ, MAP_PRIVATE | fixed, fd, 
					   map_offset);
  if (map_start == -1)
    {
      MDL_LOG_ERROR ("Unable to perform mapping for %s\n", filename);
      goto error;
    }
  // calculate the offset between the start address we asked for and the one we got
  unsigned long load_base = map_start - requested_map_start;
  if (fixed && load_base != 0)
    {
      MDL_LOG_ERROR ("We need a fixed address and we did not get it in %s\n", filename);
      goto error;
    }

  // make the rw map actually writable.
  if (system_mprotect (((void*)(ALIGN_DOWN (rw->p_vaddr, rw->p_align) + load_base)), 
		       ALIGN_UP (rw->p_memsz, rw->p_align), PROT_READ | PROT_WRITE) == -1)
    {
      MDL_LOG_ERROR ("Unable to add w flag to rw mapping for file=%s 0x%x:0x%x\n", 
		     filename, ALIGN_DOWN (rw->p_vaddr, rw->p_align) + load_base,
		     ALIGN_UP (rw->p_memsz, rw->p_align));
      goto error;
    }

  // Now that we are done perfoming the mapping, store all the values somewhere
  struct MappedFile *file = mdl_new (struct MappedFile);
  file->load_base = load_base;
  file->filename = mdl_strdup (name);
  file->dynamic = dynamic->p_vaddr + load_base;
  file->next = 0;
  file->prev = 0;
  file->count = 1;
  file->ro_map = (void*)map_start;
  file->ro_map_size = ALIGN_UP (ro->p_memsz, ro->p_align);
  file->rw_map = (void*)ALIGN_DOWN (rw->p_vaddr, rw->p_align) + load_base;
  file->rw_map_size = ALIGN_UP (rw->p_memsz, rw->p_align);
  file->init_called = 0;
  file->fini_called = 0;

  MDL_LOG_DEBUG ("mapped file %s ro=0x%x:0x%x, rw=0x%x:0x%x\n", filename,
		 file->ro_map, file->ro_map_size, 
		 file->rw_map, file->rw_map_size);
  
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
