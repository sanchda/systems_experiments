#include <stdio.h>
#include <sys/time.h>
#include <time.h>


int main() {
  struct timeval tv;
  struct timespec ts;
  time_t t;
  int ret;

  for (int i = 0; i < 1e8; i++) {
    // gettimeofday()
    ret = gettimeofday(&tv, NULL);

    // time()
    t = time(NULL);


    // clock_gettime(CLOCK_REALTIME,...
    ret = clock_gettime(CLOCK_REALTIME, &ts);
  }

  return 0;
}
