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
      *reloc_addr = match.file->load_base + match.symbol->st_value;
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
    }
  else if (type == R_X86_64_DTPMOD64)
    {
    }
  else
    {
      int loop = 1;
      while (loop) {}
      VDL_LOG_ASSERT (0, "unhandled reloc type");
    }
  return *reloc_addr;
}
extern void machine_resolve_trampoline (struct VdlFile *file, unsigned long offset);
void
machine_lazy_setup (struct VdlFile *file)
{
  VDL_LOG_FUNCTION ("file=%s", file->name);
  // setup lazy binding by setting the GOT entries 2 and 3
  // as specified by the ELF x86_64 ABI. It's the same
  // as the i386 ABI here.
  // Entry 2 is set to a pointer to the associated VdlFile
  // Entry 3 is set to the asm trampoline machine_resolve_trampoline
  unsigned long *got = (unsigned long *) vdl_file_get_dynamic_p (file, DT_PLTGOT);
  if (got == 0)
    {
      return;
    }
  got[1] = (unsigned long)file;
  got[2] = (unsigned long) machine_resolve_trampoline;
}

void machine_insert_trampoline (unsigned long from, unsigned long to)
{
  VDL_LOG_FUNCTION ("from=0x%x, to=0x%x", from, to);
}

void machine_tcb_allocate_and_set (unsigned long tcb_size)
{
  unsigned long total_size = tcb_size + CONFIG_TCB_SIZE;
  unsigned long buffer = (unsigned long) vdl_utils_malloc (total_size);
  vdl_utils_memset ((void*)buffer, 0, total_size);
  unsigned long tcb = buffer + tcb_size;
  vdl_utils_memcpy ((void*)(tcb+CONFIG_TCB_TCB_OFFSET), &tcb, sizeof (tcb));
  vdl_utils_memcpy ((void*)(tcb+CONFIG_TCB_SELF_OFFSET), &tcb, sizeof (tcb));

  unsigned long fs = tcb;
  int status = SYSCALL2 (arch_prctl, ARCH_SET_FS, fs);
  VDL_LOG_DEBUG ("status=%d\n", status);
  VDL_LOG_ASSERT (status == 0, "Unable to set TCB");
}
void machine_tcb_set_dtv (unsigned long *dtv)
{
  unsigned long tcb = machine_tcb_get ();
  vdl_utils_memcpy ((void*)(tcb+CONFIG_TCB_DTV_OFFSET), &dtv, sizeof (dtv));
}
void machine_tcb_set_sysinfo (unsigned long sysinfo)
{
  unsigned long tcb = machine_tcb_get ();
  vdl_utils_memcpy ((void*)(tcb+CONFIG_TCB_SYSINFO_OFFSET), &sysinfo, sizeof (sysinfo));
}
unsigned long machine_tcb_get (void)
{
  unsigned long value = 0;
  asm ("mov %%fs:0,%0" : "=r" (value) :);
  return value;
}
unsigned long *machine_tcb_get_dtv (void)
{
  unsigned long tcb = machine_tcb_get ();
  unsigned long dtv;
  vdl_utils_memcpy (&dtv, (void*)(tcb+CONFIG_TCB_DTV_OFFSET), sizeof (dtv));
  return (unsigned long *)dtv;
}
unsigned long machine_tcb_get_sysinfo (void)
{
  unsigned long tcb = machine_tcb_get ();
  unsigned long sysinfo;
  vdl_utils_memcpy (&sysinfo, (void*)(tcb+CONFIG_TCB_SYSINFO_OFFSET), sizeof (sysinfo));
  return sysinfo;
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
void *machine_system_mmap(void *start, size_t length, int prot, int flags, int fd, off_t offset)
{
  long int status = SYSCALL6(mmap, start, length, prot, flags, fd, offset);
  if (status < 0 && status > -4095)
    {
      return MAP_FAILED;
    }
  return (void*)status;
}
