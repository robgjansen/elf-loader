#include "machine.h"
#include "vdl.h"
#include "vdl-utils.h"
#include "vdl-log.h"
#include "vdl-reloc.h"
#include "vdl-lookup.h"
#include "syscall.h"
#include <sys/mman.h>
#include <sys/mman.h>
#include <asm/prctl.h> // for ARCH_SET_FS

bool machine_reloc_is_relative (unsigned long reloc_type)
{
  return reloc_type == R_X86_64_RELATIVE;
}
bool machine_reloc_is_copy (unsigned long reloc_type)
{
  return reloc_type == R_X86_64_COPY;
}
void machine_reloc_without_match (struct VdlFile *file,
				  unsigned long *reloc_addr,
				  unsigned long reloc_type,
				  unsigned long reloc_addend,
				  const ElfW(Sym) *sym)
{
  if (reloc_type == R_X86_64_RELATIVE)
    {
      *reloc_addr = file->load_base + reloc_addend;
    }
  else if (reloc_type == R_X86_64_TPOFF64)
    {
      *reloc_addr += file->tls_offset + sym->st_value + reloc_addend;
    }
  else if (reloc_type == R_X86_64_DTPMOD64)
    {
      *reloc_addr = file->tls_index;
    }
  else if (reloc_type == R_X86_64_DTPOFF64)
    {
      *reloc_addr = sym->st_value + reloc_addend;
    }
  else
    {
      VDL_LOG_ASSERT (0, "unhandled reloc type");
    }
}
void
machine_reloc_with_match (unsigned long *reloc_addr,
			  unsigned long reloc_type,
			  unsigned long reloc_addend,
			  const struct VdlLookupResult *match)
{
  if (reloc_type == R_X86_64_GLOB_DAT ||
      reloc_type == R_X86_64_JUMP_SLOT ||
      reloc_type == R_X86_64_64)
    {
      *reloc_addr = match->file->load_base + match->symbol->st_value + reloc_addend;
    }
  else if (reloc_type == R_X86_64_TPOFF64)
    {
      VDL_LOG_ASSERT (match->file->has_tls,
		      "Module which contains target symbol does not have a TLS block ??");
      VDL_LOG_ASSERT (ELFW_ST_TYPE (match->symbol->st_info) == STT_TLS,
		      "Target symbol is not a tls symbol ??");
      *reloc_addr += match->file->tls_offset + match->symbol->st_value + reloc_addend;
    }
  else if (reloc_type == R_X86_64_DTPMOD64)
    {
      VDL_LOG_ASSERT (match->file->has_tls,
		      "Module which contains target symbol does not have a TLS block ??");
      *reloc_addr = match->file->tls_index;
    }
  else if (reloc_type == R_X86_64_DTPOFF64)
    {
      VDL_LOG_ASSERT (match->file->has_tls,
		      "Module which contains target symbol does not have a TLS block ??");
      *reloc_addr = match->symbol->st_value + reloc_addend;
    }
  else
    {
      VDL_LOG_ASSERT (0, "unhandled reloc type");
    }
}
const char *machine_reloc_type_to_str (unsigned long reloc_type)
{
#define ITEM(x)					\
  case R_##x:					\
    return "R_" #x ;				\
  break
  switch (reloc_type) {
    ITEM(X86_64_NONE);
    ITEM(X86_64_64);
    ITEM(X86_64_PC32);
    ITEM(X86_64_GOT32);
    ITEM(X86_64_PLT32);
    ITEM(X86_64_COPY);
    ITEM(X86_64_GLOB_DAT);
    ITEM(X86_64_JUMP_SLOT);
    ITEM(X86_64_RELATIVE);
    ITEM(X86_64_GOTPCREL);
    ITEM(X86_64_32);
    ITEM(X86_64_32S);
    ITEM(X86_64_16);
    ITEM(X86_64_PC16);
    ITEM(X86_64_8);
    ITEM(X86_64_PC8);
    ITEM(X86_64_DTPMOD64);
    ITEM(X86_64_DTPOFF64);
    ITEM(X86_64_TPOFF64);
    ITEM(X86_64_TLSGD);
    ITEM(X86_64_TLSLD);
    ITEM(X86_64_DTPOFF32);
    ITEM(X86_64_GOTTPOFF);
    ITEM(X86_64_TPOFF32);
    ITEM(X86_64_PC64);
    ITEM(X86_64_GOTOFF64);
    ITEM(X86_64_GOTPC32);
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
  // as specified by the ELF x86_64 ABI. It's the same
  // as the i386 ABI here.
  // Entry 2 is set to a pointer to the associated VdlFile
  // Entry 3 is set to the asm trampoline machine_resolve_trampoline
  unsigned long dt_pltgot = vdl_file_get_dynamic_p (file, DT_PLTGOT);
  unsigned long dt_jmprel = vdl_file_get_dynamic_p (file, DT_JMPREL);
  unsigned long dt_pltrel = vdl_file_get_dynamic_v (file, DT_PLTREL);
  unsigned long dt_pltrelsz = vdl_file_get_dynamic_v (file, DT_PLTRELSZ);

  if (dt_pltgot == 0 || 
      (dt_pltrel != DT_REL && dt_pltrel != DT_RELA) || 
      dt_pltrelsz == 0 || 
      dt_jmprel == 0)
    {
      return;
    }
  VDL_LOG_ASSERT (dt_pltrel == DT_RELA, "x86-64 uses rela entries");
  // If this platform does prelinking, the prelinker has stored
  // a pointer to plt + 0x16 in got[1]. Otherwise, got[1] is zero.
  // No, there is no documentation about this other than the code
  // of the compile-time linker (actually, bfd), dynamic loader and 
  // prelinker.
  unsigned long *got = (unsigned long *) dt_pltgot;
  unsigned long plt = got[1];
  got[1] = (unsigned long)file;
  got[2] = (unsigned long) machine_resolve_trampoline;

  int i;
  for (i = 0; i < dt_pltrelsz/sizeof(ElfW(Rela)); i++)
    {
      ElfW(Rela) *rela = &(((ElfW(Rela)*)dt_jmprel)[i]);
      unsigned long reloc_addr = rela->r_offset + file->load_base;
      unsigned long *preloc_addr = (unsigned long*) reloc_addr;
      if (plt == 0)
	{
	  // we are not prelinked
	  *preloc_addr += file->load_base;
	}
      else
	{
	  // we are prelinked so, we have to redo the work done by the compile-time
	  // linker: we calculate the address of the instruction right after the
	  // jump of PLT[i]
	  *preloc_addr = file->load_base + plt +  (reloc_addr - (dt_pltgot + 3*8)) * 2;
	}
    }
}
bool machine_insert_trampoline (unsigned long from, unsigned long to, unsigned long from_size)
{
  VDL_LOG_FUNCTION ("from=0x%lx, to=0x%lx, from_size=0x%lx", from, to, from_size);
  if (from_size < 14)
    {
      return false;
    }
  // In this code, we assume that the target symbol is bigger than
  // our jump and that none of that code is running yet so, we don't have
  // to worry about modifying a piece of code which is running already.
  unsigned long page_start = from / 4096 * 4096;
  system_mprotect ((void*)page_start, 4096, PROT_WRITE);
  unsigned char *buffer = (unsigned char *)(from);
  buffer[0] = 0xff;
  buffer[1] = 0x25;
  buffer[2] = 0;
  buffer[3] = 0;
  buffer[4] = 0;
  buffer[5] = 0;
  buffer[6] = (to >> 0) & 0xff;
  buffer[7] = (to >> 8) & 0xff;
  buffer[8] = (to >> 16) & 0xff;
  buffer[9] = (to >> 24) & 0xff;
  buffer[10] = (to >> 32) & 0xff;
  buffer[11] = (to >> 40) & 0xff;
  buffer[12] = (to >> 48) & 0xff;
  buffer[13] = (to >> 56) & 0xff;
  system_mprotect ((void *)page_start, 4096, PROT_READ | PROT_EXEC);
  return true;
}
void machine_thread_pointer_set (unsigned long tp)
{
  unsigned long fs = tp;
  int status = SYSCALL2 (arch_prctl, ARCH_SET_FS, fs);
  VDL_LOG_DEBUG ("status=%d\n", status);
  VDL_LOG_ASSERT (status == 0, "Unable to set TP");  
}
unsigned long machine_thread_pointer_get (void)
{
  unsigned long value = 0;
  asm ("mov %%fs:0,%0" : "=r" (value) :);
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
    "/lib64:" 
    "/usr/lib64";
  return dirs;
}
const char *machine_get_lib (void)
{
  return "lib64";
}
void *machine_system_mmap(void *start, size_t length, int prot, int flags, int fd, off_t offset)
{
  long int status = SYSCALL6(mmap, start, length, prot, flags, fd, offset);
  if (status < 0 && status > -4095)
    {
      return MAP_FAILED;
    }
  return (void*)status;
}
