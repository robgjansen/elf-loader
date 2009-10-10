#ifndef VDL_RELOC_H
#define VDL_RELOC_H

#include "vdl.h"
#include <elf.h>
#include <link.h>

void vdl_reloc (struct VdlFileList *list, int now);
// offset is in bytes, return value is reloced symbol
// called from machine_resolve_trampoline 
unsigned long vdl_reloc_one_plt (struct VdlFile *file, 
				 unsigned long offset);

#endif /* VDL_RELOC_H */
