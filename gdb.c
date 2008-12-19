#include "gdb.h"
#include "mdl.h"
#include "mdl-elf.h"
#include <elf.h>
#include <link.h>

// The name of this function is important: gdb hardcodes 
// this name itself.
static int _r_debug_state (void)
{
  // GDB
  // the debugger will put a breakpoint here.
  return 1;
}

void gdb_initialize (struct MappedFile *file)
{
  // The breakpoint member is not really used by gdb.
  // instead, gdb hardcodes the name _r_debug_state.
  // But, hey, I am trying to be nice, just in case.
  g_mdl.breakpoint = _r_debug_state;
  g_mdl.state = MDL_CONSISTENT;

  // It is important to do this, that is, store a pointer to g_mdl
  // in the DT_DEBUG entry of the main executable dynamic section
  // because this is where gdb goes to look to find the pointer and
  // lookup an inferior's linkmap.
  ElfW(Dyn) *dt_debug = mdl_elf_file_get_dynamic (file, DT_DEBUG);
  unsigned long *p = (unsigned long *)&(dt_debug->d_un.d_ptr);
  *p = (unsigned long)&g_mdl;
}

void gdb_notify (void)
{
  g_mdl.state = MDL_CONSISTENT;
  g_mdl.breakpoint ();
}
