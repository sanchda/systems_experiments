# fork_without_handlers

`fork()` is documented within POSIX to be async-signal safe, with the caveat that atfork handlers may themselves be unsafe.  At the same time, while POSIX provides some interfaces that are "better" than `fork()` within certain contexts, it doesn't provide a specific way to suppress atfork handlers.

This experiment provides a few different ways to create a child process.  The analysis itself is left as an exercise to the user (I use `strace`).
