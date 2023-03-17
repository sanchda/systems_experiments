# autodaemonize

Sometimes you call a function, and sometimes you call a service.

... But sometimes you do both!

## wat tho

It's somewhat common to classify Linux processes according to three groups
 * My application (foreground process), which "I" launched
 * system services (like httpd), which "I" did not launch
 * idk maybe kernel stuff, which is mysterious

In reality, the lines between the first two categories are a bit blurry.
Many folks know that <ctrl+z> will use the shell's process management tools to
push the active process to the background.  It's comparatively unknown that
some tools (like tmux!) will spawn their own daemons/servers when a user calls
their CLI, and it's downright arcane to consider the possibility that a library
may spawn some kind of background daemon or message broker.

In this particular example,
 * Add a primitive type which uses shared memory for coordination
 * Add a daemon class which spawns a helpful background process
 * The background process can be closed manually, or it will clean itself up
 
Some parts of this are overkill and some parts are totally nonlethal, depending
on what you'd want to use this for.  I like the discipline of combining this
with an embedded executable + memfd_create + execve("/proc/self/fd/<memfd>")
to totally decouple the daemon.  Bonus points for putting the coordinator
objects in tmpfs!
