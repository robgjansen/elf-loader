#include "test.h"

LIB(f)

extern void function_e (void);

void function_e (void)
{
  printf ("called function_e in libf.so\n");
}

void function_f_e (void)
{
  function_e ();
}
