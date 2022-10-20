#include <errno.h>
#include <stdbool.h>
#include <signal.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <x86intrin.h>

volatile bool sigflag1 = false, sigflag2 = false;

#define sigfun(n,m)                               \
void sig##n(int s) {                              \
  printf("We get signal "#n"\n");                 \
  sigflag##n = true;                              \
  unsigned long long futuretime = __rdtsc() + 1e9;\
  while (futuretime > __rdtsc())                  \
    if (sigflag##m == true) {                     \
      printf(#n " interrupt "#m"\n");             \
      sigflag##n = false;                         \
      return;                                     \
  }                                               \
  sigflag##n = false;                             \
  printf(#n" didn't interrupt anyone :(\n");  \
}

sigfun(1,2);
sigfun(2,1);

int main() {
  int oldpid = getpid();
  pid_t kidpid;
  if ((kidpid = fork())) {
    // I'm the parent, I receive signals
    signal(SIGUSR1, sig1);
    signal(SIGUSR2, sig2);
    signal(SIGUSR1, sig1); // Where does it go?
    pause();
  } else {
    // I'm the child, I send signals (wait for parent)
    usleep(1e3);
    kill(oldpid, SIGUSR1);
    usleep(1e3);
    kill(oldpid, SIGUSR2);
    usleep(1e6);
    printf("Child signing off\n");
  }
}
