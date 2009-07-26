#include "test.h"
#include <dlfcn.h>
LIB(test11)

int main (int argc, char *argv[])
{
  void *p = malloc (10);
  free (p);
  return 0;
}
