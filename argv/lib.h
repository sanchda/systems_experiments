// Struct to hold argc and argv
typedef struct {
  int argc;
  char **argv;
} Args;

// Get argc and argv, by backtracking from environ
Args get_args();

// Given a pointer to an Args struct, sanitize the argv
// Does not modify argv[0] (the program name)
void sanitize_args(Args *args);

// prints an Args struct
void print_args(Args *args);

// calls `ps` on the current process via `popen()` and prints the output
void print_ps();
