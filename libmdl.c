#define EXPORT __attribute__ ((visibility("default")))

EXPORT void *dlopen(const char *filename, int flag)
{
  return 0;
}

EXPORT char *dlerror(void)
{
  return 0;
}

EXPORT void *dlsym(void *handle, const char *symbol)
{
  return 0;
}

EXPORT int dlclose(void *handle)
{
  return 0;
}
