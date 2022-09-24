mmap coalescing
===
The summary view you get from `/proc/<pid>/maps` (and even `/proc/<pid>/smaps`) can be somewhat misleading for establishing the dynamical behavior of allocations in a given process.  In particular, when pages with equivalent settings are mapped to a contiguous address range through multiple calls to `mmap()`, they will appear as a single region in procfs summaries.

Likewise, when pages in the middle of such a region are unmapped or have their settings modified (e.g., with `madvise()`), each contiguous range is shown as its own region no matter how many individual calls (e.g., to `mmap()`) were made to realize it.

For analysts, this is also evident in the behavior of `ld.so`, which uses a few interesting techniques to guarantee that that relative offsets are preserved in mappings.  Maybe one day I'll write about that (other people already have).
