#include "vdl-gc.h"
#include "vdl-utils.h"
#include "vdl-file-list.h"
#include "vdl.h"
#include "vdl-log.h"

// return the white subset of the input set of files
static struct VdlFileList *vdl_gc_get_white (struct VdlFileList *list)
{
  struct VdlFileList *grey = 0;

  // perform the initial sweep: mark all objects as white
  // except for the roots which are marked as grey and keep
  // track of all roots.
  {
    struct VdlFileList *cur;
    for (cur = list; cur != 0; cur = cur->next)
      {
	struct VdlFile *item = cur->item;
	if (item->count > 0)
	  {
	    item->gc_color = VDL_GC_GREY;
	    grey = vdl_file_list_prepend_one (grey, item);
	  }
	else
	  {
	    item->gc_color = VDL_GC_WHITE;
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
	  struct VdlFile *item = cur->item;
	  if (item->gc_color == VDL_GC_WHITE)
	    {
	      // move referenced objects which are white to the grey list.
	      // by inserting them at the front of the list.
	      item->gc_color = VDL_GC_GREY;
	      grey = vdl_file_list_prepend_one (grey, item);
	    }
	}
      for (cur = first->item->deps; cur != 0; cur = cur->next)
	{
	  struct VdlFile *item = cur->item;
	  if (item->gc_color == VDL_GC_WHITE)
	    {
	      // move referenced objects which are white to the grey list.
	      // by inserting them at the front of the list.
	      item->gc_color = VDL_GC_GREY;
	      grey = vdl_file_list_prepend_one (grey, item);
	    }
	}
      // finally, mark our grey object as black.
      first->item->gc_color = VDL_GC_BLACK;
      vdl_utils_delete (first);
    }

  // finally, gather the list of white objects.
  struct VdlFileList *white = 0;
  {
    struct VdlFileList *cur;
    for (cur = list; cur != 0; cur = cur->next)
      {
	struct VdlFile *item = cur->item;
	if (item->gc_color == VDL_GC_WHITE)
	  {
	    white = vdl_file_list_prepend_one (white, item);
	  }
      }
  }

  return vdl_file_list_reverse (white);
}

struct VdlFileList *
vdl_gc_get_objects_to_unload (void)
{
  struct VdlFileList *all_free = 0;
  struct VdlFileList *global = vdl_file_list_get_global_linkmap ();
  struct VdlFileList *free = vdl_gc_get_white (global);
  while (free != 0)
    {
      struct VdlFileList *cur;
      for (cur = free; cur != 0; cur = cur->next)
	{
	  // now, we remove that file from the global list to ensure
	  // that the next call to vdl_gc_get_white won't return it again
	  global = vdl_file_list_free_one (global, cur->item);
	}
      all_free = vdl_file_list_append (all_free, free);

      // Now, try to see if some of the deps will have to be unloaded
      free = vdl_gc_get_white (global);
    }
  vdl_file_list_free (global);
  return all_free;
}

