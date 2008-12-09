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

void *mdl_malloc (size_t size)
{
  return (void*)alloc_malloc (&g_mdl.alloc, size);
}
void mdl_free (void *buffer, size_t size)
{
  alloc_free (&g_mdl.alloc, (uint8_t *)buffer, size);
}
int mdl_strisequal (const char *a, const char *b)
{
  while (*a != 0 && *b != 0)
    {
      if (*a != *b)
	{
	  return 0;
	}
      a++;
      b++;
    }
  return 1;
}
int mdl_strlen (const char *str)
{
  const char *tmp = str;
  int len = 0;
  while (*tmp != 0)
    {
      len++;
    }
  return len;
}
char *mdl_strdup (const char *str)
{
  int len = mdl_strlen (str);
  char *retval = mdl_malloc (len+1);
  mdl_memcpy (retval, str, len+1);
  return retval;
}
void mdl_memcpy (void *d, const void *s, size_t len)
{
  int tmp = len;
  char *dst = d;
  const char *src = s;
  while (tmp > 0)
    {
      *dst = *src;
      dst++;
      src++;
      tmp--;
    }
}


