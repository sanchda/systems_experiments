# no_libc

Sometimes you just want to spawn a process, but you don't want to assume the 
calling process has libc (in order to emulate the behavior of runc).

```
./check.sh $(python -m site --user-site)
```

Or,
```
./check.sh $(python -m site)
```

If you want to see the good ones too, add `--showgood`.
