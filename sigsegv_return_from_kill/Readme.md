# sigsegv_return_from_kill

Most of the time, when a program hits a SIGSEGV, the default behavior is to terminate the progrocess group immediately.
However, if you should override this behavior, your destiny is in your own hands.
I made this experiment to explore a few different conditions.


### A note about segfaults

For some reason, many developers attempt to avoid segmentation faults, and in so doing they lose insight into what they are and how they work.
A segmentation fault is essentially any violation of memory acccess.
Typically, this involves accessing a page which is not mapped for the given process (MAPERR).
It can also involve accessing a page which is mapped, but the access mode (read/write/execute) is not permitted (ACCERR).

At any rate, these cases are handled by the MMU at time of instruction execution.
The MMU will coordinate with the kernel to generate and emit the signal.
In particular, this means that the offending instruction is never executed--so returning from the handler will also return to the fault site unless the PC is modified.

It is also possible to emit a segmentation fault via `kill()` or `tgkill()`.
In this case, the transition into the kernel is deliberate, and returning from the signal might even work out.


### Re-raising

Observe what happens when we direct the application to avoid manually re-raising the signal.

```bash
$ ./watchseg noraise
> Not re-raising the signal
[1308272:1308272:main] hello
[1308272:1308273] SIGSEGV: kill 1308272
[1308272:1308273] I raised a segfault, but I'm alive now.
[1308272:1308272] Attempting to complete handler
[1308272:1308272:main] hello
[1308272:1308272:main] hello
[1308272:1308272:main] hello
[1308272:1308272:main] hello
[1308272:1308272:main] goodbye
```

Wow, there's a lot going on here!

First, the process prints `hello` from its main thread (the output is a PID:TID tuple).
Next, the thread which was launched to emit the segfault calls `kill(getpid(), SIGSEGV)`.
That thread immediately executes the next code, which is to print a log line, after which it terminates normally.
Thread 1380272 (the main thread) is chosen to handle the segfault.
It does so, then returns.
It returns to its position in the main loop, which it completes normally.

All right, so now let's direct the application to use a true segfault instead of a `kill()`.

```
$ ./watchseg noraise do_segfault
> Not re-raising the signal
> Emitting a segfault
[1310260:1310260:main] hello
[1310260:1310261] SIGSEGV: segfault
[1310260:1310261] Attempting to complete handler
[1310260:1310261] Attempting to complete handler
[1310260:1310261] Attempting to complete handler
[1310260:1310261] Attempting to complete handler
[1310260:1310261] Attempting to complete handler
...
<scanning ... just dust and echoes...>
```

Whereas the return from the `kill()` was successful, here the return from the segfault endlessly re-executes the offending instruction, causing execution to hop in and out of the signal handler forever.
Not a surprise, but certainly a reminder that behaviors are not always transferrable between cases.


### PID 1

I think the most surprising category of results is when the process is PID 1.
This emulates a service at the root of a container, which is fairly common.
The idiom for chaining a segfault handler back to SIG_DFL is:

1. When instrumenting the handler in the first place, store the sigaction struct that was there before
2. Hit your signal, do your thing
3. Restore the sigaction struct
4. If that old struct had specified SIG_DFL _and_ you can see that the signal was sent by `kill()`, then you need to re-raise the signal
5. This is a bit unfortunate because your handler is still on the stack, but what can you do?

All right, so under normal operation, what can we expect?

```
$ ./watchseg
[1322856:1322856:main] hello
[1322856:1322857] SIGSEGV: kill 1322856
[1322856:1322857] I raised a segfault, but I'm alive now.
[1322856:1322856] Attempting to complete handler
[1322856:1322856] SIGSEGV: kill 1322856
Segmentation fault
```

OK, so the take-home here is that the process re-raises the signal and it gets terminated.
We knew that.

The `container` mode emulates the nesting of the process within a PID namespace.
In order to do this, it needs some extra permissions.
Before we run it though, let's make sure the base case behaves the same with that escalation.

```
$ sudo ./watchseg
[1324306:1324306:main] hello
[1324306:1324307] SIGSEGV: kill 1324306
[1324306:1324307] I raised a segfault, but I'm alive now.
[1324306:1324306] Attempting to complete handler
[1324306:1324306] SIGSEGV: kill 1324306
Segmentation fault
```

Is PID 1 special?

```
$ sudo ./watchseg container
> Pretending to be in a container
[1:1:main] hello
[1:2] SIGSEGV: kill 1
[1:2] I raised a segfault, but I'm alive now.
[1:1] Attempting to complete handler
[1:1] SIGSEGV: kill 1
$ [1:1:main] hello
[1:1:main] hello
[1:1:main] hello
[1:1:main] hello
[1:1:main] goodbye
```

This is tricky, but do you see what happened?
This time, execution _returned_ from the handler.
Is there some kind of bug here?
Re-run under strace.

```
$ sudo strace -f -s 250000 -v -o log ./watchseg container
...
```

And here's the interesting part.
Note that PIDs are given in the root namespace, since strace was added from the outside.
```
1325084 kill(1, SIGSEGV <unfinished ...>
1325083 futex(0x7fbdc60427e0, FUTEX_WAKE_PRIVATE, 1 <unfinished ...>
1325084 <... kill resumed>)             = 0
1325083 <... futex resumed>)            = 0
1325084 --- SIGSEGV {si_signo=SIGSEGV, si_code=SI_USER, si_pid=1, si_uid=0} ---
1325083 clock_nanosleep(CLOCK_REALTIME, 0, {tv_sec=1, tv_nsec=0},  <unfinished ...>
1325084 gettid()                        = 2
1325084 getpid()                        = 1
1325084 write(1, "[1:2] Attempting to complete handler\n", 37) = 37
1325084 rt_sigaction(SIGSEGV, {sa_handler=SIG_DFL, sa_mask=[], sa_flags=SA_RESTORER, sa_restorer=0x7fbdc605a420}, NULL, 8) = 0
1325084 getpid()                        = 1
1325084 gettid()                        = 2
1325084 gettid()                        = 2
1325084 getpid()                        = 1
1325084 write(1, "[1:2] SIGSEGV: kill 1\n", 22) = 22
1325084 kill(1, SIGSEGV)                = 0
1325083 <... clock_nanosleep resumed>{tv_sec=0, tv_nsec=999723262}) = ? ERESTART_RESTARTBLOCK (Interrupted by signal)
1325084 --- SIGSEGV {si_signo=SIGSEGV, si_code=SI_USER, si_pid=1, si_uid=0} ---
1325083 restart_syscall(<... resuming interrupted clock_nanosleep ...> <unfinished ...>
1325084 rt_sigreturn({mask=[]})         = 0
1325084 gettid()                        = 2
1325084 getpid()                        = 1
1325084 write(1, "[1:2] I raised a segfault, but I'm alive now.\n", 46) = 46
```

Here's the vital sequence:
```
1325084 rt_sigaction(SIGSEGV, {sa_handler=SIG_DFL, sa_mask=[], sa_flags=SA_RESTORER, sa_restorer=0x7fbdc605a420}, NULL, 8) = 0
...
1325084 kill(1, SIGSEGV)                = 0
1325083 <... clock_nanosleep resumed>{tv_sec=0, tv_nsec=999723262}) = ? ERESTART_RESTARTBLOCK (Interrupted by signal)
1325084 --- SIGSEGV {si_signo=SIGSEGV, si_code=SI_USER, si_pid=1, si_uid=0} ---
1325083 restart_syscall(<... resuming interrupted clock_nanosleep ...> <unfinished ...>
1325084 rt_sigreturn({mask=[]})         = 0
1325084 gettid()                        = 2
1325084 getpid()                        = 1
1325084 write(1, "[1:2] I raised a segfault, but I'm alive now.\n", 46) = 46
```

Let's focus on just the handling thread.
The handler uninstalls itself, replaces itself with `SIG_DFL`, and then re-raises the signal.
```
1325084 kill(1, SIGSEGV)                = 0
1325084 --- SIGSEGV {si_signo=SIGSEGV, si_code=SI_USER, si_pid=1, si_uid=0} ---
1325084 rt_sigreturn({mask=[]})         = 0
1325084 gettid()                        = 2
1325084 getpid()                        = 1
1325084 write(1, "[1:2] I raised a segfault, but I'm alive now.\n", 46) = 46
```

`rt_sigreturn()` means that we _came back_ from the signal handler, which is clearly `SIG_DFL`.
And resumed normal execution!
[here](https://github.com/torvalds/linux/blob/v3.0/kernel/signal.c#L2129-L2138) is some kernel code from v3.0, showing how old this behavior is, and what the considerations are.
PID 1 is special.
