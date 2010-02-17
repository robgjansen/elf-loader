#include "vdl-linkmap.h"
#include "vdl-list.h"
#include "vdl.h"
#include "vdl-log.h"

void vdl_linkmap_append (struct VdlFile *file)
{
  if (g_vdl.link_map == 0)
    {
      g_vdl.link_map = file;
      return;
    }
  struct VdlFile *cur = g_vdl.link_map;
  while (cur->next != 0 && cur != file)
    {
      cur = cur->next;
    }
  if (cur == file)
    {
      return;
    }
  cur->next = file;
  file->prev = cur;
  file->next = 0;
  g_vdl.n_added++;
}
void vdl_linkmap_append_range (void **begin, void **end)
{
  void **i;
  for (i = begin; i != end; i = vdl_list_next (i))
    {
      vdl_linkmap_append (*i);
    }
}
void vdl_linkmap_remove (struct VdlFile *file)
{
  // first, remove them from the global link_map
  struct VdlFile *next = file->next;
  struct VdlFile *prev = file->prev;
  file->next = 0;
  file->prev = 0;
  if (prev == 0)
    {
      g_vdl.link_map = next;
    }
  else
    {
      prev->next = next;
    }
  if (next != 0)
    {
      next->prev = prev;
    }
  g_vdl.n_removed++;
}
void vdl_linkmap_remove_range (void **begin, void **end)
{
  void **i;
  for (i = begin; i != end; i = vdl_list_next (i))
    {
      vdl_linkmap_remove (*i);
    }
}

struct VdlList *vdl_linkmap_copy (void)
{
  struct VdlList *list = vdl_list_new ();
  struct VdlFile *cur;
  for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
    {
      vdl_list_push_back (list, cur);
    }
  return list;
}

void vdl_linkmap_print (void)
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
