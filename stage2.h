#ifndef STAGE2_H
#define STAGE2_H

#include <elf.h>
#include <link.h>

struct TrampolineInformation
{
  // initialized by stage0
  unsigned long load_base;
  // initialized by stage0, modified by stage1 before 
  // returning to stage0
  unsigned long entry_point_struct;
  // set by stage2 before returning to stage1 and stage0
  unsigned long entry_point;
  // set by stage2 before returning to stage1 and stage0
  unsigned long dl_fini;
};

struct OsArgs
{
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
