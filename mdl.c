#include "mdl.h"
#include "alloc.h"
#include "system.h"
#include "avprintf-cb.h"
#include <stdarg.h>

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
  mdl->logging = 0;
  mdl->search_dirs = 0;
  mdl->next_context = 0;
  alloc_initialize (&(mdl->alloc));
}
void mdl_set_logging (const char *debug_str)
{
  MDL_LOG_FUNCTION;
  if (debug_str == 0)
    {
      return;
    }
  struct StringList *list = mdl_strsplit (debug_str, ':');
  struct StringList *cur;
  uint32_t logging = 0;
  for (cur = list; cur != 0; cur = cur->next)
    {
      if (mdl_strisequal (cur->str, "debug"))
	{
	  logging |= MDL_LOG_DBG;
	}
      else if (mdl_strisequal (cur->str, "function"))
	{
	  logging |= MDL_LOG_FUNC;
	}
    }
  g_mdl.logging = logging;
  mdl_str_list_free (list);
}


void *mdl_malloc (size_t size)
{
  MDL_LOG_FUNCTION;
  return (void*)alloc_malloc (&g_mdl.alloc, size);
}
void mdl_free (void *buffer, size_t size)
{
  MDL_LOG_FUNCTION;
  alloc_free (&g_mdl.alloc, (uint8_t *)buffer, size);
}
int mdl_strisequal (const char *a, const char *b)
{
  MDL_LOG_FUNCTION;
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
  MDL_LOG_FUNCTION;
  int len = 0;
  while (str[len] != 0)
    {
      len++;
    }
  return len;
}
char *mdl_strdup (const char *str)
{
  MDL_LOG_FUNCTION;
  int len = mdl_strlen (str);
  char *retval = mdl_malloc (len+1);
  mdl_memcpy (retval, str, len+1);
  return retval;
}
void mdl_memcpy (void *d, const void *s, size_t len)
{
  MDL_LOG_FUNCTION;
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
static void avprintf_callback (char c, void *context)
{
  system_write (2, &c, 1);
}
void mdl_log_printf (enum MdlLog log, const char *str, ...)
{
  va_list list;
  va_start (list, str);
  if (g_mdl.logging & log)
    {
      avprintf_cb (avprintf_callback, 0, str, list);
    }
  va_end (list);
}
const char *mdl_getenv (const char **envp, const char *value)
{
  MDL_LOG_FUNCTION;
  while (*envp != 0)
    {
      const char *env = *envp;
      const char *tmp = value;
      while (*tmp != 0 && *env != 0)
	{
	  if (*tmp != *env)
	    {
	      goto next;
	    }
	  env++;
	  tmp++;
	}
      if (*env != '=')
	{
	  goto next;
	}
      env++;
      return env;
    next:
      envp++;
    }
  return 0;
}
struct StringList *mdl_strsplit (const char *value, char separator)
{
  MDL_LOG_FUNCTION;
  struct StringList *list = 0;
  const char *prev = value;
  const char *cur = value;
  while (1)
    {
      struct StringList *next;
      size_t prev_len;
      while (*cur != separator && *cur != 0)
	{
	  cur++;
	}
      prev_len = cur-prev;
      next = mdl_new (struct StringList);
      next->str = mdl_malloc (prev_len+1);
      mdl_memcpy (next->str, prev, prev_len);
      next->str[prev_len] = 0;
      next->next = list;
      list = next;
      if (*cur == 0)
	{
	  break;
	}
      cur++;
      prev = cur;
    }
  return list;
}
void mdl_str_list_free (struct StringList *list)
{
  struct StringList *cur, *next;
  for (cur = list; cur != 0; cur = next)
    {
      mdl_free (cur->str, mdl_strlen (cur->str));
      next = cur->next;
      mdl_delete (cur);
    }
}

