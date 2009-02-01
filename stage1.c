#include "dprintf.h"
#include "stage2.h"
#include "system.h"
#include <elf.h>
#include <link.h>


#define READ_INT(p)				\
  ({int v = *((int*)p);				\
    p+=sizeof(int);				\
    v;})

#define READ_POINTER(p)				\
  ({char * v = *((char**)p);			\
    p+=sizeof(char*);				\
    v;})
static struct OsArgs
os_args_read (unsigned long entry_point_struct)
{
  struct OsArgs os_args;
  unsigned long tmp = entry_point_struct;
  ElfW(auxv_t) *auxvt, *auxvt_tmp;
  os_args.program_argc = READ_INT (tmp); // skip argc
  DPRINTF("argc=0x%x\n", os_args.program_argc);
  os_args.program_argv = (const char **)tmp;
  tmp += sizeof(char *)*(os_args.program_argc+1); // skip argv
  os_args.program_envp = (const char **)tmp;
  while (READ_POINTER (tmp) != 0) {} // skip envp
  auxvt = (ElfW(auxv_t) *)tmp; // save aux vector start
  // search interpreter load base
  os_args.interpreter_load_base = 0;
  os_args.program_phdr = 0;
  os_args.program_phnum = 0;
  auxvt_tmp = auxvt;
  while (auxvt_tmp->a_type != AT_NULL)
    {
      if (auxvt_tmp->a_type == AT_BASE)
	{
	  os_args.interpreter_load_base = auxvt_tmp->a_un.a_val;
	}
      else if (auxvt_tmp->a_type == AT_PHDR)
	{
	  os_args.program_phdr = (ElfW(Phdr) *)auxvt_tmp->a_un.a_val;
	}
      else if (auxvt_tmp->a_type == AT_PHNUM)
	{
	  os_args.program_phnum = auxvt_tmp->a_un.a_val;
	}
      else if (auxvt_tmp->a_type == AT_SYSINFO)
	{
	  os_args.sysinfo = auxvt_tmp->a_un.a_val;
	}
      auxvt_tmp++;
    }
  DPRINTF ("interpreter load base: 0x%x\n", os_args.interpreter_load_base);
  if (os_args.interpreter_load_base == 0 ||
      os_args.program_phdr == 0 ||
      os_args.program_phnum == 0 ||
      os_args.sysinfo == 0)
    {
      system_exit (-3);
    }
  return os_args;
}

// relocate entries in DT_REL
static void
relocate_dt_rel (ElfW(Dyn) *dynamic, unsigned long load_base)
{
  ElfW(Dyn) *tmp = dynamic;
  ElfW(Rel) *dt_rel = 0;
  uint32_t dt_relsz = 0;
  uint32_t dt_relent = 0;
  // search DT_REL, DT_RELSZ, DT_RELENT
  while (tmp->d_tag != DT_NULL && (dt_rel == 0 || dt_relsz == 0 || dt_relent == 0))
    {
      //DEBUG_HEX(tmp->d_tag);
      if (tmp->d_tag == DT_REL)
	{
	  dt_rel = (ElfW(Rel) *)(load_base + tmp->d_un.d_ptr);
	}
      else if (tmp->d_tag == DT_RELSZ)
	{
	  dt_relsz = tmp->d_un.d_val;
	}
      else if (tmp->d_tag == DT_RELENT)
	{
	  dt_relent = tmp->d_un.d_val;
	}
      tmp++;
    }
  DPRINTF ("dtrel=0x%x, dt_relsz=%d, dt_relent=%d\n", 
	   dt_rel, dt_relsz, dt_relent);
  if (dt_rel != 0 && dt_relsz != 0 && dt_relent != 0)
    {
      // relocate entries in dt_rel. We could check the type below
      // but since we are relocating the dynamic loader itself
      // here, the entries will always be of type R_386_RELATIVE.
      uint32_t i;
      for (i = 0; i < dt_relsz; i+=dt_relent)
	{
	  ElfW(Rel) *tmp = (ElfW(Rel)*)(((uint8_t*)dt_rel) + i);
	  ElfW(Addr) *reloc_addr = (void *)(load_base + tmp->r_offset);
	  *reloc_addr += (ElfW(Addr))load_base;
	}
    }
}


// Called from _start entry point asm code.
struct TrampolineInformation * 
stage1 (struct TrampolineInformation*trampoline_information)
{
  struct OsArgs os_args = os_args_read (trampoline_information->entry_point_struct);
  // The linker defines the symbol _DYNAMIC to point to the start of the 
  // PT_DYNAMIC area which has been mapped by the OS loader as part of the
  // rw PT_LOAD segment.
  void *dynamic = _DYNAMIC;
  dynamic += os_args.interpreter_load_base;
  relocate_dt_rel (dynamic, os_args.interpreter_load_base);
  struct Stage2Result result = stage2 (trampoline_information, os_args);
  trampoline_information->entry_point = result.entry_point;
  trampoline_information->dl_fini = 0;
  // adjust the entry point structure to get rid of skipped 
  // command-line arguments
  int *pargc = ((int*)trampoline_information->entry_point_struct);
  *pargc -= result.n_argv_skipped;
  int *new_pargc = (int*)(trampoline_information->entry_point_struct+
			  result.n_argv_skipped*sizeof(char*));
  *new_pargc = *pargc;
  trampoline_information->entry_point_struct = (unsigned long)new_pargc;
  return trampoline_information;
}
