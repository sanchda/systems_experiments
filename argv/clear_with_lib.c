// gcc -fPIC -shared clear_lib.c -o libclear.so
// gcc -L. clear_with_lib.c -o main -lclear
// LD_LIBRARY_PATH=. ./main

extern void scrub_args();
extern void read_cmdline();

int main(int argc, char** argv) {
  read_cmdline();
  scrub_args();
  read_cmdline();
  return 0;
}
