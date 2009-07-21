#include "test.h"
#include <pthread.h>
#include <semaphore.h>
LIB(test9)

static __thread int g_count = 0;
static sem_t g_sem_a;
static sem_t g_sem_b;

static void *thread_a (void *ctx)
{
  while (g_count < 10)
    {
      int status = sem_wait (&g_sem_a);
      if (status == -1)
	{
	  return (void*)-1;
	}
      printf ("count=%d\n", g_count);
      status = sem_post (&g_sem_b);
      if (status == -1)
	{
	  return (void*)-1;
	}
      g_count++;
    }
  return 0;
}

static void *thread_b (void *ctx)
{
  while (g_count < 10)
    {
      int status = sem_wait (&g_sem_b);
      if (status == -1)
	{
	  return (void*)-1;
	}
      printf ("count=%d\n", g_count);
      status = sem_post (&g_sem_a);
      if (status == -1)
	{
	  return (void*)-1;
	}
      g_count++;
    }
  return 0;
}


int main (int argc, char *argv[])
{
  int status = sem_init (&g_sem_a, 0, 1);
  if (status == -1)
    {
      return -1;
    }
  status = sem_init (&g_sem_b, 0, 0);
  if (status == -1)
    {
      return -1;
    }
  pthread_attr_t attr;
  status = pthread_attr_init(&attr);
  if (status == -1)
    {
      return -1;
    }
  pthread_t tha;
  status = pthread_create (&tha, &attr, &thread_a, 0);
  if (status == -1)
    {
      return -1;
    }
  pthread_t thb;
  status = pthread_create (&thb, &attr, &thread_b, 0);
  if (status == -1)
    {
      return -1;
    }
  void *retval;
  status = pthread_join (tha, &retval);
  if (status == -1 || retval != 0)
    {
      return -1;
    }
  status = pthread_join (thb, &retval);
  if (status == -1 || retval != 0)
    {
      return -1;
    }
  return 0;
}
