#include "vdl-init-fini.h"
#include "vdl-utils.h"
#include "vdl-log.h"

// return the max depth.
static uint32_t
get_max_depth_recursive (struct VdlFileList *files, uint32_t depth)
{
  uint32_t max_depth = depth;
  struct VdlFileList *cur;
  for (cur = files; cur != 0; cur = cur->next)
    {
      cur->item->gc_depth = vdl_utils_max (depth, cur->item->gc_depth);
      max_depth = vdl_utils_max (get_max_depth_recursive (cur->item->deps, depth+1),
				 max_depth);
    }
  return max_depth;
}

static uint32_t
get_max_depth (struct VdlFileList *files)
{
  return get_max_depth_recursive (files, 1);
}


static struct VdlFileList *
sort_fini (struct VdlFileList *files)
{
  // initialize depth to zero
  {
    struct VdlFileList *cur;
    for (cur = files; cur != 0; cur = cur->next)
      {
	cur->item->gc_depth = 0;
      }
  }

  // calculate depth of each file and get the max depth
  uint32_t max_depth = get_max_depth (files);
  
  struct VdlFileList *output = 0;

  uint32_t i;
  for (i = 0; i < max_depth; i++)
    {
      // find files with matching depth and output them
      struct VdlFileList *cur;
      for (cur = files; cur != 0; cur = cur->next)
	{
	  if (cur->item->gc_depth == i)
	    {
	      output = vdl_file_list_append_one (output, cur->item);
	    }
	}
    }

  return output;
}


typedef void (*fini_function) (void);

static void
call_fini (struct VdlFile *file)
{
  VDL_LOG_FUNCTION ("file=%s", file->name);
  if (!file->init_called || file->fini_called)
    {
      // if we were never initialized properly or if
      // we are finalized already, no need to do any work
      return;
    }
  // mark the file as finalized
  file->fini_called = 1;

  // Gather information from the .dynamic section
  unsigned long dt_fini = vdl_file_get_dynamic_p (file, DT_FINI);
  unsigned long dt_fini_array = vdl_file_get_dynamic_p (file, DT_FINI_ARRAY);
  unsigned long dt_fini_arraysz = vdl_file_get_dynamic_v (file, DT_FINI_ARRAYSZ);

  // First, invoke the newer DT_FINI_ARRAY functions.
  // The address of the functions to call is stored as
  // an array of pointers pointed to by DT_FINI_ARRAY
  if (dt_fini_array != 0)
    {
      fini_function *fini = (fini_function *) dt_fini_array;
      int i;
      for (i = 0; i < dt_fini_arraysz / sizeof (fini_function); i++, fini++)
	{
	  (*(fini[i])) ();
	}
    }

  // Then, invoke the old-style DT_FINI function.
  // The address of the function to call is stored in
  // the DT_FINI tag, here: dt_fini.
  if (dt_fini != 0)
    {
      fini_function fini = (fini_function) dt_fini;
      fini ();
    }
}


// the glibc elf loader passes all 3 arguments
// to the initialization functions and the libc initializer
// function makes use of these arguments to initialize
// __libc_argc, __libc_argv, and, __environ so, we do the
// same for compatibility purposes.
typedef void (*init_function) (int, char **, char **);

static void
call_init (struct VdlFile *file)
{
  VDL_LOG_FUNCTION ("file=%s", file->name);

  if (file->init_called)
    {
      // we have already been initialized
      return;
    }
  file->init_called = 1;

  if (file->is_executable)
    {
      // The constructors of the main executable are
      // run by the libc initialization code which has
      // been linked into the binary by the compiler.
      // If we run them here, they will be run twice which
      // is not good. So, we just return.
      return;
    }

  // Gather information from the .dynamic section
  unsigned long dt_init = vdl_file_get_dynamic_p (file, DT_INIT);
  unsigned long dt_init_array = vdl_file_get_dynamic_p (file, DT_INIT_ARRAY);
  unsigned long dt_init_arraysz = vdl_file_get_dynamic_v (file, DT_INIT_ARRAYSZ);
  // First, invoke the old-style DT_INIT function.
  // The address of the function to call is stored in
  // the DT_INIT tag, here: dt_init.
  if (dt_init != 0)
    {
      init_function init = (init_function) dt_init;
      init (file->context->argc, file->context->argv, file->context->envp);
    }

  // Then, invoke the newer DT_INIT_ARRAY functions.
  // The address of the functions to call is stored as
  // an array of pointers pointed to by DT_INIT_ARRAY
  if (dt_init_array != 0)
    {
      init_function *init = (init_function *) dt_init_array;
      int i;
      for (i = 0; i < dt_init_arraysz / sizeof (init_function); i++, init++)
	{
	  (*(init[i])) (file->context->argc, file->context->argv, file->context->envp);
	}
    }
}


void vdl_init_fini_call_init (struct VdlFileList *files)
{
  struct VdlFileList *fini = sort_fini (files);
  struct VdlFileList *init = vdl_file_list_reverse (fini);

  struct VdlFileList *cur;
  for (cur = init; cur != 0; cur = cur->next)
    {
      call_init (cur->item);
    }

  vdl_file_list_free (init);
}
void vdl_init_fini_call_fini (struct VdlFileList *files)
{
  struct VdlFileList *fini = sort_fini (files);

  struct VdlFileList *cur;
  for (cur = fini; cur != 0; cur = cur->next)
    {
      call_fini (cur->item);
    }

  vdl_file_list_free (fini);
}
