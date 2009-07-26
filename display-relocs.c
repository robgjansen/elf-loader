#include <elf.h>
#include <link.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#if __ELF_NATIVE_CLASS == 32
#define ELFW_R_SYM ELF32_R_SYM
#define ELFW_R_TYPE ELF32_R_TYPE
#define ELFW_ST_BIND(val) ELF32_ST_BIND(val)
#define ELFW_ST_TYPE(val) ELF32_ST_TYPE(val)
#define ELFW_ST_INFO(bind, type) ELF32_ST_INFO(bind,type)
#else
#define ELFW_R_SYM ELF64_R_SYM
#define ELFW_R_TYPE ELF64_R_TYPE
#define ELFW_ST_BIND(val) ELF64_ST_BIND(val)
#define ELFW_ST_TYPE(val) ELF64_ST_TYPE(val)
#define ELFW_ST_INFO(bind, type) ELF64_ST_INFO(bind,type)
#endif

static const char *
type_to_str (unsigned long type)
{
#define ITEM(x)					\
  case R_##x:					\
    return "R_" #x ;				\
  break
  switch (type) {
    ITEM(X86_64_NONE);
    ITEM(X86_64_64);
    ITEM(X86_64_PC32);
    ITEM(X86_64_GOT32);
    ITEM(X86_64_PLT32);
    ITEM(X86_64_COPY);
    ITEM(X86_64_GLOB_DAT);
    ITEM(X86_64_JUMP_SLOT);
    ITEM(X86_64_RELATIVE);
    ITEM(X86_64_GOTPCREL);
    ITEM(X86_64_32);
    ITEM(X86_64_32S);
    ITEM(X86_64_16);
    ITEM(X86_64_PC16);
    ITEM(X86_64_8);
    ITEM(X86_64_PC8);
    ITEM(X86_64_DTPMOD64);
    ITEM(X86_64_DTPOFF64);
    ITEM(X86_64_TPOFF64);
    ITEM(X86_64_TLSGD);
    ITEM(X86_64_TLSLD);
    ITEM(X86_64_DTPOFF32);
    ITEM(X86_64_GOTTPOFF);
    ITEM(X86_64_TPOFF32);
    ITEM(X86_64_PC64);
    ITEM(X86_64_GOTOFF64);
    ITEM(X86_64_GOTPC32);
  default:
    return "XXX";
  }
}

int main (int argc, char *argv[])
{
  const char *filename = argv[1];
  int fd = open (filename, O_RDONLY);
  struct stat buf;
  fstat (fd, &buf);
  unsigned long file = (unsigned long)mmap (0, buf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (file == (unsigned long)MAP_FAILED)
    {
      exit (1);
    }
  ElfW(Ehdr) *header = (ElfW(Ehdr) *)file;
  ElfW(Shdr) *sh = (ElfW(Shdr)*)(file + header->e_shoff);
  int i;
  for (i = 0; i < header->e_shnum; i++)
    {
      
      if (sh[i].sh_type == SHT_RELA)
	{
	  ElfW(Rela) *rela = (ElfW(Rela)*) (file + sh[i].sh_offset);
	  unsigned long n_rela = sh[i].sh_size / sh[i].sh_entsize;
	  int j;
	  for (j = 0; j < n_rela; j++)
	    {
	      printf ("i=%d r_offset=0x%lx sym=0x%lx type=0x%lx/%s r_addend=0x%lx\n", 
		      j, rela->r_offset, ELFW_R_SYM (rela->r_info),
		      ELFW_R_TYPE (rela->r_info), type_to_str (ELFW_R_TYPE (rela->r_info)),
		      rela->r_addend);
	      rela++;
	    }
	}
    }
  return 0;
}
