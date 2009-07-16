#include "system.h"
#include "syscall.h"
#include <sys/mman.h>

void *system_mmap(void *start, size_t length, int prot, int flags, int fd, off_t offset)
{
  long int status = SYSCALL6(mmap, start, length, prot, flags, fd, offset);
  if (status < 0 && status > -4095)
    {
      return MAP_FAILED;
    }
  return (void*)status;
}
