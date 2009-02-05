#define _GNU_SOURCE
#include "glibc.h"
#include "machine.h"
#include "vdl.h"
#include "vdl-utils.h"
#include "vdl-log.h"
#include "config.h"
#include <elf.h>
#include <dlfcn.h>
#include <link.h>


#define EXPORT __attribute__ ((visibility("default")))
#define WEAK __attribute__ ((weak))

// Set to zero until just before main is invoked
// at which point it must be set to 1. Specifically,
// it is zero during the .init function execution.
// On my system, surprisingly, the dynamic loader
// does not export this symbol so, one could wonder
// why we bother with it.
// XXX: check on a couple more systems if we can't
// get rid of it.
EXPORT int _dl_starting_up = 0 ;
// Set to the end of the main stack (the stack allocated
// by the kernel). Must be constant. Is used by libpthread 
// _and_ the ELF loader to make the main stack executable
// when an object which is loaded needs it. i.e., when
// an object is loaded which has a program header of type GNU_STACK
// and the protection of that program header is RWX, we need to
// make sure that all stacks of all threads are executable.
// The stack of the main thread is made executable by incrementally
// changing the protection of every page starting at __libc_stack_end
// When we attempt to change the protection of the bottom
// of the stack, we will fail because there is always a hole below
// the kernel-allocated main stack.
//
// Note: I don't know of any application which actually needs
// an executable stack which is why we don't do much with it.
//
// Implementation note: if you assume that the stack grows downward,
// the easiest way to initialize this variable is to set it to
// __builtin_frame_address(0) from the top-level dynamic loader
// entry point.
//
EXPORT void *__libc_stack_end = 0;
// If set to 1, indicates that we are running with super privileges.
// If so, the ELF loader won't use LD_LIBRARY_PATH, and the libc will
// enable a couple of extra security checks.
// By default, we don't set secure mode, contrary to the libc.
// Theoretically, this variable would be set based on a couple of
// checks on eid/egid, etc.
EXPORT int __libc_enable_secure = 0;
// Obviously, points to the program argv. I can't figure out why
// and how this symbol is imported by libc.so
EXPORT char **_dl_argv;

EXPORT char _rtld_global_ro[CONFIG_RTLD_GLOBAL_RO_SIZE];
// We have to define first a local symbol to ensure that all references
// to this symbol do not go through the GOT.
static char _rtld_local[CONFIG_RTLD_GLOBAL_SIZE];
// and, then, we define the exported symbol as an alias to the local symbol.
extern __typeof (_rtld_local) _rtld_global __attribute__ ((alias("_rtld_local"), 
							   visibility("default")));


EXPORT WEAK void *calloc(size_t nmemb, size_t size)
{
  VDL_LOG_ASSERT (0, "calloc called");
  return 0;
}
EXPORT WEAK void *malloc(size_t size)
{
  VDL_LOG_ASSERT (0, "malloc called");
  return 0;
}
EXPORT WEAK void free(void *ptr)
{
  VDL_LOG_ASSERT (0, "free called");
}
EXPORT WEAK void *realloc(void *ptr, size_t size)
{
  VDL_LOG_ASSERT (0, "realloc called");
  return 0;
}
//_r_debug;
//__libc_memalign;


static void **vdl_dl_error_catch_tsd (void)
{
  static void *data;
  return &data;
}

static int vdl_dl_addr (const void *address, Dl_info *info,
			struct link_map **mapp, const ElfW(Sym) **symbolp)
{
  VDL_LOG_FUNCTION ("address=%p, info=%p, mapp=%p, symbolp=%p", address, info, mapp, symbolp);
  return 0;
}

// It's hard to believe but _dl_get_tls_static_info did get this 
// treatment in the libc so, we have to do the same if we want to
// make sure that the pthread library can call us.
# ifdef __i386__
#  define internal_function   __attribute ((regparm (3), stdcall))
# else
#  define internal_function
# endif

// used by __pthread_initialize_minimal_internal from nptl/init.c
void
internal_function
 EXPORT
_dl_get_tls_static_info (size_t *sizep, size_t *alignp)
{
  *sizep = g_vdl.tls_static_size;
  *alignp = g_vdl.tls_static_align;
}


void glibc_startup_finished (void) 
{
  _dl_starting_up = 1;
}


void glibc_initialize (void)
{
  void **(*fn) (void) = vdl_dl_error_catch_tsd;
  char *dst = &_rtld_local[CONFIG_DL_ERROR_CATCH_TSD_OFFSET];
  vdl_utils_memcpy ((void*)dst, &fn, sizeof (fn));
}



void glibc_patch (struct VdlFile *file)
{
  VDL_LOG_FUNCTION ("file=%s", file->name);
  if (file->patched)
    {
      // if we are patched already, no need to do any work
      return;
    }
  // mark the file as patched
  file->patched = 1;

  // iterate over all deps first before initialization.
  struct VdlFileList *cur;
  for (cur = file->deps; cur != 0; cur = cur->next)
    {
      glibc_patch (cur->item);
    }

  unsigned long addr = vdl_file_symbol_lookup_local (file, "_dl_addr");
  if (addr != 0)
    {
      machine_insert_trampoline (addr, (unsigned long) &vdl_dl_addr);
    }
}
