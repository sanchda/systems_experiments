#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#define rdmsrl(msr, val) ((val) = native_read_msr((msr)))
#define DECLARE_ARGS(val, low, high)  unsigned long low, high
#define EAX_EDX_VAL(val, low, high) ((low) | (high) << 32)
#define EAX_EDX_RET(val, low, high) "=a" (low), "=d" (high)

#define MSR_IA32_MPERF  0x000000e7
#define MSR_IA32_APERF  0x000000e8

# define _EXPAND_EXTABLE_HANDLE(x) #x
# define _ASM_EXTABLE_HANDLE(from, to)     \
  " .pushsection \"__ex_table\",\"a\"\n"      \
  " .balign 4\n"            \
  " .long (" #from ") - .\n"        \
  " .long (" #to ") - .\n"        \

typedef uint64_t u64;
typedef uint32_t u32;
typedef int32_t s32;

struct user_pt_regs {
  u64   regs[31];
  u64   sp;
  u64   pc;
  u64   pstate;
};

struct pt_regs {
  union {
    struct user_pt_regs user_regs;
    struct {
      u64 regs[31];
      u64 sp;
      u64 pc;
      u64 pstate;
    };
  };
  u64 orig_x0;
#ifdef __AARCH64EB__
  u32 unused2;
  s32 syscallno;
#else
  s32 syscallno;
  u32 unused2;
#endif
  u64 sdei_ttbr1;
  /* Only valid when ARM64_HAS_IRQ_PRIO_MASKING is enabled. */
  u64 pmr_save;
  u64 stackframe[2];

  /* Only valid for some EL1 exceptions. */
  u64 lockdep_hardirqs;
  u64 exit_rcu;
};

struct exception_table_entry {
  int insn, fixup;
  long handler;
};

static inline unsigned long
ex_fixup_addr(const struct exception_table_entry *x)
{
  return (unsigned long)&x->fixup + x->fixup;
}

bool ex_handler_rdmsr_unsafe(const struct exception_table_entry *fixup,
               struct pt_regs *regs, int trapnr,
               unsigned long error_code,
               unsigned long fault_addr)
{
  return true;
}

static inline unsigned long long __rdmsr(unsigned int msr) {
  DECLARE_ARGS(val, low, high);
  asm volatile("1: rdmsr\n"
               "2:\n"
               _ASM_EXTABLE_HANDLE(1b, 2b)
               : EAX_EDX_RET(val, low, high) : "c" (msr));
  return EAX_EDX_VAL(val, low, high);
}

static inline unsigned long long native_read_msr(unsigned int msr) {
  unsigned long long val;
  val = __rdmsr(msr);
  return val;
}

int main(void) {
  unsigned long long aperf = 0, mperf = 0;
  aperf = __rdmsr(MSR_IA32_APERF);
  printf("APERF: %llu\n", aperf);
}
