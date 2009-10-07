#include "vdl-gc.h"
#include "vdl-utils.h"
#include "vdl-file-list.h"
#include "vdl.h"
#include "vdl-log.h"
#include "vdl-tls.h"

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

  // then, remove them from our local scope map
  item->local_scope = vdl_file_list_free_one (item->local_scope, item);

  // then, remove them from the local scope maps of all
  // those who have potentially a reference to us
  struct VdlFile *cur;
  for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
    {
      cur->local_scope = vdl_file_list_free_one (cur->local_scope, item);
    }  

  // finally, remove them from the global scope map
  item->context->global_scope = vdl_file_list_free_one (item->context->global_scope, item);
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
      for (cur = first->item->deps; cur != 0; cur = cur->next)
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

struct VdlFileList *
vdl_gc_get_objects_to_unload (void)
{
  struct VdlFileList *all_free = 0;
  struct VdlFileList *free = vdl_gc_get_white ();
  while (free != 0)
    {
      struct VdlFileList *cur;
      for (cur = free; cur != 0; cur = cur->next)
	{
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

