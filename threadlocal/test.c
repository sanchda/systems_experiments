//usr/bin/gcc "$0" -lpthread && exec ./a.out
// Just execute this file and it should execute.  I'm too lazy to print procfs.
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>

void helperfun() {
  static __thread char a = 0;
  return;
}

void *worker(void *arg) {
  helperfun();
}

#define NUM_THREADS 10000
int main() {
  printf("Doing a run and joining %d threads.  `grep -i rss /proc/%d/status`\n", NUM_THREADS, getpid());
  pthread_t tids[NUM_THREADS] = {0};
  for(int i=0; i<NUM_THREADS; i++) pthread_create(&tids[i], NULL, worker, NULL);
  for(int i=0; i<NUM_THREADS; i++) pthread_join(tids[i], NULL);
  char buf[100] = {0};
  printf("Hit enter to continue\n");
  read(0, buf, 1);

  printf("Doing a run and NOT joining %d threads.  `grep -i rss /proc/%d/status`\n", NUM_THREADS, getpid());
  for(int i=0; i<NUM_THREADS; i++) pthread_create(&tids[i], NULL, worker, NULL);
  printf("Hit enter to continue\n");
  read(0, buf, 1);
  return 0;
}
