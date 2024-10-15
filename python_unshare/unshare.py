import os
import subprocess
import sys

def remount_read_only(mount_point="/"):
    """
    Remounts the given filesystem as read-only.
    Unfortunately, Python's os module lacks a way of calling `mount()` directly, so we've gotta do
    it through a call to the `mount` command.
    """
    try:
        subprocess.check_call(["mount", "-o", "remount,ro", mount_point])
    except subprocess.CalledProcessError as e:
        print(f"Failed to remount {mount_point} as read-only: {e}")
        sys.exit(1)

def setup_read_only_mount_namespace():
    """Sets up a new mount namespace and remounts the root filesystem as read-only."""
    try:
        os.unshare(os.CLONE_NEWNS)
    except OSError as e:
        print(f"Failed to create new mount namespace: {e}")
        sys.exit(1)

    # Remount the root filesystem as read-only
    remount_read_only("/")

def child_process():
    """Executes the child process logic with read-only filesystem access."""
    setup_read_only_mount_namespace()

    # Attempts to create a new file in /tmp
    fd = None
    try:
        fd = os.open("/tmp/testfile", os.O_CREAT | os.O_RDWR)
    except OSError as e:
        print(f"Failed to create file: {e}")
        sys.exit(1)

    try:
        os.write(fd, b"Hello, world!")
    except OSError as e:
        print(f"Failed to write to file: {e}")
        sys.exit(1)

    os.close(fd)
    print("File created successfully.  Which is bad.  This isn't supposed to happen.")
    sys.exit(0)

def main():
    """Main function to fork the process and manage the child."""
    try:
        pid = os.fork()
    except OSError as e:
        print(f"Fork failed: {e}")
        sys.exit(1)

    if pid == 0:
        child_process()
    else:
        os.waitpid(pid, 0)

if __name__ == "__main__":
    main()

