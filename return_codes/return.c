#include <signal.h>
#include <unistd.h>

int main() {
  for (int i = 1; i <= 64; ++i) {
    if (!fork()) {
      kill(getpid(), i);
      _exit(-1);
    }
  }
  return 0;
}
