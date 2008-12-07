#include "syscall.h"
#include "dprintf.h"
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

extern int g_test;

static uint8_t *
get_interpreter_load_base (unsigned long *args)
{
  uint8_t *interpreter_load_base;
  unsigned long tmp;
  int argc;
  ElfW(auxv_t) *auxvt, *auxvt_tmp;
  tmp = (unsigned long)(args-1);
  argc = READ_INT (tmp); // skip argc
  tmp += sizeof(char *)*(argc+1); // skip argv
  while (READ_POINTER (tmp) != 0) {} // skip envp
  auxvt = (ElfW(auxv_t) *)tmp; // save aux vector start
  // search interpreter load base
  interpreter_load_base = 0;
  auxvt_tmp = auxvt;
  while (auxvt_tmp->a_type != AT_NULL)
    {
      if (auxvt_tmp->a_type == AT_BASE)
	  {
	    interpreter_load_base = (uint8_t *)auxvt_tmp->a_un.a_val;
	    break;
	  }
      auxvt_tmp++;
    }
  DPRINTF ("interpreter load base: 0x%x\n", interpreter_load_base);
  if (interpreter_load_base == 0)
    {
      SYSCALL1 (exit, -3);
    }
  return interpreter_load_base;
}

static ElfW(Dyn) *
get_pt_dynamic (uint8_t *load_base)
{
  ElfW(Half) i;
  ElfW(Dyn) *dynamic = 0;
  ElfW(Ehdr) *header = (ElfW(Ehdr) *)load_base;
  DPRINTF ("program headers n=%d, off=0x%x\n", 
	   header->e_phnum, 
	   header->e_phoff);
  for (i = 0; i < header->e_phnum; i++)
    {
      ElfW(Phdr) *program_header = (ElfW(Phdr) *) 
	(load_base + 
	 header->e_phoff + 
	 header->e_phentsize * i);
      //DPRINTF ("type=0x%x\n", program_header->p_type)
      if (program_header->p_type == PT_DYNAMIC)
	{
	  //DPRINTF ("off=0x%x\n", program_header->p_offset);
	  dynamic = (ElfW(Dyn)*)(load_base + program_header->p_offset);
	  break;
	}
    }
  if (dynamic == 0)
    {
      SYSCALL1 (exit, -4);
    }
  // we found PT_DYNAMIC
  DPRINTF("dynamic=0x%x\n", dynamic);
  return dynamic;
}

  // relocate entries in DT_REL
static void
relocate_dt_rel (ElfW(Dyn) *dynamic, uint8_t *load_base)
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
      // relocate entries in dt_rel
      uint32_t i;
      for (i = 0; i < dt_relsz; i+=dt_relent)
	{
	  ElfW(Rel) *tmp = (ElfW(Rel)*)(((uint8_t*)dt_rel) + i);
	  ElfW(Addr) *reloc_addr = (void *)(load_base + tmp->r_offset);
	  *reloc_addr += (ElfW(Addr))load_base;
	}
    }
}

void _dl_start(unsigned long args)
{
  uint8_t *interpreter_load_base;
  ElfW(Dyn) *dynamic;

  interpreter_load_base = get_interpreter_load_base (&args);
  dynamic = get_pt_dynamic (interpreter_load_base);
  relocate_dt_rel (dynamic, interpreter_load_base);

  SYSCALL1 (exit, -6);
}
