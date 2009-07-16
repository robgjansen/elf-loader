#include "system.h"
#include "syscall.h"
#include <sys/mman.h>

void *system_mmap(void *start, size_t length, int prot, int flags, int fd, off_t offset)
{
  int status = SYSCALL6(mmap2, start, length, prot, flags, fd, offset / 4096);
  if (status < 0 && status > -256)
    {
      return MAP_FAILED;
    }
  return (void*)status;
}
