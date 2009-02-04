#include "vdl.h"
#include "alloc.h"
#include "system.h"
#include "avprintf-cb.h"
#include "vdl-utils.h"
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

void vdl_file_ref (struct VdlFile *file)
{
  file->count++;
}
void vdl_file_unref (struct VdlFile *file)
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


