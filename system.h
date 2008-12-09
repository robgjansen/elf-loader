#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>
#include <sys/types.h>

uint8_t *system_mmap_anon (size_t size);
uint8_t *system_mmap_anon_with_position (unsigned long position, size_t size);
void system_munmap (uint8_t *start, size_t size);
void system_write (int fd, const void *buf, size_t size);

#endif /* SYSTEM_H */
