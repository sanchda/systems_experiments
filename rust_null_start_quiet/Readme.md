# rust_null_start_quiet

I was looking at how processes start in very locked-down environments and was struck that lack of access to /dev/null resulted in a sigabrt.
Here's a simple demonstration.
