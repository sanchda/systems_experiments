#include <pthread.h>
#include <stdlib.h>

void *bar(void *arg) {
  (void)arg;
  // allocates some memory
  void *p = malloc(10);

  // Does some CPU work
  int num = 10;
  while (num--) {
    int i = 0;
    for (int j = 0; j < 1000000; j++) {
      i += j;
    }
  }

  free(p);
  return NULL;
}

extern void foo() {
  // spawns two threads with pthreads in order to call bar and do some allocations
  pthread_t t1, t2;
  pthread_create(&t1, NULL, bar, NULL);
  pthread_create(&t2, NULL, bar, NULL);
  // waits for the threads to finish
  pthread_join(t1, NULL);
  pthread_join(t2, NULL);
}
