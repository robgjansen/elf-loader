#include "glibc.h"
#include "machine.h"
#include "vdl.h"
#include "vdl-utils.h"
#include "vdl-log.h"
#include "vdl-dl.h"
#include "vdl-lookup.h"
#include "vdl-list.h"
#include "vdl-tls.h"
#include "vdl-sort.h"
#include "vdl-config.h"
#include "vdl-mem.h"
#include "vdl-file.h"
#include "futex.h"
#include "macros.h"
#include <elf.h>
#include <dlfcn.h>
#include <link.h>
#include <stdbool.h>


#define WEAK __attribute__ ((weak))

// Set to zero until just before main is invoked
// at which point it must be set to 1. Specifically,
// it is zero during the .init function execution.
// On my system, surprisingly, the dynamic loader
// does not export this symbol so, one could wonder
// why we bother with it.
// XXX: check on a couple more systems if we can't
// get rid of it.
static int __dl_starting_up = 0 ;
// and, then, we define the exported symbol as an alias to the local symbol.
extern __typeof (__dl_starting_up) _dl_starting_up __attribute__ ((alias("__dl_starting_up"), 
								   visibility("default")));

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

static char _rtld_local_ro[CONFIG_RTLD_GLOBAL_RO_SIZE];
// and, then, we define the exported symbol as an alias to the local symbol.
extern __typeof (_rtld_local_ro) _rtld_global_ro __attribute__ ((alias("_rtld_local_ro"),
                                                                 visibility("default")));
// We have to define first a local symbol to ensure that all references
// to this symbol do not go through the GOT.
static char _rtld_local[CONFIG_RTLD_GLOBAL_SIZE];
// and, then, we define the exported symbol as an alias to the local symbol.
extern __typeof (_rtld_local) _rtld_global __attribute__ ((alias("_rtld_local"), 
							   visibility("default")));

//_r_debug;
//__libc_memalign;

#if 0
static void **vdl_dl_error_catch_tsd (void)
{
  static void *data;
  return &data;
}
#endif

// definition stolen from glibc...
struct tls_index
{
  unsigned long int ti_module;
  unsigned long int ti_offset;
};
EXPORT void *
__tls_get_addr (struct tls_index *ti)
{
  void *retval = (void*) vdl_tls_get_addr_fast (ti->ti_module, ti->ti_offset);
  if (retval == 0)
    {
      futex_lock (g_vdl.futex);      
      retval = (void*) vdl_tls_get_addr_slow (ti->ti_module, ti->ti_offset);
      futex_unlock (g_vdl.futex);
    }
  return retval;
}

// It's hard to believe but _dl_get_tls_static_info as well as other functions
// were defined with a non-standard calling convention as an optimization. Since
// these functions are called by the outside world, we have to do precisely
// the same to be compatible.
# if defined (__i386__)
#  define internal_function   __attribute ((regparm (3), stdcall))
// this is the prototype for the GNU version of the i386 TLS ABI
// note the extra leading underscore used here to identify this optimized
// version of _get_addr
EXPORT void *
__attribute__ ((__regparm__ (1))) ___tls_get_addr (struct tls_index *ti)
{
  void *retval = (void*) vdl_tls_get_addr_fast (ti->ti_module, ti->ti_offset);
  if (retval == 0)
    {
      futex_lock (g_vdl.futex);      
      retval = (void*) vdl_tls_get_addr_slow (ti->ti_module, ti->ti_offset);
      futex_unlock (g_vdl.futex);
    }
  return retval;
}
# else
#  define internal_function
# endif

EXPORT void
internal_function
_dl_get_tls_static_info (size_t *sizep, size_t *alignp)
{
  // This method is called from __pthread_initialize_minimal_internal (nptl/init.c)
  // It is called from the .init constructors in libpthread.so
  // We thus must make sure that we have correctly initialized all static tls data
  // structures _before_ calling the constructors.
  // Both of these variables are used by the pthread library to correctly
  // size and align the stack for every newly-created thread. i.e., the pthread
  // library allocates on the stack the static tls area and delegates initialization
  // to _dl_allocate_tls_init below.
  // This method must return the _total_ size for the thread tls area, including the thread descriptor
  // stored next to it.
  *sizep = g_vdl.tls_static_total_size + CONFIG_TCB_SIZE;
  *alignp = g_vdl.tls_static_align;
}

// This function is called from within pthread_create to initialize
// the content of the dtv for a new thread before giving control to
// that new thread
EXPORT 
void *
internal_function
_dl_allocate_tls_init (void *tcb)
{
  if (tcb == 0)
    {
      return 0;
    }
  futex_lock (g_vdl.futex);

  vdl_tls_dtv_initialize ((unsigned long)tcb);

  futex_unlock (g_vdl.futex);
  return tcb;
}
// This function is called from within pthread_create to allocate
// memory for the dtv of the thread. Optionally, the caller
// also is able to delegate memory allocation of the tcb
// to this function
EXPORT 
void *
internal_function
_dl_allocate_tls (void *mem)
{
  futex_lock (g_vdl.futex);

  unsigned long tcb = (unsigned long)mem;
  if (tcb == 0)
    {
      tcb = vdl_tls_tcb_allocate ();
    }
  vdl_tls_dtv_allocate (tcb);
  vdl_tls_dtv_initialize ((unsigned long)tcb);

  futex_unlock (g_vdl.futex);
  return (void*)tcb;
}
EXPORT 
void
internal_function
_dl_deallocate_tls (void *ptcb, bool dealloc_tcb)
{
  futex_lock (g_vdl.futex);

  unsigned long tcb = (unsigned long) ptcb;
  vdl_tls_dtv_deallocate (tcb);
  if (dealloc_tcb)
    {
      vdl_tls_tcb_deallocate (tcb);
    }

  futex_unlock (g_vdl.futex);
}
EXPORT
int
internal_function
_dl_make_stack_executable (void **stack_endp)
{
  return 0;
}

void glibc_startup_finished (void) 
{
  __dl_starting_up = 1;
}


void glibc_initialize (void)
{
//  void **(*fn) (void) = vdl_dl_error_catch_tsd;
//  char *dst = &_rtld_local[CONFIG_DL_ERROR_CATCH_TSD_OFFSET];
//  vdl_memcpy ((void*)dst, &fn, sizeof (fn));
  char *off = &_rtld_local_ro[CONFIG_RTLD_DL_PAGESIZE_OFFSET];
  int pgsz = system_getpagesize ();
  vdl_memcpy (off, &pgsz, sizeof (pgsz));
}


static void *
dlsym_hack (void *handle, const char *symbol)
{
  return vdl_dlsym (handle, symbol, RETURN_ADDRESS);
}

// Typically called by malloc to lookup ptmalloc_init.
// In this case, symbolp is 0.
int
internal_function
_dl_addr_hack (const void *address, Dl_info *info,
	       void **mapp, const ElfW(Sym) **symbolp)
{
  return vdl_dladdr1 (address, info, mapp, RTLD_DL_LINKMAP);
}


void 
do_glibc_patch (struct VdlFile *file)
{
  VDL_LOG_FUNCTION ("file=%s", file->name);
  if (file->patched)
    {
      // if we are patched already, no need to do any work
      return;
    }
  // mark the file as patched
  file->patched = 1;

  struct VdlLookupResult result;
  result = vdl_lookup_local (file, "_dl_addr");
  if (result.found)
    {
      unsigned long addr = file->load_base + result.symbol->st_value;
      bool ok = machine_insert_trampoline (addr, (unsigned long) &_dl_addr_hack,
					   result.symbol->st_value);
      VDL_LOG_ASSERT (ok, "Unable to intercept dl_addr. Check your selinux config.");
    }
  result = vdl_lookup_local (file, "__libc_dlopen_mode");
  if (result.found)
    {
      unsigned long addr = file->load_base + result.symbol->st_value;
      bool ok = machine_insert_trampoline (addr, (unsigned long) &vdl_dlopen, 
					   result.symbol->st_size);
      VDL_LOG_ASSERT (ok, "Unable to intercept dl_addr. Check your selinux config.");
    }
  result = vdl_lookup_local (file, "__libc_dlclose");
  if (result.found)
    {
      unsigned long addr = file->load_base + result.symbol->st_value;
      bool ok = machine_insert_trampoline (addr, (unsigned long) &vdl_dlclose,
					   result.symbol->st_size);
      VDL_LOG_ASSERT (ok, "Unable to intercept dl_addr. Check your selinux config.");
    }
  result = vdl_lookup_local (file, "__libc_dlsym");
  if (result.found)
    {
      unsigned long addr = file->load_base + result.symbol->st_value;
      bool ok = machine_insert_trampoline (addr, (unsigned long) &dlsym_hack, 
					   result.symbol->st_value);
      VDL_LOG_ASSERT (ok, "Unable to intercept dl_addr. Check your selinux config.");
    }
}

void glibc_patch (struct VdlList *files)
{
  struct VdlList *sorted = vdl_sort_increasing_depth (files);
  vdl_list_reverse (sorted);
  vdl_list_iterate (files, (void(*)(void*))do_glibc_patch);
  vdl_list_delete (sorted);
}
