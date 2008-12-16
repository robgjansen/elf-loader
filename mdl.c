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

static struct StringList *
get_system_search_dirs (void)
{
  const char *dirs[] = {"/lib", "/lib64", "/lib32",
			"/usr/lib", "/usr/lib64", "/usr/lib32"};
  struct StringList *list = 0;
  int i;
  for (i = 0; i < sizeof (dirs)/sizeof(char *); i++)
    {
      struct StringList *tmp = mdl_new (struct StringList);
      tmp->str = mdl_strdup (dirs[i]);
      tmp->next = list;
      list = tmp;
    }
  list = mdl_str_list_reverse (list);
  return list;
}


void mdl_initialize (unsigned long interpreter_load_base)
{
  struct Mdl *mdl = &g_mdl;
  mdl->version = 1;
  mdl->link_map = 0;
  mdl->breakpoint = mdl_breakpoint;
  mdl->state = MDL_CONSISTENT;
  mdl->interpreter_load_base = interpreter_load_base;
  mdl->logging = MDL_LOG_ERR;
  mdl->search_dirs = 0;
  mdl->next_context = 0;
  alloc_initialize (&(mdl->alloc));
  mdl->search_dirs = 0;
  mdl->bind_now = 0; // by default, do lazy binding

  // populate search dirs from system directories
  mdl->search_dirs = mdl_str_list_append (mdl->search_dirs, 
					  get_system_search_dirs ());
}
void mdl_set_logging (const char *debug_str)
{
  MDL_LOG_FUNCTION ("debug=%s", debug_str);
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
  g_mdl.logging |= logging;
  mdl_str_list_free (list);
}


void *mdl_malloc (size_t size)
{
  MDL_LOG_FUNCTION ("size=%d", size);
  return (void*)alloc_malloc (&g_mdl.alloc, size);
}
void mdl_free (void *buffer, size_t size)
{
  MDL_LOG_FUNCTION ("buffer=%p, size=%d", buffer, size);
  alloc_free (&g_mdl.alloc, (uint8_t *)buffer, size);
}
int mdl_strisequal (const char *a, const char *b)
{
  MDL_LOG_FUNCTION ("a=%s, b=%s", a, b);
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
  MDL_LOG_FUNCTION ("str=%s", str);
  int len = 0;
  while (str[len] != 0)
    {
      len++;
    }
  return len;
}
char *mdl_strdup (const char *str)
{
  MDL_LOG_FUNCTION ("str=%s", str);
  int len = mdl_strlen (str);
  char *retval = mdl_malloc (len+1);
  mdl_memcpy (retval, str, len+1);
  return retval;
}
void mdl_memcpy (void *d, const void *s, size_t len)
{
  MDL_LOG_FUNCTION ("dst=%p, src=%p, len=%d", d, s, len);
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
char *mdl_strconcat (const char *str, ...)
{
  MDL_LOG_FUNCTION ("str=%s", str);
  va_list l1, l2;
  uint32_t size;
  char *cur, *retval, *tmp;
  size = mdl_strlen (str);
  va_start (l1, str);
  va_copy (l2, l1);
  // calculate size of final string
  cur = va_arg (l1, char *);
  while (cur != 0)
    {
      size += mdl_strlen (cur);
      cur = va_arg (l1, char *);
    }
  va_end (l1);
  retval = mdl_malloc (size + 1);
  // copy first string
  tmp = retval;
  mdl_memcpy (tmp, str, mdl_strlen (str));
  tmp += mdl_strlen (str);
  // concatenate the other strings.
  cur = va_arg (l2, char *);
  while (cur != 0)
    {
      mdl_memcpy (tmp, cur, mdl_strlen (cur));
      tmp += mdl_strlen(cur);
      cur = va_arg (l2, char *);
    }
  // append final 0
  *tmp = 0;
  va_end (l2);
  return retval;
}
int mdl_exists (const char *filename)
{
  MDL_LOG_FUNCTION ("filename=%s", filename);
  struct stat buf;
  int status = system_fstat (filename, &buf);
  return status == 0;
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
  MDL_LOG_FUNCTION ("envp=%p, value=%s", envp, value);
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
  MDL_LOG_FUNCTION ("value=%s, separator=%d", value, separator);
  struct StringList *list = 0;
  const char *prev = value;
  const char *cur = value;

  if (value == 0)
    {
      return 0;
    }
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
  return mdl_str_list_reverse (list);
}
void mdl_str_list_free (struct StringList *list)
{
  MDL_LOG_FUNCTION ("list=%p", list);
  struct StringList *cur, *next;
  for (cur = list; cur != 0; cur = next)
    {
      mdl_free (cur->str, mdl_strlen (cur->str));
      next = cur->next;
      mdl_delete (cur);
    }
}
struct StringList *mdl_str_list_append (struct StringList *start, struct StringList *end)
{
  MDL_LOG_FUNCTION ("start=%p, end=%p", start, end);
  struct StringList *cur, *prev;
  for (cur = start, prev = 0; cur != 0; cur = cur->next)
    {
      prev = cur;
    }
  if (prev == 0)
    {
      return end;
    }
  else
    {
      prev->next = end;
      return start;
    }
}
struct StringList *mdl_str_list_reverse (struct StringList *list)
{
  MDL_LOG_FUNCTION ("list=%p", list);
  struct StringList *ret = 0, *cur, *next;
  for (cur = list; cur != 0; cur = next)
    {
      next = cur->next;
      cur->next = ret;
      ret = cur;
    }
  return ret;
}
void mdl_file_ref (struct MappedFile *file)
{
  file->count++;
}
void mdl_file_unref (struct MappedFile *file)
{
  file->count--;
  if (file->count == 0)
    {
      mdl_file_list_free (file->deps);
      // remove file from global link map
      // and count number of files in the same context
      // which share the same global scope.
      uint32_t scope_count = 0;
      struct MappedFile *cur;
      for (cur = g_mdl.link_map; cur != 0; cur = cur->next)
	{
	  if (cur->context == file->context &&
	      cur->global_scope == file->global_scope)
	    {
	      scope_count++;
	    }
	  if (cur == file)
	    {
	      cur->prev->next = cur->next;
	      cur->next->prev = cur->prev;
	      cur->next = 0;
	      cur->prev = 0;
	    }
	}
      if (scope_count <= 1)
	{
	  mdl_file_list_free (file->global_scope);
	}
      mdl_delete (file);
    }
}

void mdl_file_list_free (struct MappedFileList *list)
{
  struct MappedFileList *cur;
  for (cur = list; cur != 0; cur = cur->next)
    {
      mdl_file_unref (cur->item);
      mdl_delete (cur);
    }
}
struct MappedFileList *mdl_file_list_copy (struct MappedFileList *list)
{
  struct MappedFileList *copy = 0;
  struct MappedFileList *cur;
  for (cur = list; cur != 0; cur = cur->next)
    {
      copy = mdl_file_list_append_one (copy, cur->item);
    }
  return copy;
}

struct MappedFileList *mdl_file_list_append_one (struct MappedFileList *list, 
						 struct MappedFile *item)
{
  mdl_file_ref (item);
  if (list == 0)
    {
      list = mdl_new (struct MappedFileList);
      list->next = 0;
      list->item = item;
      return list;
    }
  struct MappedFileList *cur = list;
  while (cur->next != 0)
    {
      cur = cur->next;
    }
  cur->next = mdl_new (struct MappedFileList);
  cur->next->item = item;
  cur->next->next = 0;
  return list;
}
struct MappedFileList *mdl_file_list_append (struct MappedFileList *start, 
					     struct MappedFileList *end)
{
  struct MappedFileList *cur = start;
  if (cur == 0)
    {
      return end;
    }
  while (cur->next != 0)
    {
      cur = cur->next;
    }
  cur->next = end;
  return start;
}


