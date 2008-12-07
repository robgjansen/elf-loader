#include "syscall.h"
#include <elf.h>
#include <link.h>

#define DEBUG_CHAR(c)						\
  {								\
    char tmp_char_array[] = {(c), '\n'};			\
    SYSCALL3 (write,2,&tmp_char_array,sizeof(tmp_char_array));	\
  }
#define INT_TO_HEX(i)(((i)<10)?(0x30+(i)):(0x41+((i)-10)))
#define DEBUG_HEX(v)							\
  {									\
    char tmp_hex_array[sizeof(v)*2+3];					\
    int i;								\
    tmp_hex_array[0] = '0';						\
    tmp_hex_array[1] = 'x';						\
    for (i = 0; i < sizeof(v)*8; i+=4)					\
      {									\
	tmp_hex_array[sizeof(v)*2+1-(i/4)] = INT_TO_HEX ((((unsigned long)v)>>i)&0xf); \
      }									\
    tmp_hex_array[sizeof(v)*2+2] = '\n';				\
    SYSCALL3 (write,2,&tmp_hex_array,sizeof(tmp_hex_array));		\
  }


#define READ_INT(p)				\
  ({int v = *((int*)p);				\
    p+=sizeof(int);				\
    v;})

#define READ_POINTER(p)				\
  ({char * v = *((char**)p);			\
    p+=sizeof(char*);				\
    v;})

#if __ELF_NATIVE_CLASS == 32
#define ELF_R_SYM(val) ELF32_R_SYM(val)
#define ELF_R_TYPE(val) ELF32_R_TYPE(val)
#define ELF_R_INFO(sym,type) ELF32_R_INFO(sym,type)
#else
#define ELF_R_SYM(val) ELF64_R_SYM(val)
#define ELF_R_TYPE(val) ELF64_R_TYPE(val)
#define ELF_R_INFO(sym,type) ELF64_R_INFO(sym,type)
#endif

extern int g_test;

void _dl_start(unsigned long args)
{
  uint8_t *interpreter_load_base;
  ElfW(Dyn) *dynamic;

  // search for interpreter load base
  {
    unsigned long tmp;
    int argc;
    ElfW(auxv_t) *auxvt, *auxvt_tmp;
    tmp = (unsigned long)((&args)-1);
    argc = READ_INT (tmp); // skip argc
    tmp += sizeof(char *)*(argc+1); // skip argv
    while (READ_POINTER (tmp) != 0) {} // skip envp
    auxvt = (ElfW(auxv_t) *)tmp; // save aux vector start
    // search interpreter load base
    interpreter_load_base = 0;
    auxvt_tmp = auxvt;
    while (auxvt_tmp->a_type != AT_NULL)
      {
	if (auxvt_tmp->a_type == AT_BASE)
	  {
	    interpreter_load_base = (uint8_t *)auxvt_tmp->a_un.a_val;
	    break;
	  }
	auxvt_tmp++;
      }
    DEBUG_HEX (interpreter_load_base);
    if (interpreter_load_base == 0)
      {
	SYSCALL1 (exit, -3);
      }
  }

  // search for PT_DYNAMIC
  {
    ElfW(Half) i;
    ElfW(Ehdr) *interpreter_header = (ElfW(Ehdr) *)interpreter_load_base;
    dynamic = 0;
    DEBUG_HEX (interpreter_header->e_phnum);
    DEBUG_HEX (interpreter_header->e_phoff);
    for (i = 0; i < interpreter_header->e_phnum; i++)
      {
	ElfW(Phdr) *program_header = (ElfW(Phdr) *) 
	  (interpreter_load_base + 
	   interpreter_header->e_phoff + 
	   interpreter_header->e_phentsize * i);
	//DEBUG_HEX(program_header->p_type)
	if (program_header->p_type == PT_DYNAMIC)
	  {
	    //DEBUG_HEX(program_header->p_offset);
	    dynamic = (ElfW(Dyn)*)(interpreter_load_base + program_header->p_offset);
	    break;
	  }
      }
    if (dynamic == 0)
      {
	SYSCALL1 (exit, -4);
      }
    // we found PT_DYNAMIC
    DEBUG_HEX(dynamic);
  }
  // relocate entries in DT_REL
  {
    ElfW(Dyn) *tmp = dynamic;
    ElfW(Rel) *dt_rel = 0;
    uint32_t dt_relsz = 0;
    uint32_t dt_relent = 0;
    // search DT_REL, DT_RELSZ, DT_RELENT
    while (tmp->d_tag != DT_NULL && (dt_rel == 0 || dt_relsz == 0 || dt_relent == 0))
      {
	//DEBUG_HEX(tmp->d_tag);
	if (tmp->d_tag == DT_REL)
	  {
	    dt_rel = (ElfW(Rel) *)(interpreter_load_base + tmp->d_un.d_ptr);
	  }
	else if (tmp->d_tag == DT_RELSZ)
	  {
	    dt_relsz = tmp->d_un.d_val;
	  }
	else if (tmp->d_tag == DT_RELENT)
	  {
	    dt_relent = tmp->d_un.d_val;
	  }
	tmp++;
      }
    if (dt_rel == 0 || dt_relsz == 0 || dt_relent == 0)
      {
	SYSCALL1 (exit, -5);
      }
    DEBUG_HEX(dt_rel);
    DEBUG_HEX(dt_relsz);
    DEBUG_HEX(dt_relent);
    // relocate entries in dt_rel
    {
      uint32_t i;
      for (i = 0; i < dt_relsz; i+=dt_relent)
	{
	  ElfW(Rel) *tmp = (ElfW(Rel)*)(((uint8_t*)dt_rel) + i);
	  ElfW(Addr) *reloc_addr = (void *)(interpreter_load_base + tmp->r_offset);
	  *reloc_addr += (ElfW(Addr))interpreter_load_base;
	}
    }
  }

  

  SYSCALL1 (exit, -6);
}
