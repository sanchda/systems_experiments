contextvar_segfault
===

Due to the way contextvars are implements, it's possible to cause a segfault just by using the module correctly, as
long as someone else is being naughty.

If you pass the argument 'crash' to the script, it will crash while _setting_ the context var (actually, cloning).
