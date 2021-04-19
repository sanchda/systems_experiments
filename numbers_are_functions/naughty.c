#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <stdlib.h>
#include <stdio.h>

// C does not allow you to treat an arbitrary number or pointer as a function
// pointer, but it does allow you to form a union between those types, and it
// also allows you to call a function pointer on the wrong arguments, so it
// boils down to a syntactic barrier and nothing more.
union hackptr {
  void(*fun)(void);
  long ptr;
};

int main() {
  unsigned long long ptr = 0x000000000000113c; // from the symbol table

  // Let's map in the `test` binary as executable!
  unsigned long long mem = mmap(NULL, 0x100 + 0x113c, PROT_READ | PROT_EXEC, MAP_PRIVATE, open("test", O_RDONLY), 0);
  int (*fun)(int) = ((union hackptr){.ptr=ptr+mem}).fun;

  int x = fun(665);
  printf("The value of x is %d\n", x);

  return 0;
}
