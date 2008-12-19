#include "machine.h"
#include "mdl-elf.h"

void machine_perform_relocation (struct MappedFile *file,
				 ElfW(Rel) *rel,
				 const char *symbol_name)
{
  MDL_LOG_FUNCTION ("file=%s, symbol_name=%s, off=0x%x, type=0x%x", 
		    file->name, symbol_name, rel->r_offset,
		    ELFW_R_TYPE (rel->r_info));
  unsigned long type = ELFW_R_TYPE (rel->r_info);
  unsigned long *reloc_addr = (unsigned long*) (rel->r_offset + file->load_base);

  if (type == R_386_JMP_SLOT || type == R_386_GLOB_DAT)
    {
      unsigned long addr = mdl_elf_symbol_lookup (symbol_name, file);
      if (addr == 0)
	{
	  MDL_LOG_ERROR ("Bwaah: could not resolve %s in %s\n", 
			 symbol_name, file->name);
	  // if the symbol resolution has failed, it's
	  // not a big deal because we might never call
	  // this function so, we ignore the error for now
	  return;
	}
      // apply the address to the relocation
      *reloc_addr = addr;
    }
  else if (type == R_386_RELATIVE)
    {
      *reloc_addr += file->load_base;
    }
  else if (type == R_386_32)
    {
      *reloc_addr += file->load_base;
    }
  else
    {
      MDL_LOG_ERROR ("Bwaaah: unhandled reloc type=0x%x\n", type);
    }
}


