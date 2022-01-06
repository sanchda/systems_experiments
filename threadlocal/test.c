//usr/bin/gcc "$0" -lpthread && exec ./a.out
// Just execute this file and it should execute.  I'm too lazy to print procfs.
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>

void helperfun() {
  static __thread char a = 0;
}

void *worker(void *arg) {
  helperfun();
}

void *lazy(void *arg) {
}

void print_stats() {
  usleep(10000);
  char proc_loc[1024] = {0};
  snprintf(proc_loc, sizeof(proc_loc), "/proc/%d/status", getpid());
  char *commands[] = {"grep", "-i", "rss", proc_loc, NULL};
  pid_t pid = fork();
  if (!pid)
    execvp(commands[0], commands);
  waitpid(pid, 0, 0);
}

#define NUM_THREADS 10000
int main() {
  printf("Doing a run and joining %d threads\n", NUM_THREADS);
  pthread_t tids[NUM_THREADS] = {0};
  for(int i=0; i<NUM_THREADS; i++) pthread_create(&tids[i], NULL, worker, NULL);
  for(int i=0; i<NUM_THREADS; i++) pthread_join(tids[i], NULL);
  print_stats();

  printf("Doing a lazy run and NOT joining %d threads\n", NUM_THREADS);
  for(int i=0; i<NUM_THREADS; i++) pthread_create(&tids[i], NULL, lazy, NULL);
  print_stats();

  printf("joining threads\n");
  for(int i=0; i<NUM_THREADS; i++) pthread_join(tids[i], NULL);
  print_stats();

  printf("Doing a lazy run and NOT joining %d threads\n", NUM_THREADS);
  for(int i=0; i<NUM_THREADS; i++) pthread_create(&tids[i], NULL, worker, NULL);
  print_stats();

  printf("joining threads\n");
  for(int i=0; i<NUM_THREADS; i++) pthread_join(tids[i], NULL);
  print_stats();
  return 0;
}
