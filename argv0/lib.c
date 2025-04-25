#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

extern char **environ;
int this_argc = 0;
char **this_argv = NULL;

int
get_argc()
{
  // NB - also sets the value of this_argv

  // Only run once
  static bool has_run = false;
  if (has_run)
    return this_argc;
  has_run = true;

  int argc = 0;
  this_argv = environ;

  for (this_argv--; argc < 1024; argc++, this_argv--)
    if (argc == (long)this_argv[-1])
      return argc;
  return 0;
}

const char *
get_argv(int idx)
{
  if (idx < 0 || idx > this_argc)
    return NULL;
  return this_argv[idx];
}

const char *
get_exe()
{
  // Reads /proc/self/exe as a symlink, returning it
  static char buf[4096];
  ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (len == -1)
    return NULL;
  buf[len] = '\0';
  return buf;
}

void __attribute__((constructor))
init_for_fun()
{
  this_argc = get_argc();
  pid_t pid = getpid();
  fprintf(stderr, "[%d] argv[0] = %s\n", pid, get_argv(0));
  if (this_argc > 1)
    fprintf(stderr, "[%d] argv[1] = %s\n", pid, get_argv(1));
  fprintf(stderr, "[%d] exe = %s\n", pid, get_exe());
}
