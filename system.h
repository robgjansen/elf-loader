#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/unistd.h>
#include <asm/ldt.h>


void *system_mmap(void *start, size_t length, int prot, int flags, int fd, off_t offset);
int system_munmap (uint8_t *start, size_t size);
int system_mprotect (const void *addr, size_t len, int prot);
void system_write (int fd, const void *buf, size_t size);
int system_open_ro (const char *file);
int system_read (int fd, void *buffer, size_t to_read);
int system_lseek (int fd, off_t offset, int whence);
int system_fstat (const char *file, struct stat *buf);
void system_close (int fd);
void system_exit (int status);
int system_set_thread_area (struct user_desc *u_info);
int system_getpagesize (void);

#endif /* SYSTEM_H */
