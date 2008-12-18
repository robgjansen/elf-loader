#include "syscall.h"
#include "dprintf.h"
#include "mdl.h"
#include "mdl-elf.h"
#include "glibc.h"
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

struct OsArgs
{
  unsigned long interpreter_load_base;
  ElfW(Phdr) *program_phdr;
  uint32_t program_phnum;
  void *program_entry;
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
  os_args.program_entry = 0;
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
      else if (auxvt_tmp->a_type == AT_ENTRY)
	{
	  os_args.program_entry = (void*)auxvt_tmp->a_un.a_val;
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

static struct MappedFile *
interpreter_new (unsigned long load_base, struct Context *context)
{
  /* We make many assumptions here:
   *   - The loader is an ET_DYN
   *   - The loader has been compile-time linked at base address 0
   *   - The first PT_LOAD map of the interpreter contains the elf header 
   *     and program headers.
   *   
   * Consequently, we can infer that the load_base in fact points to
   * the first PT_LOAD map of the interpreter which means that load_base
   * in fact points to the elf header itself.
   */
  ElfW(Ehdr) *header = (ElfW(Ehdr) *)load_base;
  ElfW(Phdr) *phdr = (ElfW(Phdr) *) (header->e_phoff + load_base);
  struct FileInfo info;
  if (!mdl_elf_file_get_info (header->e_phnum, phdr, &info))
    {
      MDL_LOG_ERROR ("Could not obtain file info for interpreter\n", 1);
      goto error;
    }
  struct MappedFile *file = mdl_file_new (load_base, &info, 
					  (char*)(info.interpreter_name + load_base),
					  (char*)(info.interpreter_name + load_base),
					  context);
  // the interpreter has already been reloced, so, we must be
  // careful to not relocate it twice.
  file->reloced = 1;

  mdl_elf_file_setup_debug (file);

  return file;
 error:
  return 0;
}

void stage1 (unsigned long args);

static void stage2 (struct OsArgs args)
{
  mdl_initialize (args.interpreter_load_base);

  // populate search_dirs from LD_LIBRARY_PATH
  const char *ld_lib_path = mdl_getenv (args.program_envp, "LD_LIBRARY_PATH");
  struct StringList *list = mdl_strsplit (ld_lib_path, ':');
  g_mdl.search_dirs = mdl_str_list_append (g_mdl.search_dirs, list);

  // setup logging from LD_LOG
  const char *ld_log = mdl_getenv (args.program_envp, "LD_LOG");
  mdl_set_logging (ld_log);

  // setup bind_now from LD_BIND_NOW
  const char *bind_now = mdl_getenv (args.program_envp, "LD_BIND_NOW");
  if (bind_now != 0)
    {
      g_mdl.bind_now = 1;
    }

  struct Context *context = mdl_context_new (args.program_argc,
					     args.program_argv,
					     args.program_envp);

  // add the interpreter itself to the link map to ensure that it is
  // recorded somewhere. We don't add it to the global scope.
  interpreter_new (args.interpreter_load_base, context);

  struct MappedFileList *global_scope = 0;

  // add the LD_PRELOAD binary if it is specified somewhere.
  // We must do this _before_ adding the main binary to the link map
  // to ensure that the symbol scope of the main binary is correct,
  // that is, that symbols are resolved first within the 
  // LD_PRELOAD binary, before everything else.
  const char *ld_preload = mdl_getenv (args.program_envp, "LD_PRELOAD");
  if (ld_preload != 0)
    {
      // search the requested program
      char *ld_preload_filename = mdl_elf_search_file (ld_preload);
      if (ld_preload_filename == 0)
	{
	  MDL_LOG_ERROR ("Could not find %s\n", ld_preload_filename);
	  goto error;
	}
      // map it in memory.
      struct MappedFile *ld_preload_file = mdl_elf_map_single (0, ld_preload_filename, 
							       ld_preload);
      if (ld_preload_file == 0)
	{
	  MDL_LOG_ERROR ("Unable to load LD_PRELOAD: %s\n", ld_preload_filename);
	  goto error;
	}
      // add it to the global scope
      global_scope = mdl_file_list_append_one (global_scope, ld_preload_file);
    }
  
  
  struct MappedFile *main_file;
  if (args.program_entry == stage1)
    {
      // the interpreter is run as a normal program. We behave like the libc
      // interpreter and assume that this means that the name of the program
      // to run is the first argument in the argv.
      if (args.program_argc < 2)
	{
	  MDL_LOG_ERROR ("Not enough arguments to run loader, argc=%d\n", args.program_argc);
	  goto error;
	}

      // We need to do what the kernel usually does for us, that is,
      // search the file, and map it in memory
      char *filename = mdl_elf_search_file (args.program_argv[1]);
      if (filename == 0)
	{
	  MDL_LOG_ERROR ("Could not find %s\n", filename);
	  goto error;
	}
      // the filename for the main exec is "" for gdb.
      main_file = mdl_elf_map_single (0, "", args.program_argv[1]);
    }
  else
    {
      // here, the file is already mapped so, we just create the 
      // right data structure
      struct FileInfo info;
      if (!mdl_elf_file_get_info (args.program_phnum,
				  args.program_phdr,
				  &info))
	{
	  MDL_LOG_ERROR ("Unable to obtain information about main program\n", 1);
	  goto error;
	}

      // The load base of the main program is easy to calculate as the difference
      // between the PT_PHDR vaddr and its real address in memory.
      unsigned long load_base = ((unsigned long)args.program_phdr) - args.program_phdr->p_vaddr;

      // the filename for the main exec is "" for gdb.
      main_file = mdl_file_new (load_base,
				&info,
				"",
				args.program_argv[0],
				context);
    }

  if (!mdl_elf_map_deps (main_file))
    {
      MDL_LOG_ERROR ("Unable to map main file and dependencies\n", 1);
      //XXX: mdl_elf_unmap_recursive (main_file);
      goto error;
    }

  // The global scope is defined as being made of the main binary
  // and all its dependencies, breadth-first, with duplicate items removed.
  struct MappedFileList *all_deps = mdl_elf_gather_all_deps_breadth_first (main_file);
  global_scope = mdl_file_list_append (global_scope, all_deps);
  mdl_file_list_unicize (global_scope);

  // all files hold a pointer back to the context so they can find 
  // the global scope when they needed it.
  context->global_scope = global_scope;

  // We either setup the GOT for lazy symbol resolution
  // or we perform binding for all symbols now if LD_BIND_NOW is set
  g_mdl.bind_now = 1;
  {
    struct MappedFile *cur;
    for (cur = g_mdl.link_map; cur != 0; cur = cur->next)
      {
	mdl_elf_reloc (cur);
      }
  }

  // Finally, call init functions
  {
    struct MappedFile *cur;
    for (cur = g_mdl.link_map; cur != 0; cur = cur->next)
      {
	mdl_elf_call_init (cur);
      }
  }

  unsigned long entry = mdl_elf_get_entry_point (main_file);
  if (entry == 0)
    {
      MDL_LOG_ERROR ("Zero entry point: nothing to do in %s\n", main_file->name);
      goto error;
    }
  int (*main_fn) (int, char **) = (int (*)(int,char**)) entry;
  glibc_startup_finished ();
  int retval = main_fn (args.program_argc, (char**)args.program_argv);

  // call fini functions.

  // call exit (retval)

  // And, return the user entry point to allow the _dl_start
  // trampoline to call the executable entry point.

  SYSCALL1 (exit, 0);
error:
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
  dynamic += os_args.interpreter_load_base;
  relocate_dt_rel (dynamic, os_args.interpreter_load_base);

  stage2 (os_args);
}
