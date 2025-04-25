# magic_bytes

binfmt checks for magic bytes, but if we want to dispatch an ELF object through
binfmt_misc we need to change the magic bytes.  It looks scary.  Can we do it?
