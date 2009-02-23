#ifndef VDL_FILE_RELOC_H
#define VDL_FILE_RELOC_H

#include "vdl.h"
#include <elf.h>
#include <link.h>

void vdl_file_reloc (struct VdlFile *file);
// offset is in bytes, return value is reloced symbol
unsigned long vdl_file_reloc_one_plt (struct VdlFile *file, 
				      unsigned long offset);

#endif /* VDL_FILE_RELOC_H */
