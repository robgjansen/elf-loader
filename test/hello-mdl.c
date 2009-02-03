#include <dlfcn.h>

int main (int argc, char *argv[])
{
  void *h = dlopen ("libtest.so", 0);
  dlclose (h);
  return 0;
}
