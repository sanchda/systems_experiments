# unshare_orphans

Everyone knows that a child's exit status must be reaped by its parent.
But what happens when you're PID 1 in your own namespace and a grandchild gets re-parented to you?
