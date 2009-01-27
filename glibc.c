#define _GNU_SOURCE
#include "glibc.h"
#include "machine.h"
#include "mdl.h"
#include "mdl-elf.h"
#include "config.h"
#include <elf.h>
#include <dlfcn.h>
#include <link.h>

#define EXPORT __attribute__ ((visibility("default")))

// Set to zero until just before main is invoked
// at which point it must be set to 1. Specifically,
// it is zero during the .init function execution.
int _dl_starting_up EXPORT = 0 ;
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
void *__libc_stack_end EXPORT = 0;
// If set to 1, indicates that we are running with super privileges.
// If so, the ELF loader won't use LD_LIBRARY_PATH, and the libc will
// enable a couple of extra security checks.
// By default, we don't set secure mode, contrary to the libc.
// Theoretically, this variable would be set based on a couple of
// checks on eid/egid, etc.
int __libc_enable_secure EXPORT = 0;
// Obviously, points to the program argv. I can't figure out why
// and how this symbol is imported by libc.so
char **_dl_argv EXPORT;

char _rtld_global[CONFIG_RTLD_GLOBAL_SIZE] EXPORT;
char _rtld_global_ro[CONFIG_RTLD_GLOBAL_RO_SIZE] EXPORT;

static void **mdl_dl_error_catch_tsd (void)
{
  static void *data;
  return &data;
}

struct dl_open_hook
{
  void *(*dlopen_mode) (const char *name, int mode);
  void *(*dlsym) (void *map, const char *name);
  int (*dlclose) (void *map);
} * _dl_open_hook = 0;
void *mdl_dlopen_mode (const char *name, int mode)
{
  return 0;
}
void *mdl_dlsym (void *map, const char *name)
{
  return 0;
}
int mdl_dlclose (void *map)
{
  return 0;
}
static struct dl_open_hook g_dl_open_hook = {mdl_dlopen_mode, mdl_dlsym, mdl_dlclose};


static int mdl_dl_addr (const void *address, Dl_info *info,
			struct link_map **mapp, const ElfW(Sym) **symbolp)
{
  MDL_LOG_FUNCTION ("address=%p, info=%p, mapp=%p, symbolp=%p", address, info, mapp, symbolp);
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
_dl_get_tls_static_info (size_t *sizep, size_t *alignp) EXPORT;
void
internal_function
_dl_get_tls_static_info (size_t *sizep, size_t *alignp)
{
  *sizep = g_mdl.tls_static_size;
  *alignp = g_mdl.tls_static_align;
}


void glibc_startup_finished (void) 
{
  _dl_starting_up = 1;
}


void glibc_initialize (void)
{
  _dl_open_hook = &g_dl_open_hook;
  void **(*fn) (void) = mdl_dl_error_catch_tsd;
  mdl_memcpy ((void*)_rtld_global+CONFIG_DL_ERROR_CATCH_TSD_OFFSET,
	      &fn, sizeof (fn));
}



void glibc_patch (struct MappedFile *file)
{
  MDL_LOG_FUNCTION ("file=%s", file->name);
  if (file->patched)
    {
      // if we are patched already, no need to do any work
      return;
    }
  // mark the file as patched
  file->patched = 1;

  // iterate over all deps first before initialization.
  struct MappedFileList *cur;
  for (cur = file->deps; cur != 0; cur = cur->next)
    {
      glibc_patch (cur->item);
    }

  unsigned long addr = mdl_elf_symbol_lookup_local ("_dl_addr", file);
  if (addr != 0)
    {
      machine_insert_trampoline (addr, (unsigned long) &mdl_dl_addr);
    }
}
