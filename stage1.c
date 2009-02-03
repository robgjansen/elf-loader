#include "dprintf.h"
#include "stage1.h"
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
static struct Stage2Input
prepare_stage2 (unsigned long entry_point_struct)
{
  struct Stage2Input stage2_input;
  unsigned long tmp = entry_point_struct;
  ElfW(auxv_t) *auxvt, *auxvt_tmp;
  stage2_input.program_argc = READ_INT (tmp); // skip argc
  DPRINTF("argc=0x%x\n", stage2_input.program_argc);
  stage2_input.program_argv = (const char **)tmp;
  tmp += sizeof(char *)*(stage2_input.program_argc+1); // skip argv
  stage2_input.program_envp = (const char **)tmp;
  while (READ_POINTER (tmp) != 0) {} // skip envp
  auxvt = (ElfW(auxv_t) *)tmp; // save aux vector start
  stage2_input.program_phdr = 0;
  stage2_input.program_phnum = 0;
  auxvt_tmp = auxvt;
  while (auxvt_tmp->a_type != AT_NULL)
    {
      if (auxvt_tmp->a_type == AT_PHDR)
	{
	  stage2_input.program_phdr = (ElfW(Phdr) *)auxvt_tmp->a_un.a_val;
	}
      else if (auxvt_tmp->a_type == AT_PHNUM)
	{
	  stage2_input.program_phnum = auxvt_tmp->a_un.a_val;
	}
      else if (auxvt_tmp->a_type == AT_SYSINFO)
	{
	  stage2_input.sysinfo = auxvt_tmp->a_un.a_val;
	}
      auxvt_tmp++;
    }
  if (stage2_input.program_phdr == 0 ||
      stage2_input.program_phnum == 0 ||
      stage2_input.sysinfo == 0)
    {
      system_exit (-3);
    }
  return stage2_input;
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
      // but since we are relocating the dynamic loader itself here,
      // the entries will always be of type R_XXX_RELATIVE.
      // i.e., we work under the assumption that they will be. If 
      // they are not, BAD things will happen.
      uint32_t i;
      for (i = 0; i < dt_relsz; i+=dt_relent)
	{
	  ElfW(Rel) *tmp = (ElfW(Rel)*)(((uint8_t*)dt_rel) + i);
	  ElfW(Addr) *reloc_addr = (void *)(load_base + tmp->r_offset);
	  *reloc_addr += (ElfW(Addr))load_base;
	}
    }
}


// Called from stage0 entry point asm code.
void stage1 (struct Stage1InputOutput *input_output)
{
  // The linker defines the symbol _DYNAMIC to give you the offset from
  // the load base to the start of the PT_DYNAMIC area which has been 
  // mapped by the OS loader as part of the rw PT_LOAD segment.
  void *dynamic = _DYNAMIC;
  dynamic += input_output->load_base;
  relocate_dt_rel (dynamic, input_output->load_base);

  // Note that prepare_stage2 could return the AT_BASE value to give
  // us the load_base of the interpreter but this value is unreliable
  // when the loader is used as an executable so, we have to calculate
  // it by hand and the caller is expected to do just that.
  struct Stage2Input stage2_input = prepare_stage2 (input_output->entry_point_struct);
  stage2_input.interpreter_load_base = input_output->load_base;

  // Now that we have relocated this binary, we can access global variables
  // so, we switch to stage2 to complete the loader initialization.
  struct Stage2Output stage2_output = stage2 (stage2_input);

  // We are all done, so we update the caller's data structure to be able
  // jump in the program's entry point.
  input_output->entry_point = stage2_output.entry_point;
  input_output->dl_fini = 0;
  // adjust the entry point structure to get rid of skipped 
  // command-line arguments
  int *pargc = ((int*)input_output->entry_point_struct);
  *pargc -= stage2_output.n_argv_skipped;
  int *new_pargc = (int*)(input_output->entry_point_struct+
			  stage2_output.n_argv_skipped*sizeof(char*));
  *new_pargc = *pargc;
  input_output->entry_point_struct = (unsigned long)new_pargc;
}
