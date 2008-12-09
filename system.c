#include "system.h"
#include "syscall.h"
#include <sys/mman.h>

uint8_t *system_mmap_anon (size_t size)
{
  unsigned long start = SYSCALL6(mmap2, 0, size, PROT_READ | PROT_WRITE, 
				 MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
  return (uint8_t *)start;
}
uint8_t *system_mmap_anon_with_position (unsigned long position, size_t size)
{
  unsigned long start = SYSCALL6(mmap2, position, size, PROT_READ | PROT_WRITE, 
				 MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
  return (uint8_t *)start;
}
void system_munmap (uint8_t *start, size_t size)
{
  SYSCALL2 (munmap, start, size);
}
void system_write (int fd, const void *buf, size_t size)
{
  SYSCALL3(write,fd,buf,size);
}
