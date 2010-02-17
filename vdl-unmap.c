#include "vdl-unmap.h"
#include "vdl-context.h"
#include "vdl-file.h"
#include "vdl-utils.h"
#include "vdl-log.h"
#include "vdl-alloc.h"
#include "system.h"

static unsigned long 
get_total_mapping_size (struct VdlFileMap ro_map, struct VdlFileMap rw_map)
{
  unsigned long end = ro_map.mem_start_align + ro_map.mem_size_align;
  end = vdl_utils_max (end, rw_map.mem_start_align + rw_map.mem_size_align);
  unsigned long mapping_size = end - ro_map.mem_start_align;
  return mapping_size;
}

static void
file_delete (struct VdlFile *file, bool mapping)
{
  vdl_context_remove_file (file->context, file);

  if (mapping)
    {
      unsigned long mapping_size = get_total_mapping_size (file->ro_map, file->rw_map);
      int status = system_munmap ((void*)file->ro_map.mem_start_align, mapping_size);
      if (status == -1)
	{
	  VDL_LOG_ERROR ("unable to unmap \"%s\"\n", file->filename);
	}
    }

  if (vdl_context_empty (file->context))
    {
      vdl_context_delete (file->context);
    }

  vdl_list_delete (file->deps);
  vdl_list_delete (file->local_scope);
  vdl_list_delete (file->gc_symbols_resolved_in);
  vdl_alloc_free (file->name);
  vdl_alloc_free (file->filename);

  file->deps = 0;
  file->local_scope = 0;
  file->gc_symbols_resolved_in = 0;
  file->name = 0;
  file->filename = 0;
  file->context = 0;

  vdl_alloc_delete (file);
}

void vdl_unmap (struct VdlList *files, bool mapping)
{
  void **i;
  for (i = vdl_list_begin (files); i != vdl_list_end (files); i = vdl_list_next (i))
    {
      file_delete (*i, mapping);
    }
}

