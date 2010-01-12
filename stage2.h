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
  char **program_argv;
  char **program_envp;
};

struct Stage2Output
{
  unsigned long entry_point;
  int n_argv_skipped;
};

struct Stage2Output stage2_initialize (struct Stage2Input input);

void stage2_finalize (void);

#endif /* STAGE2_H */
