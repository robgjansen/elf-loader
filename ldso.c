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

struct OsArgs
{
  uint8_t *interpreter_load_base;
  int (*program_entry) (int, char *[]);
  int program_argc;
  char **program_argv;
  char **program_envp;
  int program_fd;
  ElfW(Phdr) *program_phdr;
  uint32_t program_phent;
  uint32_t program_phnum;
};

static struct OsArgs
get_os_args (unsigned long *args)
{
  struct OsArgs os_args;
  unsigned long tmp;

  ElfW(auxv_t) *auxvt, *auxvt_tmp;
  tmp = (unsigned long)(args-1);
  os_args.program_argc = READ_INT (tmp); // skip argc
  os_args.program_argv = (char **)tmp;
  tmp += sizeof(char *)*(os_args.program_argc+1); // skip argv
  os_args.program_envp = (char **)tmp;
  while (READ_POINTER (tmp) != 0) {} // skip envp
  auxvt = (ElfW(auxv_t) *)tmp; // save aux vector start
  // search interpreter load base
  os_args.interpreter_load_base = 0;
  auxvt_tmp = auxvt;
  while (auxvt_tmp->a_type != AT_NULL)
    {
      if (auxvt_tmp->a_type == AT_BASE)
	{
	  os_args.interpreter_load_base = (uint8_t *)auxvt_tmp->a_un.a_val;
	  break;
	}
      else if (auxvt_tmp->a_type == AT_EXECFD)
	{
	  os_args.program_fd = auxvt_tmp->a_un.a_val;
	  break;
	}
      else if (auxvt_tmp->a_type == AT_PHDR)
	{
	  os_args.program_phdr = (ElfW(Phdr) *)auxvt_tmp->a_un.a_val;
	}
      else if (auxvt_tmp->a_type == AT_PHENT)
	{
	  os_args.program_phent = auxvt_tmp->a_un.a_val;
	}
      else if (auxvt_tmp->a_type == AT_PHNUM)
	{
	  os_args.program_phnum = auxvt_tmp->a_un.a_val;
	}
      else if (auxvt_tmp->a_type == AT_ENTRY)
	{
	  os_args.program_entry = (int (*)(int,char**))auxvt_tmp->a_un.a_val;
	}
      auxvt_tmp++;
    }
  DPRINTF ("interpreter load base: 0x%x\n", os_args.interpreter_load_base);
  if (os_args.interpreter_load_base == 0)
    {
      SYSCALL1 (exit, -3);
    }
  return os_args;
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
  struct OsArgs os_args;
  ElfW(Dyn) *dynamic;

  os_args = get_os_args (&args);
  dynamic = get_pt_dynamic (os_args.interpreter_load_base);
  relocate_dt_rel (dynamic, os_args.interpreter_load_base);

  SYSCALL1 (exit, -6);
}
