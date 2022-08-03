#include <stdio.h>
#include <stdlib.h>

int main(void) {
  size_t sz = snprintf(NULL, 0, "%s--%d--%d\n", "HELLO", 120931, 1249124);
  printf("The size is %ld\n", sz);
  char *buf = malloc(sz+1);
  snprintf(buf, sz, "%s--%d--%d\n", "HELLO", 120931, 1249124);
  printf("The message is: %s\n", buf);
  free(buf);
}
