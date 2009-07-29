#include "vdl-utils.h"
#include <stdarg.h>
#include "vdl.h"
#include "vdl-log.h"
#include "avprintf-cb.h"


void vdl_utils_linkmap_print (void)
{
  struct VdlFile *cur;
  for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
    {
      vdl_log_printf (VDL_LOG_PRINT, 
		      "load_base=0x%x , file=%s\n"
		      "\tro_start=0x%x , ro_end=0x%x\n"
		      "\tro_zero_start=0x%x , ro_zero_end=0x%x\n"
		      "\tro_anon_start=0x%x , ro_anon_end=0x%x\n"
		      "\trw_start=0x%x , rw_end=0x%x\n", 
		      
		      cur->load_base, 
		      cur->filename,
		      cur->ro_map.mem_start_align, 
		      cur->ro_map.mem_start_align + cur->ro_map.mem_size_align, 
		      cur->ro_map.mem_zero_start,
		      cur->ro_map.mem_zero_start + cur->ro_map.mem_zero_size,
		      cur->ro_map.mem_anon_start_align,
		      cur->ro_map.mem_anon_start_align + cur->ro_map.mem_anon_size_align,
		      cur->rw_map.mem_start_align, 
		      cur->rw_map.mem_start_align + cur->rw_map.mem_size_align);
    }
}


void *vdl_utils_malloc (size_t size)
{
  VDL_LOG_FUNCTION ("size=%d", size);
#ifdef MALLOC_DEBUG_ENABLE
  unsigned long *buffer = (unsigned long*)alloc_malloc (&g_vdl.alloc, 
							size+2*sizeof(unsigned long));
  buffer[0] = (unsigned long)buffer;
  buffer[1] = size;
  return (void*)(buffer+2);
#else
  return (void*)alloc_malloc (&g_vdl.alloc, size);
#endif
}
void vdl_utils_free (void *buffer, size_t size)
{
  VDL_LOG_FUNCTION ("buffer=%p, size=%d", buffer, size);
#ifdef MALLOC_DEBUG_ENABLE
  unsigned long *buf = (unsigned long*)buffer;
  VDL_LOG_ASSERT (buf[-2] == (unsigned long)(buf-2), "freeing invalid buffer");
  VDL_LOG_ASSERT (buf[-1] == size, "freeing invalid size");
  buf[-2] = 0xdeadbeaf;
  buf[-1] = 0xdeadbeaf;
  alloc_free (&g_vdl.alloc, (uint8_t *)(buf-2), size+2*sizeof(unsigned long));
#else
  alloc_free (&g_vdl.alloc, (uint8_t *)buffer, size);
#endif
}
int vdl_utils_strisequal (const char *a, const char *b)
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
int vdl_utils_strlen (const char *str)
{
  //VDL_LOG_FUNCTION ("str=%s", str);
  int len = 0;
  while (str[len] != 0)
    {
      len++;
    }
  return len;
}
void vdl_utils_strfree (char *str)
{
  vdl_utils_free (str, vdl_utils_strlen (str)+1);
}
char *vdl_utils_strdup (const char *str)
{
  //VDL_LOG_FUNCTION ("str=%s", str);
  int len = vdl_utils_strlen (str);
  char *retval = vdl_utils_malloc (len+1);
  vdl_utils_memcpy (retval, str, len+1);
  return retval;
}
void vdl_utils_memcpy (void *d, const void *s, size_t len)
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
void vdl_utils_memset(void *d, int c, size_t n)
{
  char *dst = d;
  size_t i;
  for (i = 0; i < n; i++)
    {
      dst[i] = c;
    }
}
char *vdl_utils_strconcat (const char *str, ...)
{
  VDL_LOG_FUNCTION ("str=%s", str);
  va_list l1, l2;
  uint32_t size;
  char *cur, *retval, *tmp;
  size = vdl_utils_strlen (str);
  va_start (l1, str);
  va_copy (l2, l1);
  // calculate size of final string
  cur = va_arg (l1, char *);
  while (cur != 0)
    {
      size += vdl_utils_strlen (cur);
      cur = va_arg (l1, char *);
    }
  va_end (l1);
  retval = vdl_utils_malloc (size + 1);
  // copy first string
  tmp = retval;
  vdl_utils_memcpy (tmp, str, vdl_utils_strlen (str));
  tmp += vdl_utils_strlen (str);
  // concatenate the other strings.
  cur = va_arg (l2, char *);
  while (cur != 0)
    {
      vdl_utils_memcpy (tmp, cur, vdl_utils_strlen (cur));
      tmp += vdl_utils_strlen(cur);
      cur = va_arg (l2, char *);
    }
  // append final 0
  *tmp = 0;
  va_end (l2);
  return retval;
}
int vdl_utils_exists (const char *filename)
{
  VDL_LOG_FUNCTION ("filename=%s", filename);
  struct stat buf;
  int status = system_fstat (filename, &buf);
  return status == 0;
}
const char *vdl_utils_getenv (const char **envp, const char *value)
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
struct VdlStringList *vdl_utils_strsplit (const char *value, char separator)
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
      next = vdl_utils_new (struct VdlStringList);
      next->str = vdl_utils_malloc (prev_len+1);
      vdl_utils_memcpy (next->str, prev, prev_len);
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
  return vdl_utils_str_list_reverse (list);
}
void vdl_utils_str_list_free (struct VdlStringList *list)
{
  VDL_LOG_FUNCTION ("list=%p", list);
  struct VdlStringList *cur, *next;
  for (cur = list; cur != 0; cur = next)
    {
      vdl_utils_strfree (cur->str);
      next = cur->next;
      vdl_utils_delete (cur);
    }
}
struct VdlStringList *vdl_utils_str_list_append (struct VdlStringList *start, struct VdlStringList *end)
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
struct VdlStringList * vdl_utils_str_list_prepend (struct VdlStringList *start, 
						   struct VdlStringList *end)
{
  return vdl_utils_str_list_append (end, start);
}
struct VdlStringList *
vdl_utils_str_list_split (struct VdlStringList *start, 
			  struct VdlStringList *at)
{
  if (start == at)
    {
      return 0;
    }
  struct VdlStringList *cur;
  for (cur = start; cur != 0; cur = cur->next)
    {
      if (cur->next == at)
	{
	  cur->next = 0;
	  return start;
	}
    }
  return start;
}

struct VdlStringList *vdl_utils_str_list_reverse (struct VdlStringList *list)
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
struct VdlStringList *vdl_utils_splitpath (const char *value)
{
  struct VdlStringList *list = vdl_utils_strsplit (value, ':');
  struct VdlStringList *cur;
  for (cur = list; cur != 0; cur = cur->next)
    {
      if (vdl_utils_strisequal (cur->str, ""))
	{
	  // the empty string is interpreted as '.'
	  vdl_utils_strfree (cur->str);
	  cur->str = vdl_utils_strdup (".");
	}
    }
  return list;
}


unsigned long vdl_utils_align_down (unsigned long v, unsigned long align)
{
  if ((v % align) == 0)
    {
      return v;
    }
  unsigned long aligned = v - (v % align);
  return aligned;
}
unsigned long vdl_utils_align_up (unsigned long v, unsigned long align)
{
  if ((v % align) == 0)
    {
      return v;
    }
  unsigned long aligned = v + align - (v % align);
  return aligned;
}

ElfW(Phdr) *vdl_utils_search_phdr (ElfW(Phdr) *phdr, int phnum, int type)
{
  VDL_LOG_FUNCTION ("phdr=%p, phnum=%d, type=%d", phdr, phnum, type);
  ElfW(Phdr) *cur;
  int i;
  for (cur = phdr, i = 0; i < phnum; cur++, i++)
    {
      if (cur->p_type == type)
	{
	  return cur;
	}
    }
  return 0;
}

// Note that this implementation is horribly inneficient but it's
// also incredibly simple. A more efficient implementation would
// pre-allocate a large string buffer and would create a larger
// buffer only when needed to avoid the very very many memory
// allocations and frees done for each caracter.
static void avprintf_callback (char c, void *context)
{
  if (c != 0)
    {
      char **pstr = (char**)context;
      char new_char[] = {c, 0};
      char *new_str = vdl_utils_strconcat (*pstr, new_char, 0);
      vdl_utils_strfree (*pstr);
      *pstr = new_str;
    }
}


char *vdl_utils_vprintf (const char *str, va_list args)
{
  char *retval = vdl_utils_strdup ("");
  int status = avprintf_cb (avprintf_callback, &retval, str, args);
  if (status < 0)
    {
      return 0;
    }
  return retval;
}
