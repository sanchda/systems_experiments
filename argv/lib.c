#include <stdio.h>
#include <unistd.h>

#include "lib.h"

extern char **environ;

Args get_args() {
  char **argv = environ;
  int argc = 0;

  // Find argc
  for (argv--; argc < 1024; argc++, argv--)
    if (argc  == (long)argv[-1])
      return (Args){argc, argv};

  return (Args){};
}

void sanitize_args(Args *args) {
  for (int i = 1; i < args->argc; i++)
    for (int j = 0; args->argv[i][j]; j++)
      args->argv[i][j] = '*';
}

// prints an Args struct
void print_args(Args *args) {
  printf("argc: %d\n", args->argc);
  for (int i = 0; i < args->argc; i++)
    printf("argv[%d]: %s\n", i, args->argv[i]);
}

// calls `ps` on the current process via `popen()` and prints the output
void print_ps() {
  char cmd[1024];
  char line[1024];

  int result = snprintf(cmd, sizeof(cmd), "ps -p %d -o args=", getpid());
  if (result < 0 || result >= sizeof(cmd)) {
    fprintf(stderr, "Command formatting error\n");
    return;
  }

  FILE *fp = popen(cmd, "r");
  if (fp == NULL) {
    fprintf(stderr, "Failed to run command\n");
    return;
  }

  printf("ps output:\n");
  while (fgets(line, sizeof(line), fp) != NULL) {
    printf("%s", line);
  }
  pclose(fp);
}
