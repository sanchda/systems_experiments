##evil_static_init

Static initialization occurs whenever a mapping is loaded, either at process load-time or even during dynamic loading.  The former occurs before `__start__`, and the latter occurs afterward.

Your library doesn't need to be referenced in the main code for initialization to occur.  You can do tricksy things with this.


Three examples are given here.
1. Does not load the dynamic library at all, regular execution occurs (echos the prompt)
2. Build-time dependency.  Static initialization occurs at application startup.
3. Dynamic-link dependency.  Static initialization occurs at `dlopen()`.
4. Using `LD_PRELOAD=./libevil.so` with example 1
