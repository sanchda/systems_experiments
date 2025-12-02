# PID 1 Signal Propagation Experiments

Experiments demonstrating signal delivery to processes, particularly PID 1 behavior in containers.

## Build

```bash
./build.sh
```

Produces: `wrapper` and `sigterm_test`

## Programs

- **sigterm_test**: Catches SIGINT, prints "Signal received"
- **wrapper**: Forks/execs a binary with configurable signal handling

### Wrapper Modes

- `--default`: No signal handlers installed
- `--no-ignore`: Explicitly set SIG_DFL
- `--ignore`: Set SIG_IGN for SIGTERM/SIGINT
- `--pass`: Forward signals to child process

## How Signals Work

### Signal Delivery Mechanisms

1. **Terminal-generated signals (CTRL+C, CTRL+Z)**: Sent to the **foreground process group**
   - Kernel delivers to all processes in the terminal's foreground group
     - the terminal did a `setpgid`/`tcsetpgrp` to set the foreground process, so the kernel knows
   - Parent processes typically NOT in this group

2. **Direct signals (kill command)**: Sent to specific PID
   - `kill -SIGINT <pid>` targets exactly that process
   - Does not propagate to children automatically

3. **Process groups and sessions**:
   - Shell creates new process group for foreground jobs
   - Children inherit process group from parent
   - Signal disposition (handlers) NOT inherited across exec()

4. **Signal handling options**:
   - `SIG_DFL`: Default action (usually terminate)
   - `SIG_IGN`: Ignore signal
   - Custom handler: Run user function

### PID 1 Special Behavior

- Kernel does NOT deliver signals to PID 1 unless it has an explicit handler
- PID 1 with `SIG_DFL` will NOT terminate on SIGTERM (kernel protection)
- PID 1 must forward signals to children if needed (not automatic)

## Experiments

### Foreground: CTRL+C goes to child directly

```bash
strace -I 4 -f -etrace=signal ./wrapper --default ./sigterm_test
# Press CTRL+C
```

Result: Child receives SIGINT from terminal, wrapper does not.

### Background: kill targets parent

Terminal 1:
```bash
./wrapper --pass ./sigterm_test
# Note wrapper PID
```

Terminal 2:
```bash
kill -INT <wrapper_pid>
```

Result: Wrapper receives signal, forwards to child with `--pass`.

### Trace signal paths

```bash
# Foreground - child gets terminal signal
strace -I 4 -f -etrace=signal ./wrapper --default ./sigterm_test
# Press CTRL+C

# Background - parent gets kill signal, forwards it
strace -I 4 -f -etrace=signal ./wrapper --pass ./sigterm_test &
kill -INT $!
```

## Docker Behavior

Docker sends signals only to PID 1. Children must be explicitly signaled.

### Build

```bash
docker build -t sigtest .
```

### Test: Default mode (exits immediately)

```bash
docker run --name test-default sigtest /wrapper --default /sigterm_test
docker stop test-default  # from another terminal
```

Wrapper (PID 1) exits on SIGTERM, child killed.

### Test: Ignore mode (force kill after timeout)

```bash
docker run --name test-ignore sigtest /wrapper --ignore /sigterm_test
docker stop test-ignore
```

Wrapper ignores SIGTERM, Docker sends SIGKILL after 10s.

### Test: Pass mode (signal forwarded)

```bash
docker run --name test-pass sigtest /wrapper --pass /sigterm_test
docker stop test-pass
```

Wrapper forwards SIGTERM to child, clean exit.

### CTRL+C with Docker

```bash
docker run sigtest /wrapper --pass /sigterm_test
# Press CTRL+C
```

Flow:
1. CTRL+C → Docker CLI (foreground process)
2. Docker CLI → SIGTERM to container's PID 1
3. Child only gets signal if wrapper uses `--pass`

Container processes are isolated from host terminal signals.

## Summary

- Terminal signals (CTRL+C) target foreground process group
- Direct signals (kill) target specific PID
- Docker always signals PID 1 only
- PID 1 must explicitly handle and forward signals to children
