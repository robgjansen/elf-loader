#include "futex.h"
#include <pthread.h>
struct futex g_futex;
unsigned int g_shared_var = 0;

void *
futex_thread_a (void*)
{
  for (unsigned int i = 0; i < 100000000; i++)
    {
      futex_lock (&g_futex);
      g_shared_var++;
      futex_unlock (&g_futex);
    }
  return (void*)0;
}

void *
futex_thread_b (void*)
{
  for (unsigned int i = 100000000; i > 0; i--)
    {
      futex_lock (&g_futex);
      g_shared_var--;
      futex_unlock (&g_futex);
    }
  return (void*)0;
}

bool
test_futex(void)
{
  futex_init (&g_futex);
  pthread_t tha;
  pthread_t thb;
  pthread_create (&tha, 0, &futex_thread_a, 0);
  pthread_create (&thb, 0, &futex_thread_b, 0);
  void *reta;
  void *retb;
  pthread_join (tha, &reta);
  pthread_join (thb, &retb);
  return reta == 0 && retb == 0 && g_shared_var == 0;
}
