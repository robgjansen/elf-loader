#include "vdl-dl.h"
#include "machine.h"
#include "macros.h"
#include "vdl-lookup.h"
#include "stage1.h"
#include "vdl-log.h"

typedef void (*LibcFreeRes) (void);

EXPORT void libc_freeres_interceptor (void);
void
libc_freeres_interceptor (void)
{
  VDL_LOG_FUNCTION ("");
  // call glibc function
  LibcFreeRes libc_freeres = (LibcFreeRes) vdl_dlvsym_with_flags (RTLD_DEFAULT, "__libc_freeres",
								  0, VDL_LOOKUP_NO_REMAP,
								  RETURN_ADDRESS);
  if (libc_freeres != 0)
    {
      libc_freeres ();
    }

  if (g_vdl.finalized)
    {
      // if __libc_freeres is called while g_vdl.finalized is set to 1, it
      // means that stage2_finalize has already run which means that
      // we are called from within exit_group so, we are running under 
      // valgrind. If this is so, we do perform final shutdown
      // of everything here. We are allowed to do so because 
      // this function will return to vgpreload_core and the process
      // will terminate immediately after.
      stage1_freeres ();
    }
}

void
valgrind_initialize (void)
{
  VDL_LOG_FUNCTION ("");
  // we intercept only in the first context under the assumption that it's this
  // context which is going to trigger the exit_group syscall
  // which is the piece of code which will call __libc_freeres
  vdl_context_add_symbol_remap (g_vdl.contexts,
				"__libc_freeres", 0, 0,
				"libc_freeres_interceptor", 0, 0);
}
