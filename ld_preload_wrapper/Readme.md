# ld_preload_wrapper

People often refer to "LD_PRELOAD tricks," but then struggle to put one together.

Here's one put together.

As an extra bonus, this is packaged in such a way that the user can just call it as a self-contained wrapper binary.

## How does it all work?

1. env.c defines some wrapper operations over things like `getenv`.  It uses `dlsym()` in order to get the true glibc symbols.  When it is loaded this way, these definitions are inserted ahead of their libc counterparts, and so subsequent callers use them.
2. env.c writes directly to stdout using `writev()`.  It does this operation directly.  Note how the syscall number is at the end--this prevents having to shuffle arguments around.
3. env.c is compiled as a shared object.  I can juice it down to a smaller binary, I think, but this doesn't matter too much for me.
4. `ld` is used to bundle the shared library in 3 into a .o which can be linked.  Two symbols are added to this .o by the linker (start and end pointers) to indicate the size of the object.  You just have to know what these things are (if you get this far, you can use `readelf` to find them).
5. peekenv.c is linked with `env.o` from the last step.  It uses those symbols to find the shared object.  Then it copies that shared object into a memfd and adds the procfs/fd symlink of that memfd to its own `LD_PRELOAD` environment.
6. peekenv.c then executes the target binary.
