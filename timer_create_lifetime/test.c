#define _POSIX_C_SOURCE 199309L
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void handle_signal(int signal) {
  if (signal == SIGUSR1) {
    printf("Timer expired\n");
  }
}

int main() {
  // Set up the signal handler
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = &handle_signal;
  sigaction(SIGUSR1, &sa, NULL);

  // Create the timer
  timer_t timerID;
  struct sigevent se;
  memset(&se, 0, sizeof(se));
  se.sigev_notify = SIGEV_SIGNAL;
  se.sigev_signo = SIGUSR1;
  if (timer_create(CLOCK_REALTIME, &se, &timerID) == -1) {
    perror("timer_create");
    return 1;
  }

  // Now fork
  if (!fork()) {
    // Try to delete the timer in the child.  This will leak.
    if (timer_delete(timerID) == -1) {
      printf("Couldn't delete timer\n"); fflush(stdout);
      return 1;
    }
    return 0;
  }

  // Start the timer
  struct itimerspec ts;
  ts.it_value.tv_sec = 1; // First expiration at 1 second
  ts.it_value.tv_nsec = 0;
  ts.it_interval.tv_sec = 2; // And every 2 seconds after that
  ts.it_interval.tv_nsec = 0;
  if (timer_settime(timerID, 0, &ts, NULL) == -1) {
    perror("timer_settime");
    return 1;
  }

  // This doesn't actually work, but whatever
  printf("I sleep\n");
  sleep(10);
  printf("I wake\n");

  // Delete the timer in parent
  if (timer_delete(timerID) == -1) {
    perror("timer_delete");
    return 1;
  }

  return 0;
}
