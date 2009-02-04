#include "vdl-utils.h"
#include <stdarg.h>
#include "avprintf-cb.h"
#include "vdl.h"


void vdl_linkmap_print (void)
{
  struct VdlFile *cur;
  for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
    {
      vdl_log_printf (VDL_LOG_PRINT, 
		      "load_base=0x%x , ro_start=0x%x , ro_end=0x%x , file=%s\n", 
		      cur->load_base, cur->ro_start, cur->ro_start + cur->ro_size, 
		      cur->filename);
    }
}

void vdl_set_logging (const char *debug_str)
{
  VDL_LOG_FUNCTION ("debug=%s", debug_str);
  if (debug_str == 0)
    {
      return;
    }
  struct VdlStringList *list = vdl_strsplit (debug_str, ':');
  struct VdlStringList *cur;
  uint32_t logging = 0;
  for (cur = list; cur != 0; cur = cur->next)
    {
      if (vdl_strisequal (cur->str, "debug"))
	{
	  logging |= VDL_LOG_DBG;
	}
      else if (vdl_strisequal (cur->str, "function"))
	{
	  logging |= VDL_LOG_FUNC;
	}
      else if (vdl_strisequal (cur->str, "error"))
	{
	  logging |= VDL_LOG_ERR;
	}
      else if (vdl_strisequal (cur->str, "assert"))
	{
	  logging |= VDL_LOG_AST;
	}
      else if (vdl_strisequal (cur->str, "symbol-fail"))
	{
	  logging |= VDL_LOG_SYM_FAIL;
	}
      else if (vdl_strisequal (cur->str, "symbol-ok"))
	{
	  logging |= VDL_LOG_SYM_OK;
	}
      else if (vdl_strisequal (cur->str, "reloc"))
	{
	  logging |= VDL_LOG_REL;
	}
      else if (vdl_strisequal (cur->str, "help"))
	{
	  VDL_LOG_ERROR ("Available logging levels: debug, function, error, assert, symbol-fail, symbol-ok, reloc\n", 1);
	}
    }
  g_vdl.logging |= logging;
  vdl_str_list_free (list);
}


void *vdl_malloc (size_t size)
{
  VDL_LOG_FUNCTION ("size=%d", size);
  return (void*)alloc_malloc (&g_vdl.alloc, size);
}
void vdl_free (void *buffer, size_t size)
{
  VDL_LOG_FUNCTION ("buffer=%p, size=%d", buffer, size);
  alloc_free (&g_vdl.alloc, (uint8_t *)buffer, size);
}
int vdl_strisequal (const char *a, const char *b)
{
  //VDL_LOG_FUNCTION ("a=%s, b=%s", a, b);
  while (*a != 0 && *b != 0)
    {
      if (*a != *b)
	{
	  return 0;
	}
      a++;
      b++;
    }
  return *a == *b;
}
int vdl_strlen (const char *str)
{
  //VDL_LOG_FUNCTION ("str=%s", str);
  int len = 0;
  while (str[len] != 0)
    {
      len++;
    }
  return len;
}
char *vdl_strdup (const char *str)
{
  //VDL_LOG_FUNCTION ("str=%s", str);
  int len = vdl_strlen (str);
  char *retval = vdl_malloc (len+1);
  vdl_memcpy (retval, str, len+1);
  return retval;
}
void vdl_memcpy (void *d, const void *s, size_t len)
{
  //VDL_LOG_FUNCTION ("dst=%p, src=%p, len=%d", d, s, len);
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
void vdl_memset(void *d, int c, size_t n)
{
  char *dst = d;
  size_t i;
  for (i = 0; i < n; i++)
    {
      dst[i] = c;
    }
}
char *vdl_strconcat (const char *str, ...)
{
  VDL_LOG_FUNCTION ("str=%s", str);
  va_list l1, l2;
  uint32_t size;
  char *cur, *retval, *tmp;
  size = vdl_strlen (str);
  va_start (l1, str);
  va_copy (l2, l1);
  // calculate size of final string
  cur = va_arg (l1, char *);
  while (cur != 0)
    {
      size += vdl_strlen (cur);
      cur = va_arg (l1, char *);
    }
  va_end (l1);
  retval = vdl_malloc (size + 1);
  // copy first string
  tmp = retval;
  vdl_memcpy (tmp, str, vdl_strlen (str));
  tmp += vdl_strlen (str);
  // concatenate the other strings.
  cur = va_arg (l2, char *);
  while (cur != 0)
    {
      vdl_memcpy (tmp, cur, vdl_strlen (cur));
      tmp += vdl_strlen(cur);
      cur = va_arg (l2, char *);
    }
  // append final 0
  *tmp = 0;
  va_end (l2);
  return retval;
}
int vdl_exists (const char *filename)
{
  VDL_LOG_FUNCTION ("filename=%s", filename);
  struct stat buf;
  int status = system_fstat (filename, &buf);
  return status == 0;
}
static void avprintf_callback (char c, void *context)
{
  if (c != 0)
    {
      system_write (2, &c, 1);
    }
}
void vdl_log_printf (enum VdlLog log, const char *str, ...)
{
  va_list list;
  va_start (list, str);
  if (g_vdl.logging & log)
    {
      avprintf_cb (avprintf_callback, 0, str, list);
    }
  va_end (list);
}
const char *vdl_getenv (const char **envp, const char *value)
{
  VDL_LOG_FUNCTION ("envp=%p, value=%s", envp, value);
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
struct VdlStringList *vdl_strsplit (const char *value, char separator)
{
  VDL_LOG_FUNCTION ("value=%s, separator=%d", value, separator);
  struct VdlStringList *list = 0;
  const char *prev = value;
  const char *cur = value;

  if (value == 0)
    {
      return 0;
    }
  while (1)
    {
      struct VdlStringList *next;
      size_t prev_len;
      while (*cur != separator && *cur != 0)
	{
	  cur++;
	}
      prev_len = cur-prev;
      next = vdl_new (struct VdlStringList);
      next->str = vdl_malloc (prev_len+1);
      vdl_memcpy (next->str, prev, prev_len);
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
  return vdl_str_list_reverse (list);
}
void vdl_str_list_free (struct VdlStringList *list)
{
  VDL_LOG_FUNCTION ("list=%p", list);
  struct VdlStringList *cur, *next;
  for (cur = list; cur != 0; cur = next)
    {
      vdl_free (cur->str, vdl_strlen (cur->str));
      next = cur->next;
      vdl_delete (cur);
    }
}
struct VdlStringList *vdl_str_list_append (struct VdlStringList *start, struct VdlStringList *end)
{
  VDL_LOG_FUNCTION ("start=%p, end=%p", start, end);
  struct VdlStringList *cur, *prev;
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
struct VdlStringList *vdl_str_list_reverse (struct VdlStringList *list)
{
  VDL_LOG_FUNCTION ("list=%p", list);
  struct VdlStringList *ret = 0, *cur, *next;
  for (cur = list; cur != 0; cur = next)
    {
      next = cur->next;
      cur->next = ret;
      ret = cur;
    }
  return ret;
}

void vdl_file_list_free (struct VdlFileList *list)
{
  struct VdlFileList *cur;
  for (cur = list; cur != 0; cur = cur->next)
    {
      vdl_file_unref (cur->item);
      vdl_delete (cur);
    }
}
struct VdlFileList *vdl_file_list_copy (struct VdlFileList *list)
{
  struct VdlFileList *copy = 0;
  struct VdlFileList *cur;
  for (cur = list; cur != 0; cur = cur->next)
    {
      copy = vdl_file_list_append_one (copy, cur->item);
    }
  return copy;
}

struct VdlFileList *vdl_file_list_append_one (struct VdlFileList *list, 
						 struct VdlFile *item)
{
  vdl_file_ref (item);
  if (list == 0)
    {
      list = vdl_new (struct VdlFileList);
      list->next = 0;
      list->item = item;
      return list;
    }
  struct VdlFileList *cur = list;
  while (cur->next != 0)
    {
      cur = cur->next;
    }
  cur->next = vdl_new (struct VdlFileList);
  cur->next->item = item;
  cur->next->next = 0;
  return list;
}
static struct VdlFileList *
vdl_file_list_get_end (struct VdlFileList *start)
{
  if (start == 0)
    {
      return 0;
    }
  struct VdlFileList *cur = start;
  while (cur->next != 0)
    {
      cur = cur->next;
    }
  return cur;
}
struct VdlFileList *vdl_file_list_append (struct VdlFileList *start, 
					     struct VdlFileList *last)
{
  if (start == 0)
    {
      return last;
    }
  struct VdlFileList *end = vdl_file_list_get_end (start);
  end->next = last;
  return start;
}

void vdl_file_list_unicize (struct VdlFileList *list)
{
  struct VdlFileList *cur;
  for (cur = list; cur != 0; cur = cur->next)
    {
      struct VdlFileList *tmp, *prev;
      for (prev = cur, tmp = cur->next; tmp != 0; prev = tmp, tmp = tmp->next)
	{
	  if (cur == tmp)
	    {
	      // if we have a duplicate, we eliminate it from the list
	      prev->next = tmp->next;
	      vdl_file_unref (cur->item);
	      vdl_delete (cur);
	    }
	}
    }
}
unsigned long vdl_align_down (unsigned long v, unsigned long align)
{
  if ((v % align) == 0)
    {
      return v;
    }
  unsigned long aligned = v - (v % align);
  return aligned;
}
unsigned long vdl_align_up (unsigned long v, unsigned long align)
{
  if ((v % align) == 0)
    {
      return v;
    }
  unsigned long aligned = v + align - (v % align);
  return aligned;
}

