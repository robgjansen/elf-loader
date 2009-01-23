#include "machine.h"
#include "mdl-elf.h"
#include "mdl.h"
#include <sys/mman.h>


void machine_perform_relocation (const struct MappedFile *file,
				 const ElfW(Rel) *rel,
				 const ElfW(Sym) *sym,
				 const char *symbol_name)
{
  MDL_LOG_FUNCTION ("file=%s, symbol_name=%s, off=0x%x, type=0x%x", 
		    file->name, (symbol_name != 0)?symbol_name:"", 
		    rel->r_offset,
		    ELFW_R_TYPE (rel->r_info));
  unsigned long type = ELFW_R_TYPE (rel->r_info);
  unsigned long *reloc_addr = (unsigned long*) (rel->r_offset + file->load_base);

  if (type == R_386_JMP_SLOT || 
      type == R_386_GLOB_DAT ||
      type == R_386_32)
    {
      struct SymbolMatch match;
      if (!mdl_elf_symbol_lookup (symbol_name, file, &match))
	{
	  MDL_LOG_SYMBOL_FAIL (symbol_name, file);
	  // if the symbol resolution has failed, it's could
	  // be that it's not a big deal.
	  return;
	}
      MDL_LOG_SYMBOL_OK (symbol_name, file, match.file);
      // apply the address to the relocation
      *reloc_addr = match.file->load_base + match.symbol->st_value;
    }
  else if (type == R_386_RELATIVE)
    {
      *reloc_addr += file->load_base;
    }
  else if (type == R_386_COPY)
    {
      struct SymbolMatch match;
      if (!mdl_elf_symbol_lookup (symbol_name, file, &match))
	{
	  MDL_LOG_SYMBOL_FAIL (symbol_name, file);
	  // if the symbol resolution has failed, it's could
	  // be that it's not a big deal.
	  return;
	}
      MDL_LOG_SYMBOL_OK (symbol_name, file, match.file);
      MDL_ASSERT (match.symbol->st_size == sym->st_size,
		  "Symbols don't have the same size: likely a recipe for disaster.");
      mdl_memcpy (reloc_addr, 
		  (void*)(match.file->load_base + match.symbol->st_value),
		  match.symbol->st_size);
    }
  else
    {
      MDL_LOG_RELOC (rel);
    }
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

struct i386_tcbhead
{
  void *tcb;
  void *dtv;
  void *self;
  int multiple_threads;
  uintptr_t sysinfo;
  uintptr_t stack_guard;
  uintptr_t pointer_guard;
  int gscope_flag;
  int private_futex;
};

// stole from readelf -wi /usr/lib/debug/libpthread.so.0
// struct pthread includes struct i386_tcbhead as its first member
#define PTHREAD_SIZE (1136)

void machine_tcb_allocate_and_set (unsigned long tcb_size)
{
  unsigned long total_size = PTHREAD_SIZE + tcb_size;
  unsigned long buffer = (unsigned long) mdl_malloc (total_size);
  mdl_memset ((void*)buffer, 0, total_size);
  struct i386_tcbhead *tcb = (struct i386_tcbhead *)(buffer + tcb_size);
  tcb->tcb = tcb;
  tcb->self = tcb; // the tcb is the first member of struct pthread
  struct user_desc desc;
  mdl_memset (&desc, 0, sizeof (desc));
  desc.entry_number = -1; // ask kernel to allocate an entry number
  desc.base_addr = buffer + tcb_size;
  desc.limit = 0xfffff; // maximum memory address in number of pages (4K) -> 4GB
  desc.seg_32bit = 1;

  desc.contents = 0;
  desc.read_exec_only = 0;
  desc.limit_in_pages = 1;
  desc.seg_not_present = 0;
  desc.useable = 1;
  
  int status = system_set_thread_area (&desc);
  MDL_ASSERT (status == 0, "Unable to set TCB");

  // set_thread_area allocated an entry in the GDT and returned
  // the index associated to this entry. So, now, we associate
  // %gs with this newly-allocated entry.
  // Bits 3 to 15 indicate the entry index.
  // Bit 2 is set to 0 to indicate that we address the GDT through
  // this segment selector.
  // Bits 0 to 1 indicate the privilege level requested (here, 3,
  // is the least privileged level)
  int gs = (desc.entry_number << 3) | 3;
  asm ("movw %w0, %%gs" :: "q" (gs));
}
void machine_tcb_set_dtv (unsigned long *dtv)
{
  struct i386_tcbhead *tcb = (struct i386_tcbhead *) machine_tcb_get ();
  tcb->dtv = (void*)dtv;
}
void machine_tcb_set_sysinfo (unsigned long sysinfo)
{
  struct i386_tcbhead *tcb = (struct i386_tcbhead *) machine_tcb_get ();
  tcb->sysinfo = sysinfo;
}
unsigned long machine_tcb_get (void)
{
  unsigned long value;
  asm ("movl %%gs:0,%0" : "=r" (value) :);
  return value;
}
unsigned long *machine_tcb_get_dtv (void)
{
  struct i386_tcbhead *tcb = (struct i386_tcbhead *) machine_tcb_get ();
  return tcb->dtv;
}
unsigned long machine_tcb_get_sysinfo (void)
{
  struct i386_tcbhead *tcb = (struct i386_tcbhead *) machine_tcb_get ();
  return tcb->sysinfo;
}

