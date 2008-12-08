#include "../system.h"
#include <sys/mman.h>

uint8_t *system_mmap_anon (size_t size)
{
  void *start = mmap (0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
  return start;
}

void system_munmap (uint8_t *start, size_t size)
{
  munmap (start, size);
}
