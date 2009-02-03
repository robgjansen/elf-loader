#include <dlfcn.h>

int main (int argc, char *argv[])
{
  void *h = dlopen ("libtest.so", RTLD_LAZY);
  dlclose (h);
  return 0;
}
