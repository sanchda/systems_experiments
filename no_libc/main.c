typedef unsigned long long uint64_t;

int syscall_3(uint64_t call_no, uint64_t p1, uint64_t p2, uint64_t p3) {
    int ret;

    asm("mov %1, %%rax\n\t"
        "mov %2, %%rdi\n\t"
        "mov %3, %%rsi\n\t"
        "mov %4, %%rdx\n\t"
        "syscall\n\t"
        "mov %%eax, %0"
        : "=r" (ret)
        : "r" (call_no),
          "r" (p1),
          "r" (p2),
          "r" (p3)
        : "%rax", "%rdi", "%rsi", "%rdx");

    return ret;
}

int execvpe(const char *pathname, char *const argv[], char *const envp[]) {
  // __NR_execve == 59 on x86_64 Linux
  return syscall_3(59, (uint64_t)pathname, (uint64_t)argv, (uint64_t)envp);
}

void _start() {
  char *const argv[] = {"/tmp", 0};
  char *const envp[] = {"LD_PRELOAD=./lolopenat.so", 0};
  execvpe("/usr/bin/ls", argv, envp);
  execvpe("/usr/bin/ls", argv, 0);
}
