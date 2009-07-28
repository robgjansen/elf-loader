#define _GNU_SOURCE
#include "test.h"
#include <stdlib.h>
#include <dlfcn.h>
#include <stdio.h>
LIB(test13)

void function_f (void)
{
  printf ("called function_f in main\n");
}

int main (int argc, char *argv[])
{
  function_f ();
  void (*fct_f)(void) = (void (*)(void)) dlsym (RTLD_DEFAULT, "function_f");
  fct_f ();
  fct_f = (void (*)(void)) dlsym (RTLD_NEXT, "function_f");
  fct_f ();
  return 0;
}
