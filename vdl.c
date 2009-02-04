#include "vdl.h"
#include "alloc.h"
#include "system.h"
#include "avprintf-cb.h"
#include <stdarg.h>

struct Vdl g_vdl;

static struct VdlStringList *
get_system_search_dirs (void)
{
  // XXX: first is for my ubuntu box.
  const char *dirs[] = {"/lib/tls/i686/cmov",
			"/lib", "/lib64", "/lib32",
			"/usr/lib", "/usr/lib64", "/usr/lib32"};
  struct VdlStringList *list = 0;
  int i;
  for (i = 0; i < sizeof (dirs)/sizeof(char *); i++)
    {
      struct VdlStringList *tmp = vdl_new (struct VdlStringList);
      tmp->str = vdl_strdup (dirs[i]);
      tmp->next = list;
      list = tmp;
    }
  list = vdl_str_list_reverse (list);
  return list;
}


void vdl_initialize (unsigned long interpreter_load_base)
{
  struct Vdl *vdl = &g_vdl;
  vdl->version = 1;
  vdl->link_map = 0;
  vdl->breakpoint = 0;
  vdl->state = VDL_CONSISTENT;
  vdl->interpreter_load_base = interpreter_load_base;
  vdl->logging = VDL_LOG_ERR | VDL_LOG_AST | VDL_LOG_PRINT;
  alloc_initialize (&(vdl->alloc));
  vdl->bind_now = 0; // by default, do lazy binding
  vdl->contexts = 0;

  // populate search dirs from system directories
  vdl->search_dirs = vdl_str_list_append (vdl->search_dirs, 
					  get_system_search_dirs ());
  vdl->tls_gen = 1;
  vdl->tls_static_size = 0;
  vdl->tls_static_align = 0;
  vdl->tls_n_dtv = 0;
}

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

struct VdlContext *vdl_context_new (int argc, const char **argv, const char **envp)
{
  VDL_LOG_FUNCTION ("argc=%d", argc);

  struct VdlContext *context = vdl_new (struct VdlContext);
  context->global_scope = 0;
  // prepend to context list.
  if (g_vdl.contexts != 0)
    {
      g_vdl.contexts->prev = context;
    }
  context->next = g_vdl.contexts;
  context->prev = 0;
  g_vdl.contexts = context;
  // store argc safely
  context->argc = argc;
  // create a private copy of argv
  context->argv = vdl_malloc (sizeof (char*)*(argc+1));
  int i;
  for (i = 0; i < argc; i++)
    {
      context->argv[i] = vdl_strdup (argv[i]);
    }
  context->argv[argc] = 0;
  // calculate size of envp
  i = 0;
  while (1)
    {
      if (envp[i] == 0)
	{
	  break;
	}
      i++;
    }
  // create a private copy of envp
  context->envp = vdl_malloc (sizeof (char *)*i);
  context->envp[i] = 0;
  i = 0;
  while (1)
    {
      if (envp[i] == 0)
	{
	  break;
	}
      context->envp[i] = vdl_strdup (envp[i]);
      i++;
    }
  return context;
}
static void vdl_context_delete (struct VdlContext *context)
{
  // get rid of associated global scope
  vdl_file_list_free (context->global_scope);
  context->global_scope = 0;
  // unlink from main context list
  if (context->prev != 0)
    {
      context->prev->next = context->next;
    }
  if (context->next != 0)
    {
      context->next->prev = context->prev;
    }
  context->prev = 0;
  context->next = 0;
  // delete argv
  int i;
  for (i = 0; i < context->argc; i++)
    {
      vdl_free (context->argv[i], vdl_strlen (context->argv[i])+1);
    }
  vdl_free (context->argv, sizeof (char *)*context->argc);
  // delete envp
  char **cur;
  for (cur = context->envp, i = 0; *cur != 0; cur++, i++)
    {
      vdl_free (*cur, vdl_strlen (*cur)+1);
    }
  vdl_free (context->envp, sizeof(char *)*i);
  vdl_delete (context);
}

static void
append_file (struct VdlFile *item)
{
  VDL_LOG_FUNCTION ("item=%p", item);
  if (g_vdl.link_map == 0)
    {
      g_vdl.link_map = item;
      return;
    }
  struct VdlFile *cur = g_vdl.link_map;
  while (cur->next != 0)
    {
      cur = cur->next;
    }
  cur->next = item;
  item->prev = cur;
  item->next = 0;
}
struct VdlFile *vdl_file_new (unsigned long load_base,
				 const struct VdlFileInfo *info,
				 const char *filename, 
				 const char *name,
				 struct VdlContext *context)
{
  struct VdlFile *file = vdl_new (struct VdlFile);

  file->load_base = load_base;
  file->filename = vdl_strdup (filename);
  file->dynamic = info->dynamic + load_base;
  file->next = 0;
  file->prev = 0;
  file->count = 1;
  file->context = context;
  file->st_dev = 0;
  file->st_ino = 0;
  file->ro_start = load_base + info->ro_start;
  file->ro_size = info->ro_size;
  file->rw_start = load_base + info->rw_start;
  file->rw_size = info->rw_size;
  file->zero_start = load_base + info->zero_start;
  file->zero_size = info->zero_size;
  file->deps_initialized = 0;
  file->tls_initialized = 0;
  file->init_called = 0;
  file->fini_called = 0;
  file->reloced = 0;
  file->patched = 0;
  file->is_executable = 0;
  file->local_scope = 0;
  file->deps = 0;
  file->name = vdl_strdup (name);

  append_file (file);

  return file;
}

static void vdl_file_ref (struct VdlFile *file)
{
  file->count++;
}
static void vdl_file_unref (struct VdlFile *file)
{
  file->count--;
  if (file->count == 0)
    {
      vdl_file_list_free (file->deps);
      file->deps = 0;
      // remove file from global link map
      // and count number of files in the same context
      uint32_t context_count = 0;
      struct VdlFile *cur;
      for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
	{
	  if (cur->context == file->context)
	    {
	      context_count++;
	    }
	  if (cur == file)
	    {
	      cur->prev->next = cur->next;
	      cur->next->prev = cur->prev;
	      cur->next = 0;
	      cur->prev = 0;
	    }
	}
      if (context_count <= 1)
	{
	  vdl_context_delete (file->context);
	}
      vdl_delete (file);
    }
}


///////////////////////////////////////////////////////////
// Helper functions
///////////////////////////////////////////////////////////

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

