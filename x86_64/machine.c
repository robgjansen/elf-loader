#include "machine.h"
#include "vdl.h"
#include "vdl-utils.h"
#include "vdl-log.h"
#include "vdl-file-reloc.h"
#include "vdl-file-symbol.h"
#include "config.h"
#include "syscall.h"
#include <sys/mman.h>
#include <sys/mman.h>
#include <asm/prctl.h> // for ARCH_SET_FS

static int do_lookup_and_log (struct VdlFile *file,
			      const char *symbol_name,
			      const ElfW(Vernaux) *ver,
			      enum LookupFlag flags,
			      struct SymbolMatch *match)
{
  if (!vdl_file_symbol_lookup (file, symbol_name, ver, flags, match))
    {
      VDL_LOG_SYMBOL_FAIL (symbol_name, file);
      // if the symbol resolution has failed, it could
      // be that it's not a big deal.
      return 0;
    }
  VDL_LOG_SYMBOL_OK (symbol_name, file, match);
  return 1;
}
unsigned long
machine_reloc_rel (struct VdlFile *file,
		   const ElfW(Rel) *rel,
		   const ElfW(Sym) *sym,
		   const ElfW(Vernaux) *ver,
		   const char *symbol_name)
{
  VDL_LOG_ASSERT (0, "x86_64 does not use rel entries");
  return 0;
}
unsigned long
machine_reloc_rela (struct VdlFile *file,
		    const ElfW(Rela) *rela,
		    const ElfW(Sym) *sym,
		    const ElfW(Vernaux) *ver,
		    const char *symbol_name)
{
  VDL_LOG_FUNCTION ("file=%s, symbol_name=%s, off=0x%x, type=0x%x", 
		    file->filename, (symbol_name != 0)?symbol_name:"", 
		    rela->r_offset,
		    ELFW_R_TYPE (rela->r_info));
  unsigned long type = ELFW_R_TYPE (rela->r_info);
  unsigned long *reloc_addr = (unsigned long*) (file->load_base + rela->r_offset);

  if (type == R_X86_64_GLOB_DAT ||
      type == R_X86_64_JUMP_SLOT)
    {
      struct SymbolMatch match;
      if (!do_lookup_and_log (file, symbol_name, ver, 0, &match))
	{
	  return 0;
	}
      *reloc_addr = match.file->load_base + match.symbol->st_value + rela->r_addend;
    }
  else if (type == R_X86_64_RELATIVE)
    {
      *reloc_addr = file->load_base + rela->r_addend;
    }
  else if (type == R_X86_64_64)
    {
      struct SymbolMatch match;
      if (!do_lookup_and_log (file, symbol_name, ver, 0, &match))
	{
	  return 0;
	}
      *reloc_addr = match.file->load_base + match.symbol->st_value + rela->r_addend;
    }
  else if (type == R_X86_64_COPY)
    {
      struct SymbolMatch match;
      // for R_*_COPY relocations, we must use the
      // LOOKUP_NO_EXEC flag to avoid looking up the symbol
      // in the main binary.
      if (!do_lookup_and_log (file, symbol_name, ver, LOOKUP_NO_EXEC, &match))
	{
	  return 0;
	}
      VDL_LOG_ASSERT (match.symbol->st_size == sym->st_size,
		      "Symbols don't have the same size: likely a recipe for disaster.");
      vdl_utils_memcpy (reloc_addr, 
			(void*)(match.file->load_base + match.symbol->st_value),
			match.symbol->st_size);
    }
  else if (type == R_X86_64_TPOFF64)
    {
      unsigned long v;
      if (symbol_name != 0)
	{
	  struct SymbolMatch match;
	  if (!do_lookup_and_log (file, symbol_name, ver, 0, &match))
	    {
	      return 0;
	    }
	  VDL_LOG_ASSERT (match.file->has_tls,
		      "Module which contains target symbol does not have a TLS block ??");
	  VDL_LOG_ASSERT (ELFW_ST_TYPE (match.symbol->st_info) == STT_TLS,
		      "Target symbol is not a tls symbol ??");
	  v = match.file->tls_offset + match.symbol->st_value;
	}
      else
	{
	  v = file->tls_offset + sym->st_value;
	}
      *reloc_addr += v + rela->r_addend;
    }
  else if (type == R_X86_64_DTPMOD64)
    {
      unsigned long v;
      if (symbol_name != 0)
	{
	  struct SymbolMatch match;
	  if (!do_lookup_and_log (file, symbol_name, ver, 0, &match))
	    {
	      return 0;
	    }
	  VDL_LOG_ASSERT (match.file->has_tls,
		      "Module which contains target symbol does not have a TLS block ??");
	  v = match.file->tls_index;
	}
      else
	{
	  v = file->tls_index;
	}
      *reloc_addr = v;
    }
  else if (type == R_X86_64_DTPOFF64)
    {
      unsigned long v;
      if (symbol_name != 0)
	{
	  struct SymbolMatch match;
	  if (!do_lookup_and_log (file, symbol_name, ver, 0, &match))
	    {
	      return 0;
	    }
	  VDL_LOG_ASSERT (match.file->has_tls,
		      "Module which contains target symbol does not have a TLS block ??");
	  v = match.symbol->st_value;
	}
      else
	{
	  v = sym->st_value;
	}
      *reloc_addr = v + rela->r_addend;
    }
  else
    {
      VDL_LOG_ASSERT (0, "unhandled reloc type");
    }
  return *reloc_addr;
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
  // if this platform does prelinking, the prelinker has stored
  // a pointer to plt + 0x16 in got[1]. Otherwise, got[1] is zero
  // no, there is no documentation about this other than the code
  // of the compile-time linker(actually, bfd), dynamic loader and 
  // the prelinker
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

uint32_t machine_cmpxchg (uint32_t *ptr, uint32_t old, uint32_t new)
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
