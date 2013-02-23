#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <semaphore.h>
#include "test.h"
LIB(test25)

__thread int g_a = 0;

static void *thread (void*ctx)
{
  printf("a=%d\n", g_a);
  g_a = 10;
  return 0;
}

int main (int argc, char *argv[])
{
  int i;
  printf ("enter main\n");

  for (i = 0; i <100; i++)
    {
      pthread_attr_t attr;
      pthread_attr_init (&attr);
      pthread_t th;
      void *retval;
      g_a = 2;
      pthread_create (&th, &attr, thread, 0);

      pthread_join(th, &retval);
      printf ("main a=%d\n", g_a);
    }

  printf ("leave main\n");
  return 0;
}
