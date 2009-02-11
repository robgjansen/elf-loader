#include "vdl-gc.h"
#include "vdl-utils.h"
#include "vdl-file-list.h"
#include "vdl.h"

struct VdlFileList *vdl_gc (void)
{
  struct VdlFileList *grey;

  // perform the initial sweep: mark all objects as white
  // except for the roots which are marked as grey and keep
  // track of all roots.
  {
    struct VdlFile *cur;
    for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
      {
	if (cur->count > 0)
	  {
	    cur->gc_color = VDL_GC_GREY;
	    grey = vdl_file_list_prepend_one (grey, cur);
	  }
	else
	  {
	    cur->gc_color = VDL_GC_WHITE;
	  }
      }
  }

  // for each element in the grey list, 'blacken' it by
  // marking grey all the objects it references.
  while (grey != 0)
    {
      struct VdlFileList *first = grey;
      grey = vdl_file_list_remove (grey, grey);
      struct VdlFileList *cur;
      for (cur = first->item->gc_symbols_resolved_in; cur != 0; cur = cur->next)
	{
	  if (cur->item->gc_color == VDL_GC_WHITE)
	    {
	      // move referenced objects which are white to the grey list.
	      // by inserting them at the front of the list.
	      cur->item->gc_color = VDL_GC_GREY;
	      grey = vdl_file_list_prepend_one (grey, cur->item);
	    }
	  // finally, mark our grey object as black.
	  first->item->gc_color = VDL_GC_BLACK;
	}
      vdl_utils_delete (first);
    }

  // finally, try to gather the list of white objects.
  struct VdlFileList *white = 0;
  {
    struct VdlFile *cur;
    for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
      {
	if (cur->gc_color == VDL_GC_WHITE)
	  {
	    white = vdl_file_list_prepend_one (white, cur);
	  }
      }
  }
  return white;
}

