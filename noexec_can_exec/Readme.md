# can_noexec_exec

A common point of confusion is the degree to which filesystem-level executability bits are meaningful for shared objects.
For instance, it's fairly well-known that it doesn't matter if a .so has `+x` set, since the dynamic loader doesn't require it to `mmap()` the segment with `PROT_EXEC`.
On the other hand, if the filesystem is mounted with `noexec`, then an `mmap()` operation requesting `PROT_EXEC` will fail.

Wanna see?
Here you go.

Something that isn't explored here is the process of overcoming this limitation, e.g., by using `memfd_create()` or even copying the .so.
