#include <stdio.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>

// a sigsegv handler that pauses indefinitely
bool paused = true;
void sigsegv_handler(int signum) {
    printf("SIGSEGV received\n");
    while (paused) { }
    printf("Continuing after SIGSEGV\n");
}

// a sigprof handler that just prints to the screen
void sigprof_handler(int signum) {
    printf("SIGPROF received\n");
    paused = false;
}

int main() {
  // Sets the signal handler for sigsegv
  signal(SIGSEGV, sigsegv_handler);

  // Sets the signal handler for sigprof
  signal(SIGPROF, sigprof_handler);

  // Set up the timer for 5 seconds
  struct itimerval timer;
  timer.it_value.tv_sec = 5;
  timer.it_value.tv_usec = 0;
  timer.it_interval.tv_sec = 0;
  timer.it_interval.tv_usec = 0;

  // Start the timer
  setitimer(ITIMER_PROF, &timer, NULL);

  // Now do a segfault
  int *p = 0;
  *p = 0;

  return 0;
}
