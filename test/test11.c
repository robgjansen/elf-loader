#include "test.h"
#include <dlfcn.h>
LIB(test11)

int main (int argc, char *argv[])
{
  dlopen ("libselinux.so.1", RTLD_LAZY);
  return 0;
}
