#include "mdl.h"
#include "alloc.h"

static struct Mdl g_real_mdl;
extern struct Mdl g_mdl __attribute__ ((weak, alias("g_real_mdl")));
extern struct Mdl _r_debug __attribute__ ((weak, alias("g_real_mdl")));


static int mdl_breakpoint (void)
{
  // the debugger will put a breakpoint here.
  return 1;
}

void mdl_initialize (uint8_t *interpreter_load_base)
{
  struct Mdl *mdl = &g_mdl;
  mdl->version = 1;
  mdl->link_map = 0;
  mdl->breakpoint = mdl_breakpoint;
  mdl->state = MDL_CONSISTENT;
  mdl->interpreter_load_base = interpreter_load_base;
  mdl->next_context = 0;
  alloc_initialize (&(mdl->alloc));
}

uint8_t *mdl_malloc (uint32_t size)
{
  return alloc_malloc (&g_mdl.alloc, size);
}
void mdl_free (uint8_t *buffer, uint32_t size)
{
  alloc_free (&g_mdl.alloc, buffer, size);
}

