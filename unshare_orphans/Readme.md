# unshare_orphans

On POSIX systems, when a process is configured to receive notice when one of its child processes has ended, then it will receive notice for all children.

Additionally, the orphan reaping behavior is made complex by containers
* An orphan is a process whose immediate parent has died
* Rather than being re-parented to a grandparent, processes are re-parented to PID 1 in their current PID namespace (e.g. containers)
* In almost all distributions, PID 1 in the root namespace (the init process) is written so that it can "reap" child processes
* Processes which have exited, but have not had their exit status reaped, are called "zombies" and remain in a defunct state until their exit status is reaped
* A PID 1 which calls `waitpid(-1, ...` anticipating an `ECHILD` will hang indefinitely if it's been made the parent of an unexpected child (e.g., it has a non-terminating child process, but it expects all of its children to have bounded lifetimes)
