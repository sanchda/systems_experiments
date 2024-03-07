static_tls
===

This is a very simple demonstration of where static TLS comes from and how it gets used.

`test.c` defines a simple library where thread-local storage is used.  There are a few flavors of TLS (initial-exec, local-exec, and general-dynamic), but this example uses initial-exec to demonstrate the exhaustion of the static TLS segment.

All the magic is in build.sh.  We test DT_NEEDED, dlopen, and transitive dependencies.
