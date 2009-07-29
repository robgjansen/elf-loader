#include "vdl-sort.h"
#include "vdl-file-list.h"
#include "vdl-utils.h"
#include <stdint.h>


// return the max depth.
static uint32_t
get_max_depth_recursive (struct VdlFileList *files, uint32_t depth)
{
  uint32_t max_depth = depth;
  struct VdlFileList *cur;
  for (cur = files; cur != 0; cur = cur->next)
    {
      cur->item->depth = vdl_utils_max (depth, cur->item->depth);
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

struct VdlFileList *
vdl_sort_deps_breadth_first (struct VdlFileList *files)
{
  // initialize depth to zero
  {
    struct VdlFileList *cur;
    for (cur = files; cur != 0; cur = cur->next)
      {
	cur->item->depth = 0;
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
	  if (cur->item->depth == i)
	    {
	      output = vdl_file_list_append_one (output, cur->item);
	    }
	}
    }

  return output;
}

static struct VdlFileList *
get_deps_recursive (struct VdlFile *file)
{
  struct VdlFileList *list = 0;
  list = vdl_file_list_append_one (list, file);

  struct VdlFileList *cur = 0;
  for (cur = list; cur != 0; cur = cur->next)
    {
      struct VdlFileList *dep;
      for (dep = cur->item->deps; dep != 0; dep = dep->next)
	{
	  if (vdl_file_list_find (list, dep->item) == 0)
	    {
	      // not found
	      list = vdl_file_list_append_one (list, dep->item);
	    }
	}
    }

  return list;
}

struct VdlFileList *vdl_sort_deps_breadth_first_one (struct VdlFile *file)
{
  struct VdlFileList *deps = get_deps_recursive (file);

  struct VdlFileList *output = vdl_sort_deps_breadth_first (deps);

  vdl_file_list_free (deps);

  return output;
}

struct VdlFileList *vdl_sort_call_init (struct VdlFileList *files)
{
  struct VdlFileList *fini_order = vdl_sort_deps_breadth_first (files);
  struct VdlFileList *init_order = vdl_file_list_reverse (fini_order);
  return init_order;
}
struct VdlFileList *vdl_sort_call_fini (struct VdlFileList *files)
{
  struct VdlFileList *fini_order = vdl_sort_deps_breadth_first (files);
  return fini_order;
}
