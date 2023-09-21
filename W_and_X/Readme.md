W_and_X
===

Ever since multics, unices have acknowledged W^X as a useful security posture, even though it inhibits really cool polymorphic code concepts.

In order to get around this, many JIT systems (emulators, VMs) use a sequence of mprotect operations in order to rewrite executable pages.

https://papers.ssrn.com/sol3/papers.cfm?abstract_id=4553439 proposes another use for polymorphism, so I got to thinking about how to overcome
W^X without disabling it.

1. Figure out an executable section (typically you'd do this by processing the ELF metadata, but I cheated here)
2. Determine the VMA range and file offset
3. Create an identically-sized memfd
4. mmap the memfd with rw
5. copy the segment there
6. mmap the memfd again with rx, but with MAP_FIXED to overwrite the already-mapped segment
7. now you can write to rw and get the changes in rx without syscalls

I think this implementation is thread-safe, which is kind of cool, but I'd need to think harder about that.
