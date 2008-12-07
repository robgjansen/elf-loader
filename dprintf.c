#include "dprintf.h"
#include "syscall.h"
#include "avprintf-cb.h"
#include <stdarg.h>

static void avprintf_callback (char c, void *context)
{
  SYSCALL3(write,2,&c,1);
}

void dprintf (const char *str, ...)
{
  va_list list;
  va_start (list, str);
  avprintf_cb (avprintf_callback, 0, str, list);
  va_end (list);
}
