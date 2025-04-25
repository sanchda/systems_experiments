#include <stdio.h>
#include <unistd.h>

#define MAX_CHILDREN 100

extern pid_t make_zombie();
extern char get_status(pid_t pid);

int main() {
  // Make 100 zombie processes then wait for input
  pid_t children[MAX_CHILDREN] = {0};
  for (int i = 0; i < MAX_CHILDREN; i++) {
    children[i] = make_zombie();
  }

  // Iterate through the children and collect statistics
  int stats[256] = {0};
  for (int i = 0; i < MAX_CHILDREN; i++) {
    stats[get_status(children[i])]++;
  }

  for (int i = 0; i < sizeof(stats) / sizeof(stats[0]); i++) {
    if (stats[i] > 0) {
      printf("%c: %d\n", i, stats[i]);
    }
  }


  // Print PID and wait
  char c;
  printf("PID: %d\n", getpid());
  scanf("%c", &c);
  return 0;
}
