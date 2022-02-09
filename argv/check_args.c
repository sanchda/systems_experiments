#include "./clear_lib.c"

int main(int n, char** v) {
  printf("Reading my commandline from /proc/self/cmdline.\n");
  read_cmdline();

  // Anonymize args, then print them.
  for (int i = 0; i < n; i++)
    for (int j = strlen(v[i]); j; j--)
      v[i][j-1] = '*';
  printf("I scrubbed my argv.  I'll read the sanitized output for you.\n");
  for (int i = 0; i < n; i++)
    printf("%s ", v[i]);
  printf("\n");

  // Print the commandline again to see if the kernel-detected strategy works
  printf("I'm going to read my commandline from procfs again.\n");
  read_cmdline();

  printf("Now I'm going to scrub argv without knowing it.\n");
  scrub_args();
  read_cmdline();
  return 0;
}
