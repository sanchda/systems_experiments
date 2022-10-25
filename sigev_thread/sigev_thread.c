#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* Thanks again, Kerrisk! */
static void thread_fun(union sigval) {
  printf("I am handling a signal and my TID is %d\n", gettid());
}

int main() {
  struct sigevent sev;
  struct itimerspec ts;
  timer_t t;
  ts.it_value = (struct timespec){2,0};
  ts.it_interval = (struct timespec){2,0};

  sev.sigev_notify = SIGEV_THREAD;
  sev.sigev_notify_function = thread_fun;
  sev.sigev_notify_attributes = NULL;
  sev.sigev_value.sival_ptr = &t;

  timer_create(CLOCK_REALTIME, &sev, &t);
  timer_settime(t, 0, &ts, NULL);

  sigset_t mask;
  sigprocmask(0, NULL, &mask);
  sigdelset(&mask, SIGALRM);

  char buf = '\0';
  switch(read(0, &buf, 1)) {
    case(1):
      printf("Got a successful read?\n");
      break;
    case(-1):
      if (errno == EINTR)
        printf("Whoa, eintr!\n");
      else
        printf("Not eintr...\n");
  }
}
