#include "vdl-unmap.h"
#include "vdl-context.h"
#include "vdl-file.h"
#include "vdl-utils.h"
#include "vdl-log.h"
#include "vdl-alloc.h"
#include "system.h"


static void
file_delete (struct VdlFile *file, bool mapping)
{
  vdl_context_remove_file (file->context, file);

  if (mapping)
    {
      int status = system_munmap ((void*)file->ro_map.mem_start_align, 
				  file->ro_map.mem_size_align);
      if (status == -1)
	{
	  VDL_LOG_ERROR ("unable to unmap ro map for \"%s\"\n", file->filename);
	}
      status = system_munmap ((void*)file->rw_map.mem_start_align, 
			      file->rw_map.mem_size_align);
      if (status == -1)
	{
	  VDL_LOG_ERROR ("unable to unmap rw map for \"%s\"\n", file->filename);
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

