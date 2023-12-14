# stdio_multiplexing

As the universe moves away from the use of `fork()`, it's time to remember some of the behaviors that have stumped some of us in the past.

Here's one.  Reads to stdin on Linux round-robin to the listening processes.
