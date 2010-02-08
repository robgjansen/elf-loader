#include "machine.h"
#include "vdl.h"
#include "vdl-utils.h"
#include "vdl-log.h"
#include "vdl-reloc.h"
#include "vdl-lookup.h"
#include "vdl-config.h"
#include "syscall.h"
#include <sys/mman.h>
#include <asm/ldt.h>

bool machine_reloc_is_relative (unsigned long reloc_type)
{
  return reloc_type == R_386_RELATIVE;
}
bool machine_reloc_is_copy (unsigned long reloc_type)
{
  return reloc_type == R_386_COPY;
}
void machine_reloc_without_match (struct VdlFile *file,
				  unsigned long *reloc_addr,
				  unsigned long reloc_type,
				  unsigned long reloc_addend,
				  const ElfW(Sym) *sym)
{
  VDL_LOG_ASSERT (reloc_addend == 0, "i386 does not use addends");
  if (reloc_type == R_386_RELATIVE)
    {
      *reloc_addr += file->load_base;
    }
  else if (reloc_type == R_386_TLS_TPOFF)
    {
      *reloc_addr += file->tls_offset + sym->st_value;
    }
  else if (reloc_type == R_386_TLS_DTPMOD32)
    {
      *reloc_addr = file->tls_index;
    }
  else if (reloc_type == R_386_TLS_DTPOFF32)
    {
      *reloc_addr = sym->st_value;
    }
  else
    {
      VDL_LOG_ASSERT (0, "unhandled reloc type: %s", 
		      machine_reloc_type_to_str (reloc_type));
    }
}

void
machine_reloc_with_match (unsigned long *reloc_addr,
			  unsigned long reloc_type,
			  unsigned long reloc_addend,
			  const struct VdlLookupResult *match)
{
  VDL_LOG_ASSERT (reloc_addend == 0, "i386 does not use addends");

  if (reloc_type == R_386_GLOB_DAT ||
      reloc_type == R_386_JMP_SLOT ||
      reloc_type == R_386_32)
    {
      *reloc_addr = match->file->load_base + match->symbol->st_value;
    }
  else if (reloc_type == R_386_TLS_TPOFF)
    {
      VDL_LOG_ASSERT (match->file->has_tls,
		      "Module which contains target symbol does "
		      "not have a TLS block ??");
      VDL_LOG_ASSERT (ELFW_ST_TYPE (match->symbol->st_info) == STT_TLS,
		      "Target symbol is not a tls symbol ??");
      *reloc_addr += match->file->tls_offset + match->symbol->st_value;
    }
  else if (reloc_type == R_386_TLS_DTPMOD32)
    {
      VDL_LOG_ASSERT (match->file->has_tls,
		      "Module which contains target symbol does "
		      "not have a TLS block ??");
      *reloc_addr = match->file->tls_index;
    }
  else if (reloc_type == R_386_TLS_DTPOFF32)
    {
      VDL_LOG_ASSERT (match->file->has_tls,
		      "Module which contains target symbol does "
		      "not have a TLS block ??");
      *reloc_addr = match->symbol->st_value;
    }
  else
    {
      VDL_LOG_ASSERT (0, "unhandled reloc type: %s", 
		      machine_reloc_type_to_str (reloc_type));
    }
}

const char *machine_reloc_type_to_str (unsigned long reloc_type)
{
#define ITEM(x)					\
  case R_##x:					\
    return "R_" #x ;				\
  break
  switch (reloc_type) {
    ITEM(386_NONE);
    ITEM(386_32);
    ITEM(386_PC32);
    ITEM(386_GOT32);
    ITEM(386_PLT32);
    ITEM(386_COPY);
    ITEM(386_GLOB_DAT);
    ITEM(386_JMP_SLOT);
    ITEM(386_RELATIVE);
    ITEM(386_GOTOFF);
    ITEM(386_GOTPC);
    ITEM(386_32PLT);
    ITEM(386_TLS_TPOFF);
    ITEM(386_TLS_IE);
    ITEM(386_TLS_GOTIE);
    ITEM(386_TLS_LE);
    ITEM(386_TLS_GD);
    ITEM(386_TLS_LDM);
    ITEM(386_16);
    ITEM(386_PC16);
    ITEM(386_8);
    ITEM(386_PC8);
    ITEM(386_TLS_GD_32);
    ITEM(386_TLS_GD_PUSH);
    ITEM(386_TLS_GD_CALL);
    ITEM(386_TLS_GD_POP);
    ITEM(386_TLS_LDM_32);
    ITEM(386_TLS_LDM_PUSH);
    ITEM(386_TLS_LDM_CALL);
    ITEM(386_TLS_LDM_POP);
    ITEM(386_TLS_LDO_32);
    ITEM(386_TLS_IE_32);
    ITEM(386_TLS_LE_32);
    ITEM(386_TLS_DTPMOD32);
    ITEM(386_TLS_DTPOFF32);
    ITEM(386_TLS_TPOFF32);
    ITEM(386_NUM);
  default:
    return "XXX";
  }
#undef ITEM
}

extern void machine_resolve_trampoline (struct VdlFile *file, unsigned long offset);
void machine_lazy_reloc (struct VdlFile *file)
{
  VDL_LOG_FUNCTION ("file=%s", file->name);
  // setup lazy binding by setting the GOT entries 2 and 3
  // as specified by the ELF i386 ABI.
  // Entry 2 is set to a pointer to the associated VdlFile
  // Entry 3 is set to the asm trampoline machine_resolve_trampoline
  unsigned long dt_pltgot = vdl_file_get_dynamic_p (file, DT_PLTGOT);
  unsigned long dt_jmprel = vdl_file_get_dynamic_p (file, DT_JMPREL);
  unsigned long dt_pltrel = vdl_file_get_dynamic_v (file, DT_PLTREL);
  unsigned long dt_pltrelsz = vdl_file_get_dynamic_v (file, DT_PLTRELSZ);

  if (dt_pltgot == 0 || 
      dt_pltrel != DT_REL || 
      dt_pltrelsz == 0 || 
      dt_jmprel == 0)
    {
      return;
    }
  // if this platform does prelinking, the prelinker has stored
  // a pointer to plt + 0x16 in got[1]. Otherwise, got[1] is zero
  unsigned long *got = (unsigned long *) dt_pltgot;
  unsigned long plt = got[1];
  got[1] = (unsigned long)file;
  got[2] = (unsigned long) machine_resolve_trampoline;

  int i;
  for (i = 0; i < dt_pltrelsz/sizeof(ElfW(Rel)); i++)
    {
      ElfW(Rel) *rel = &(((ElfW(Rel)*)dt_jmprel)[i]);
      unsigned long reloc_addr = rel->r_offset + file->load_base;
      unsigned long *preloc_addr = (unsigned long*) reloc_addr;
      if (plt == 0)
	{
	  // we are not prelinked
	  *preloc_addr += file->load_base;
	}
      else
	{
	  // we are prelinked so, we have to redo the work done by the 
	  // compile-time linker: we calculate the address of the 
	  // instruction right after the jump of PLT[i]
	  *preloc_addr = file->load_base + plt +  
	    (reloc_addr - (dt_pltgot + 3*4)) * 4;
	}
    }
}

bool machine_insert_trampoline (unsigned long from, unsigned long to, 
				unsigned long from_size)
{
  VDL_LOG_FUNCTION ("from=0x%lx, to=0x%lx, from_size=0x%lx", 
		    from, to, from_size);
  if (from_size < 5)
    {
      return false;
    }
  // In this code, we assume that the target symbol is bigger than
  // our jump and that none of that code is running yet so, we don't have
  // to worry about modifying a piece of code which is running already.
  unsigned long page_start = from / 4096 * 4096;
  int status = system_mprotect ((void*)page_start, 4096, PROT_WRITE);
  if (status != 0)
    {
      return false;
    }
  signed long delta = to;
  delta -= from + 5;
  unsigned long delta_unsigned = delta;
  unsigned char *buffer = (unsigned char *)from;
  buffer[0] = 0xe9;
  buffer[1] = (delta_unsigned >> 0) & 0xff;
  buffer[2] = (delta_unsigned >> 8) & 0xff;
  buffer[3] = (delta_unsigned >> 16) & 0xff;
  buffer[4] = (delta_unsigned >> 24) & 0xff;
  status = system_mprotect ((void *)page_start, 4096, PROT_READ | PROT_EXEC);
  return status == 0;
}

void machine_thread_pointer_set (unsigned long tp)
{
  struct user_desc desc;
  vdl_utils_memset (&desc, 0, sizeof (desc));
  desc.entry_number = -1; // ask kernel to allocate an entry number
  desc.base_addr = tp;
  desc.limit = 0xfffff; // maximum memory address in number of pages (4K) -> 4GB
  desc.seg_32bit = 1;

  desc.contents = 0;
  desc.read_exec_only = 0;
  desc.limit_in_pages = 1;
  desc.seg_not_present = 0;
  desc.useable = 1;
  
  int status = SYSCALL1 (set_thread_area, &desc);
  VDL_LOG_ASSERT (status == 0, "Unable to set TCB");

  // set_thread_area allocated an entry in the GDT and returned
  // the index associated to this entry. So, now, we associate
  // %gs with this newly-allocated entry.
  // Bits 3 to 15 indicate the entry index.
  // Bit 2 is set to 0 to indicate that we address the GDT through
  // this segment selector.
  // Bits 0 to 1 indicate the privilege level requested (here, 3,
  // is the least privileged level)
  int gs = (desc.entry_number << 3) | 3;
  asm ("movw %w0, %%gs" :: "q" (gs));
}

unsigned long machine_thread_pointer_get (void)
{
  unsigned long value;
  asm ("movl %%gs:0,%0" : "=r" (value) :);
  return value;
}

uint32_t machine_atomic_compare_and_exchange (uint32_t *ptr, uint32_t old, uint32_t new)
{
  uint32_t prev;
  asm volatile ("lock cmpxchgl %1,%2"
		: "=a"(prev)
		: "r"(new), "m"(*ptr), "0"(old)
		: "memory");
  return prev;
}

uint32_t machine_atomic_dec (uint32_t *ptr)
{
  int32_t prev = -1;
  asm volatile ("lock xadd %0,%1\n"
		:"=q"(prev)
	        :"m"(*ptr), "0"(prev)
		:"memory", "cc");
  return prev;
}

const char *
machine_get_system_search_dirs (void)
{
  
  static const char *dirs = 
    // XXX: first is for my ubuntu box.
    "/lib/tls/i686/cmov:"
    "/lib/tls:" 
    "/lib/i686:"
    "/lib:" 
    "/lib32:"
    "/usr/lib:"
    "/usr/lib32";
  return dirs;
}

const char *machine_get_lib (void)
{
  return "lib";
}

void *machine_system_mmap(void *start, size_t length, int prot, int flags, int fd, off_t offset)
{
  int status = SYSCALL6(mmap2, start, length, prot, flags, fd, offset / 4096);
  if (status < 0 && status > -256)
    {
      return MAP_FAILED;
    }
  return (void*)status;
}
