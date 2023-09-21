#include <stdio.h>
#include "lib.h"

int main(int argc, char **argv) {
  argv[0][0] = '*';
  print_ps();
  return 0;
}
