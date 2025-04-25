# vdso_elf
This shows that the vdso region is itself a valid ELF object.

It also shows how to dump it as a file which can be used for external tools,
without actually writing to the file system anywhere.

...It _also_ shows that since `write()` implements map-boundary protection, and
the dynamic linker on Linux ensures at least one unmapped page between loaded
regions, you don't really have to check the size of the region to copy it
(doing otherwise requires processing the ELF header in multiple ways to get
region size, or reading /proc/self/maps).
