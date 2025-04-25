#ifndef TLS_SIZE
#   define TLS_SIZE 1
#endif

// Stringifying dashes is a pain, so expect the user to quote this.
#ifndef TLS_MODEL
#   define TLS_MODEL "initial-exec"
#endif

extern void foo() {
  // Equivalently, `thread_local` in C++11
  static __thread volatile char a[TLS_SIZE] __attribute__((tls_model(TLS_MODEL)));
  a[0]++;
}
