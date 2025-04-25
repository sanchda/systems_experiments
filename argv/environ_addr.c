#include <stdio.h>

extern char **environ;
void *get_stack_top() {
  int _; return &_;
}

int main() {
  printf("environ diff: %lx\n", environ - (char **)get_stack_top());
  return 0;
}
