#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#define NUM_THREADS 10
long histogram[NUM_THREADS+1] = {0};  // Where to store signal-hits
long work_histo[NUM_THREADS+1] = {0}; // Histogram of work done
__thread int my_id = 0;               // unique ID for each thread
int total_hits = 0;                   // Total count of hits; should be safe
                                      // unless the frequency becomes insane

// Designates a thread which is a heavy CPU consumer
void* cpu_runner(void* arg) {
  my_id = 1+*(int*)arg;
  int x = 1023940123;
  while(1) {
    if (x & 1) x = x*3 + 1;
    else       x = x/2;
    if (!x)    x = 0x987abcf;
    work_histo[my_id]++;
  }
}

// Off-CPU thread
void* sleep_runner(void* arg) {
  my_id = 1+*(int*)arg;
  while(1) {
    usleep(1000);
    work_histo[my_id]++;
  }
}

// SIGPROF handler to update the histogram
void handler(int s) {
  histogram[my_id]++;
  total_hits++;
}

int main(int c, char** v) {
  pthread_t tid = {0};
  char mode = (c > 1) ? *v[1] : 'A';

  switch(mode) {
  default:
  case 'A':    // All CPU test
    printf("Running all-CPU test.\n");
    for (int i=0; i<NUM_THREADS; i++)
      pthread_create(&tid, NULL, cpu_runner, &i);
    break;
  case 'B':    // All sleep test
    printf("Running all-sleep test.\n");
    for (int i=0; i<NUM_THREADS; i++)
      pthread_create(&tid, NULL, sleep_runner, &i);
    break;
  case 'C':    // Alternating
    printf("Running mixed test.\n");
    for (int i=0; i<NUM_THREADS; i++)
      pthread_create(&tid, NULL, (i&1) ? cpu_runner : sleep_runner, &i);
    break;
  }

#define USE_TC
  struct sigevent sev = (struct sigevent){.sigev_notify = SIGEV_SIGNAL, .sigev_signo = SIG, .sigev_value = (union sigval){.sival_ptr = &tmid}}

#else
  struct itimerval old;
  setitimer(ITIMER_PROF, &(struct itimerval){.it_interval = {0,1000*100},
                                             .it_value = {1,0}}, &old);
  signal(SIGPROF, handler);
  usleep(1000*1000*100);
  setitimer(ITIMER_PROF, &old, NULL);
#endif

  printf("Printing histogram.  Note ID=0 is the parent.\n");
  for (int i=0; i<NUM_THREADS; i++) {
    printf("%d -> %ld --> %ld\n", i, histogram[i], work_histo[i]);
  }
}
