#include "machine.h"
#include "mdl-elf.h"
#include "mdl.h"
#include <sys/mman.h>


void machine_perform_relocation (struct MappedFile *file,
				 ElfW(Rel) *rel,
				 const char *symbol_name)
{
  MDL_LOG_FUNCTION ("file=%s, symbol_name=%s, off=0x%x, type=0x%x", 
		    file->name, (symbol_name != 0)?symbol_name:"", rel->r_offset,
		    ELFW_R_TYPE (rel->r_info));
  unsigned long type = ELFW_R_TYPE (rel->r_info);
  unsigned long *reloc_addr = (unsigned long*) (rel->r_offset + file->load_base);

  if (type == R_386_JMP_SLOT || 
      type == R_386_GLOB_DAT ||
      type == R_386_32)
    {
      unsigned long addr = mdl_elf_symbol_lookup (symbol_name, file);
      if (addr == 0)
	{
	  MDL_LOG_SYMBOL_FAIL (symbol_name, file);
	  // if the symbol resolution has failed, it's
	  // not a big deal because we might never call
	  // this function so, we ignore the error for now
	  return;
	}
      MDL_LOG_SYMBOL_OK (symbol_name, file);
      // apply the address to the relocation
      *reloc_addr = addr;
    }
  else if (type == R_386_RELATIVE)
    {
      *reloc_addr += file->load_base;
    }
  else
    {
      MDL_LOG_RELOC (rel);
    }
}
void machine_finish_tls_setup (unsigned int entry)
{
  // set_thread_area allocated an entry in the GDT and returned
  // the index associated to this entry. So, now, we associate
  // %gs with this newly-allocated entry.
  // Bits 3 to 15 indicate the entry index.
  // Bit 2 is set to 0 to indicate that we address the GDT through
  // this segment selector.
  // Bits 0 to 1 indicate the privilege level requested (here, 3,
  // is the least privileged level)
  int gs = (entry << 3) | 3;
  __asm ("movw %w0, %%gs" :: "q" (gs));
}

void machine_insert_trampoline (unsigned long from, unsigned long to)
{
  MDL_LOG_FUNCTION ("from=0x%x, to=0x%x", from, to);
  // In this code, we assume that the target symbol is bigger than
  // our jump and that none of that code is running yet so, we don't have
  // to worry about modifying a piece of code which is running already.
  unsigned long page_start = from / 4096 * 4096;
  system_mprotect ((void*)page_start, 4096, PROT_WRITE);
  signed long delta = to;
  delta -= from + 5;
  unsigned long delta_unsigned = delta;
  unsigned char *buffer = (unsigned char *)from;
  buffer[0] = 0xe9;
  buffer[1] = (delta_unsigned >> 0) & 0xff;
  buffer[2] = (delta_unsigned >> 8) & 0xff;
  buffer[3] = (delta_unsigned >> 16) & 0xff;
  buffer[4] = (delta_unsigned >> 24) & 0xff;
  system_mprotect ((void *)page_start, 4096, PROT_READ | PROT_EXEC);
}
