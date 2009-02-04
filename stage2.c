#include "system.h"
#include "vdl.h"
#include "vdl-elf.h"
#include "glibc.h"
#include "gdb.h"
#include "machine.h"
#include "stage2.h"
#include <elf.h>
#include <link.h>


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
  if (!vdl_elf_file_get_info (header->e_phnum, phdr, &info))
    {
      MDL_LOG_ERROR ("Could not obtain file info for interpreter\n", 1);
      goto error;
    }
  struct MappedFile *file = vdl_file_new (load_base, &info, 
					  LDSO_SONAME,
					  LDSO_SONAME,
					  context);
  // the interpreter has already been reloced during stage1, so, 
  // we must be careful to not relocate it twice.
  file->reloced = 1;

  if (!vdl_elf_map_deps (file))
    {
      goto error;
    }

  return file;
 error:
  return 0;
}

static struct MappedFileList *
do_ld_preload (struct Context *context, struct MappedFileList *scope, const char **envp)
{
  // add the LD_PRELOAD binary if it is specified somewhere.
  // We must do this _before_ adding the dependencies of the main 
  // binary to the link map to ensure that the symbol scope of 
  // the main binary is correct, that is, that symbols are 
  // resolved first within the LD_PRELOAD binary, before every
  // other library, but after the main binary itself.
  const char *ld_preload = vdl_getenv (envp, "LD_PRELOAD");
  if (ld_preload != 0)
    {
      // search the requested program
      char *ld_preload_filename = vdl_elf_search_file (ld_preload);
      if (ld_preload_filename == 0)
	{
	  MDL_LOG_ERROR ("Could not find %s\n", ld_preload);
	  goto error;
	}
      // map it in memory.
      struct MappedFile *ld_preload_file = vdl_elf_map_single (context, ld_preload_filename, 
							       ld_preload);
      if (ld_preload_file == 0)
	{
	  MDL_LOG_ERROR ("Unable to load LD_PRELOAD: %s\n", ld_preload_filename);
	  goto error;
	}
      // add it to the global scope
      scope = vdl_file_list_append_one (scope, ld_preload_file);
    }
 error:
  return scope;
}

static void
setup_env_vars (const char **envp)
{
  // populate search_dirs from LD_LIBRARY_PATH
  const char *ld_lib_path = vdl_getenv (envp, "LD_LIBRARY_PATH");
  struct StringList *list = vdl_strsplit (ld_lib_path, ':');
  g_vdl.search_dirs = vdl_str_list_append (list, g_vdl.search_dirs);

  // setup logging from LD_LOG
  const char *ld_log = vdl_getenv (envp, "LD_LOG");
  vdl_set_logging (ld_log);

  // setup bind_now from LD_BIND_NOW
  const char *bind_now = vdl_getenv (envp, "LD_BIND_NOW");
  if (bind_now != 0)
    {
      g_vdl.bind_now = 1;
    }
}

static int
is_loader (unsigned long phnum, ElfW(Phdr)*phdr)
{
  // the file is already mapped in memory so, we reverse-engineer its setup
  struct FileInfo info;
  MDL_ASSERT (vdl_elf_file_get_info (phnum,phdr, &info),
	      "Unable to obtain information about main program");

  // we search the first PT_LOAD 
  MDL_ASSERT (phdr->p_type == PT_PHDR,
	      "The first program header is not a PT_PHDR");
  // If we assume that the first program in the program header table is the PT_PHDR
  // The load base of the main program is easy to calculate as the difference
  // between the PT_PHDR vaddr and its real address in memory.
  unsigned long load_base = ((unsigned long)phdr) - phdr->p_vaddr;
  // Now, go to dynamic section and look at its DT_SONAME entry
  ElfW(Dyn) *cur = (ElfW(Dyn) *) (load_base + info.dynamic);
  unsigned long dt_strtab = 0;
  unsigned long dt_soname = 0;
  while (cur->d_tag != DT_NULL)
    {
      if (cur->d_tag == DT_SONAME)
	{
	  dt_soname = cur->d_un.d_val;
	}
      else if (cur->d_tag == DT_STRTAB)
	{
	  dt_strtab = cur->d_un.d_val + load_base;
	}
      cur++;
    }
  if (dt_soname == 0)
    {
      return 0;
    }
  MDL_ASSERT (dt_strtab != 0, "Could not find dt_strtab");
  char *soname = (char *)(dt_strtab + dt_soname);
  return vdl_strisequal (soname, LDSO_SONAME);
}

struct Stage2Output
stage2 (struct Stage2Input input)
{
  struct Stage2Output output;
  vdl_initialize (input.interpreter_load_base);

  setup_env_vars (input.program_envp);

  struct MappedFile *main_file;
  struct Context *context;
  if (is_loader (input.program_phnum, input.program_phdr))
    {
      // the interpreter is run as a normal program. We behave like the libc
      // interpreter and assume that this means that the name of the program
      // to run is the first argument in the argv.
      MDL_ASSERT (input.program_argc >= 2, "Not enough arguments to run loader");

      const char *program = input.program_argv[1];
      // We need to do what the kernel usually does for us, that is,
      // search the file, and map it in memory
      char *filename = vdl_elf_search_file (program);
      MDL_ASSERT (filename != 0, "Could not find main binary");
      context = vdl_context_new (input.program_argc - 1,
				 input.program_argv + 1,
				 input.program_envp);

      // the filename for the main exec is "" for gdb.
      main_file = vdl_elf_map_single (context, program, "");
      output.n_argv_skipped = 1;
    }
  else
    {
      // here, the file is already mapped so, we just create the 
      // right data structure
      struct FileInfo info;
      MDL_ASSERT (vdl_elf_file_get_info (input.program_phnum, input.program_phdr, &info),
		  "Unable to obtain information about main program");

      // The load base of the main program is easy to calculate as the difference
      // between the PT_PHDR vaddr and its real address in memory.
      unsigned long load_base = ((unsigned long)input.program_phdr) - input.program_phdr->p_vaddr;

      context = vdl_context_new (input.program_argc,
				 input.program_argv,
				 input.program_envp);

      // the filename for the main exec is "" for gdb.
      main_file = vdl_file_new (load_base,
				&info,
				"",
				input.program_argv[0],
				context);
      output.n_argv_skipped = 0;
    }
  main_file->is_executable = 1;
  gdb_initialize (main_file);

  // add the interpreter itself to the link map to ensure that it is
  // recorded somewhere. We must add it to the link map only after
  // the main binary because gdb assumes that the first entry in the
  // link map is the main binary itself. We don't add it to the global 
  // scope.
  struct MappedFile *interpreter = interpreter_new (input.interpreter_load_base,
						    context);

  struct MappedFileList *global_scope = 0;

  // we add the main binary to the global scope
  global_scope = vdl_file_list_append_one (0, main_file);

  global_scope = do_ld_preload (context, global_scope, input.program_envp);

  MDL_ASSERT (vdl_elf_map_deps (main_file), 
	      "Unable to map dependencies of main file");

  // The global scope is defined as being made of the main binary
  // and all its dependencies, breadth-first, with duplicate items removed.
  struct MappedFileList *all_deps = vdl_elf_gather_all_deps_breadth_first (main_file);
  global_scope = vdl_file_list_append (global_scope, all_deps);
  vdl_file_list_unicize (global_scope);
  context->global_scope = global_scope;

  // We gather tls information for each module. We need to
  // do this before relocation because the TLS-type relocations 
  // need this tls information.
  {
    struct MappedFile *cur;
    for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
      {
	vdl_elf_tls (cur);
      }
  }
  // Then, we calculate the size of the memory needed for the 
  // static and local tls model. We also initialize correctly
  // the tls_offset field to be able to perform relocations
  // next (the TLS relocations need the tls_offset field).
  {
    unsigned long tcb_size = 0;
    unsigned long n_dtv = 0;
    unsigned long max_align = 0;
    struct MappedFile *cur;
    for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
      {
	if (cur->has_tls)
	  {
	    tcb_size += cur->tls_tmpl_size + cur->tls_init_zero_size;
	    tcb_size = vdl_align_up (tcb_size, cur->tls_align);
	    n_dtv++;
	    cur->tls_offset = - tcb_size;
	    if (cur->tls_align > max_align)
	      {
		max_align = cur->tls_align;
	      }
	  }
      }
    g_vdl.tls_static_size = tcb_size;
    g_vdl.tls_static_align = max_align;
    g_vdl.tls_n_dtv = n_dtv;
  }

  // We either setup the GOT for lazy symbol resolution
  // or we perform binding for all symbols now if LD_BIND_NOW is set
  g_vdl.bind_now = 1;
  {
    struct MappedFile *cur;
    for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
      {
	vdl_elf_reloc (cur);
      }
  }

  // Once relocations are done, we can initialize the tls blocks
  // and the dtv. We need to wait post-reloc because the tls
  // template area used to initialize the tls blocks is likely 
  // to be modified during relocation processing.
  {
    machine_tcb_allocate_and_set (g_vdl.tls_static_size);
    unsigned long tcb = machine_tcb_get ();
    machine_tcb_set_sysinfo (input.sysinfo);
    unsigned long *dtv = vdl_malloc ((1+g_vdl.tls_n_dtv) * sizeof (unsigned long));
    dtv[0] = g_vdl.tls_gen;
    g_vdl.tls_gen++;
    struct MappedFile *cur;
    unsigned long i; // starts at 1 because 0 contains the generation
    for (i = 1, cur = g_vdl.link_map; cur != 0; cur = cur->next)
      {
	if (cur->has_tls)
	  {
	    // setup the dtv to point to the tls block
	    dtv[i] = tcb + cur->tls_offset;
	    // copy the template in the module tls block
	    vdl_memcpy ((void*)dtv[i], (void*)cur->tls_tmpl_start, cur->tls_tmpl_size);
	    vdl_memset ((void*)(dtv[i] + cur->tls_tmpl_size), 0, cur->tls_init_zero_size);
	    i++;
	  }
      }
    machine_tcb_set_dtv (dtv);
  }

  // Note that we must invoke this method to notify gdb that we have
  // a valid linkmap only _after_ relocations have been done (if you do
  // it before, gdb gets confused) and _before_ the initializers are 
  // run (to allow the user to debug the initializers).
  gdb_notify ();

  // patch glibc functions which need to be overriden.
  // This is really a hack I am not very proud of.
  {
    struct MappedFile *cur;
    for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
      {
	glibc_patch (cur);
      }
  }

  // glibc-specific crap to avoid segfault in initializer
  glibc_initialize ();

  // Finally, call init functions
  {
    struct MappedFile *cur;
    for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
      {
	vdl_elf_call_init (cur);
      }
  }

  unsigned long entry = vdl_elf_get_entry_point (main_file);
  if (entry == 0)
    {
      MDL_LOG_ERROR ("Zero entry point: nothing to do in %s\n", main_file->name);
      goto error;
    }
  glibc_startup_finished ();

  output.entry_point = entry;
  return output;
error:
  system_exit (-6);
  return output; // quiet compiler
}

