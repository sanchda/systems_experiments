not_my_segfault
===

At the time of writing "we" (humans) prefer for computer applications to be structured in such a way that

* user processes "receive" memory mappings from the OS, which they must generally request (but sometimes get for free, e.g., the stack or the main binary itself)
* user processes must only access memory that they have been so given

Again, and as a matter of introduction I emphasize, _generally_.


Dealing with segfaults
==
process_vm_readv
old reliable: write
