#include "vdl-sort.h"
#include "vdl-file-list.h"
#include "vdl-utils.h"
#include <stdint.h>

static uint32_t
get_max_depth (struct VdlFileList *files)
{
  uint32_t max_depth = 0;
  struct VdlFileList *cur;
  for (cur = files; cur != 0; cur = cur->next)
    {
      max_depth = vdl_utils_max (cur->item->depth,
				 max_depth);
    }
  return max_depth;
}

struct VdlFileList *
vdl_sort_increasing_depth (struct VdlFileList *files)
{

  uint32_t max_depth = get_max_depth (files);
  
  struct VdlFileList *output = 0;

  uint32_t i;
  for (i = 0; i <= max_depth; i++)
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

struct VdlFileList *vdl_sort_deps_breadth_first (struct VdlFile *file)
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

struct VdlFileList *vdl_sort_call_init (struct VdlFileList *files)
{
  struct VdlFileList *fini_order = vdl_sort_increasing_depth (files);
  struct VdlFileList *init_order = vdl_file_list_reverse (fini_order);
  return init_order;
}
struct VdlFileList *vdl_sort_call_fini (struct VdlFileList *files)
{
  struct VdlFileList *fini_order = vdl_sort_increasing_depth (files);
  return fini_order;
}
