#include "syscall.h"
#include "dprintf.h"
#include "mdl.h"
#include "mdl-elf.h"
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
  ElfW(Phdr) *program_phdr;
  uint32_t program_phnum;
  int program_argc;
  const char **program_argv;
  const char **program_envp;
};

static struct OsArgs
get_os_args (unsigned long *args)
{
  struct OsArgs os_args;
  unsigned long tmp;

  ElfW(auxv_t) *auxvt, *auxvt_tmp;
  tmp = (unsigned long)(args-1);
  os_args.program_argc = READ_INT (tmp); // skip argc
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
	  os_args.interpreter_load_base = (uint8_t *)auxvt_tmp->a_un.a_val;
	}
      else if (auxvt_tmp->a_type == AT_PHDR)
	{
	  os_args.program_phdr = (ElfW(Phdr) *)auxvt_tmp->a_un.a_val;
	}
      else if (auxvt_tmp->a_type == AT_PHNUM)
	{
	  os_args.program_phnum = auxvt_tmp->a_un.a_val;
	}
      auxvt_tmp++;
    }
  DPRINTF ("interpreter load base: 0x%x\n", os_args.interpreter_load_base);
  if (os_args.interpreter_load_base == 0 ||
      os_args.program_phdr == 0 ||
      os_args.program_phnum == 0)
    {
      SYSCALL1 (exit, -3);
    }
  return os_args;
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


void stage1 (unsigned long args);

static void stage2 (struct OsArgs args)
{
  mdl_initialize (args.interpreter_load_base);

  // setup logging
  const char *ld_debug = mdl_getenv (args.program_envp, "LD_DEBUG");
  mdl_set_logging (ld_debug);

  // populate search_dirs from LD_LIBRARY_PATH
  const char *ld_lib_path = mdl_getenv (args.program_envp, "LD_LIBRARY_PATH");
  struct StringList *list = mdl_strsplit (ld_lib_path, ':');
  g_mdl.search_dirs = mdl_str_list_append (g_mdl.search_dirs, list);

  struct MappedFile *main_file = mdl_new (struct MappedFile);
  if (mdl_strisequal (args.program_argv[0], "ldso"))
    {
      // the interpreter is run as a normal program. We behave like the libc
      // interpreter and assume that this means that the name of the program
      // to run is the first argument in the argv.
      // To do this, we need to do the work the kernel did, that is, map the
      // program in memory 

      // XXX: ensure that args is initialized correctly.
    }
  else
    {
      // The load base of the main program is easy to calculate as the difference
      // between the PT_PHDR vaddr and its real address in memory.
      main_file->load_base = ((uint8_t*) args.program_phdr) - args.program_phdr->p_vaddr;
      main_file->filename = mdl_strdup (args.program_argv[0]);
      main_file->dynamic = (uint8_t *) mdl_elf_search_phdr (args.program_phdr, 
							   args.program_phnum,
							   PT_DYNAMIC);
      main_file->next = 0;
      main_file->prev = 0;
      main_file->count = 1;
      main_file->context = 0;
      // XXX
    }

  g_mdl.link_map = main_file;

  SYSCALL1 (exit, -6);
}

void stage1 (unsigned long args)
{
  struct OsArgs os_args;

  os_args = get_os_args (&args);
  // The linker defines the symbol _DYNAMIC to point to the start of the 
  // PT_DYNAMIC area which has been mapped by the OS loader as part of the
  // rw PT_LOAD segment.
  void *dynamic = _DYNAMIC;
  dynamic += (unsigned long)os_args.interpreter_load_base;
  relocate_dt_rel (dynamic, os_args.interpreter_load_base);

  stage2 (os_args);
}
