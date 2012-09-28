#include "test.h"
#include <dlfcn.h>
LIB(test23);
int main (int argc, char *argv[])
{
  void *h = dlopen ("libm.so.6", RTLD_LAZY);
  dlclose (h);
  return 0;
}
