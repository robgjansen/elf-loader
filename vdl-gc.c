#include "vdl-gc.h"
#include "vdl-utils.h"
#include "vdl-file-list.h"
#include "vdl.h"
#include "vdl-log.h"

static void
vdl_file_remove (struct VdlFile *item)
{
  VDL_LOG_FUNCTION ("item=\"%s\"", item->name);

  // first, remove them from the global link_map
  struct VdlFile *next = item->next;
  struct VdlFile *prev = item->prev;
  item->next = 0;
  item->prev = 0;
  if (next == 0 && prev == 0)
    {
      VDL_LOG_ASSERT (item == g_vdl.link_map, "invariant broken");
      g_vdl.link_map = 0;
    }
  if (prev != 0)
    {
      prev->next = next;
    }
  if (next != 0)
    {
      next->prev = prev;
    }

  // then, remove them from the local scope map
  vdl_file_list_free_one (item->local_scope, item);

  // finally, remove them from the global scope map
  vdl_file_list_free_one (item->context->global_scope, item);
}
#if 0
static uint32_t 
vdl_context_get_count (const struct VdlContext *context)
{
  // and count number of files in this context
  uint32_t context_count = 0;
  struct VdlFile *cur;
  for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
    {
      if (cur->context == context)
	{
	  context_count++;
	}
    }
  return context_count;
}
#endif

static struct VdlFileList *vdl_gc_get_white (void)
{
  struct VdlFileList *grey = 0;

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
	}
      // finally, mark our grey object as black.
      first->item->gc_color = VDL_GC_BLACK;
      vdl_utils_delete (first);
    }

  // finally, gather the list of white objects.
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

  return vdl_file_list_reverse (white);
}

static struct VdlFileList *
vdl_gc_get_objects_to_unload (void)
{
  struct VdlFileList *all_free = 0;
  struct VdlFileList *free = vdl_gc_get_white ();
  while (free != 0)
    {
      struct VdlFileList *cur;
      for (cur = free; cur != 0; cur = cur->next)
	{
	  // we know that this file will be unloaded so,
	  // update the count of its dependencies to see if 
	  // they will have to be unloaded too.
	  struct VdlFileList *dep;
	  for (dep = cur->item->deps; dep != 0; dep = dep->next)
	    {
	      VDL_LOG_ASSERT (dep->item->count > 0, "invariant broken");
	      dep->item->count--;
	    }
	  // now, we remove that file from the global list to ensure
	  // that the next call to vdl_gc_get_white won't return it again
	  vdl_file_remove (cur->item);
	}
      all_free = vdl_file_list_append (all_free, free);

      // Now, try to see if some of the deps will have to be unloaded
      free = vdl_gc_get_white ();
    }
  return all_free;
}

void
vdl_gc (void)
{
  // first, we gather the list of all objects to unload/delete
  struct VdlFileList *unload = vdl_gc_get_objects_to_unload ();

  vdl_file_list_call_fini (unload);

  // we have to wait until all the finalizers are run to 
  // delete the files 
  {
    struct VdlFileList *cur;
    for (cur = unload; cur != 0; cur = cur->next)
      {
	vdl_file_delete (cur->item);
      }
  }

  vdl_file_list_free (unload);
}
