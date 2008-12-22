#include "system.h"
#include "syscall.h"
#include <sys/mman.h>
#include <fcntl.h>

/* The magic checks below for -256 are probably misterious to non-kernel programmers:
 * they come from the fact that we call the raw system calls, not the libc wrappers
 * here so, we get the kernel return value which does not give us errno so, the
 * error number is multiplexed with the return value of the system call itself.
 * In practice, since there are less than 256 errnos defined (on my system, 131), 
 * the kernel returns -errno to indicate an error, the expected value otherwise.
 */

void *system_mmap(void *start, size_t length, int prot, int flags, int fd, off_t offset)
{
  unsigned long status = SYSCALL6(mmap2, start, length, prot, flags, fd, offset / 4096);
  if (status < 0 && status > -256)
    {
      return MAP_FAILED;
    }
  return (void*)status;
}
uint8_t *system_mmap_anon (size_t size)
{
  unsigned long start = SYSCALL6(mmap2, 0, size, PROT_READ | PROT_WRITE, 
				 MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
  if (start < 0 && start > -256)
    {
      return MAP_FAILED;
    }
  return (uint8_t *)start;
}
uint8_t *system_mmap_anon_with_position (unsigned long position, size_t size)
{
  unsigned long start = SYSCALL6(mmap2, position, size, PROT_READ | PROT_WRITE, 
				 MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
  if (start < 0 && start > -256)
    {
      return MAP_FAILED;
    }
  return (uint8_t *)start;
}
void system_munmap (uint8_t *start, size_t size)
{
  SYSCALL2 (munmap, start, size);
}
int system_mprotect (const void *addr, size_t len, int prot)
{
  int status = SYSCALL3 (mprotect, addr, len, prot);
  if (status < 0 && status > -256)
    {
      return -1;
    }
  return status;
}
void system_write (int fd, const void *buf, size_t size)
{
  SYSCALL3(write,fd,buf,size);
}
int system_open_ro (const char *file)
{
  int status = SYSCALL2(open,file,O_RDONLY);
  if (status < 0 && status > -256)
    {
      return -1;
    }
  return status;
}
int system_read (int fd, void *buffer, size_t to_read)
{
  int status = SYSCALL3(read, fd, buffer, to_read);
  if (status < 0 && status > -256)
    {
      return -1;
    }
  return status;
}
int system_lseek (int fd, off_t offset, int whence)
{
  int status = SYSCALL3(lseek, fd, offset, whence);
  if (status < 0 && status > -256)
    {
      return -1;
    }
  return status;
}
int system_fstat (const char *file, struct stat *buf)
{
  int status = SYSCALL2(stat,file,buf);
  if (status < 0 && status > -256)
    {
      return -1;
    }
  return status;
}
void system_close (int fd)
{
  SYSCALL1 (close,fd);
}
void system_exit (int status)
{
  SYSCALL1 (exit, status);
}
int system_set_thread_area (struct user_desc *u_info)
{
  int status = SYSCALL1 (set_thread_area, u_info);
  if (status < 0 && status  -256)
    {
      return -1;
    }
  return 0;
}
