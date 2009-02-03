#ifndef STAGE2_H
#define STAGE2_H

#include <elf.h>
#include <link.h>

struct Stage2Input
{
  unsigned long interpreter_load_base;
  ElfW(Phdr) *program_phdr;
  unsigned long program_phnum;
  unsigned long sysinfo;
  int program_argc;
  const char **program_argv;
  const char **program_envp;
};

struct Stage2Output
{
  unsigned long entry_point;
  int n_argv_skipped;
};



struct Stage2Output stage2 (struct Stage2Input input);

#endif /* STAGE2_H */
