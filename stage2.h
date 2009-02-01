#ifndef STAGE2_H
#define STAGE2_H

#include <elf.h>
#include <link.h>

struct TrampolineInformation
{
  unsigned long entry_point_struct;
  unsigned long entry_point;
  unsigned long dl_fini;
};

struct OsArgs
{
  unsigned long interpreter_load_base;
  ElfW(Phdr) *program_phdr;
  unsigned long program_phnum;
  unsigned long sysinfo;
  int program_argc;
  const char **program_argv;
  const char **program_envp;
};

struct Stage2Result
{
  unsigned long entry_point;
  int n_argv_skipped;
};



struct Stage2Result stage2 (struct TrampolineInformation *trampoline_information,
			    struct OsArgs args);

#endif /* STAGE2_H */
