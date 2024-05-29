# memfd_secret

The `memfd_secret()` system call was introduced in Linux 5.14.  It has something of a storied past.

What matters though is that it makes page ranges unreadable by any process.

What does this mean?  Well, think about what happens when you use `ps` or read `/proc/<pid>/cmdline`.
