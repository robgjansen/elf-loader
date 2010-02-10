#ifndef VDL_GC_H
#define VDL_GC_H

#include "vdl-file-list.h"

/* Perform a mark and sweep garbage tri-colour collection 
 * of all VdlFile objects and returns the list of objects 
 * which can be freed. These objects are already
 * removed from all global lists so, it should be safe
 * to just delete them here
 */
struct GcResult
{
  struct VdlFileList *unload;
  struct VdlFileList *not_unload;
};
struct GcResult vdl_gc_get_objects_to_unload (void);


#endif /* VDL_GC_H */
