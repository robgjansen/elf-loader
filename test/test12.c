#include "test.h"
#include <stdlib.h>
#include <dlfcn.h>
#include <stdio.h>
LIB(test12)

int main (int argc, char *argv[])
{
  void *handle = dlopen ("libj.so", RTLD_LAZY);
  if (handle == 0)
    {
      printf ("dlopen failed\n");
    }
  return 0;
}
