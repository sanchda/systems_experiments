use nix::{libc::vfork,sys::wait::waitpid,unistd::{close, chroot, dup2, execve, Pid}};
use std::ffi::CString;
use std::env;
use std::os::fd::AsRawFd;
use nix::libc::_exit;

fn main() {
    // The path to the jail directory
    let jail_path = "/tmp/my_jail";

    // Change root to the jail environment
    if let Err(e) = chroot(jail_path) {
        eprintln!("Failed to chroot: {}", e);
        return;
    }

    // Change the working directory to `/` inside the jail
    if let Err(e) = env::set_current_dir("/") {
        eprintln!("Failed to change directory inside chroot: {}", e);
        return;
    }

    // to create stdio
    let stdout = if let Ok(f) = std::fs::File::open("/dev/null") {
        Some(f.as_raw_fd())
    } else {
        None
    };

    let stderr = if let Ok(f) = std::fs::File::create("/dev/null") {
        Some(f.as_raw_fd())
    } else {
        None
    };

    // Create the new process
    match unsafe { vfork() } {
        0 => {
            // Child process
            if let Some(f) = stdout {
                let _ = dup2(f, 1);
            } else {
                let _ = close(1);
            }
            if let Some(f) = stderr {
                let _ = dup2(f, 2);
            } else {
                let _ = close(2);
            }
            let cmd = CString::new("/bin/hello").expect("CString::new failed");
            let args: &[CString] = &[]; // Empty args
            let env: &[CString] = &[]; // Empty env
            let _ = execve(&cmd, args, env);
            eprintln!("Failed to execute command");
            unsafe { _exit(1); }
        }
        -1 => {
            eprintln!("Failed to execute command");
        }
        child_pid => {
            // Parent process
            let status = waitpid(Pid::from_raw(child_pid), None);

            // Check the return status of the child process and print if it is not successful
            // WaitStatus is an enum; just check Exited and Signaled variants
            match status {
                Ok(nix::sys::wait::WaitStatus::Exited(_, code)) => {
                    if code != 0 {
                        eprintln!("Child process failed with exit code: {}", code);
                    }
                }
                Ok(nix::sys::wait::WaitStatus::Signaled(_, signal, _)) => {
                    eprintln!("Child process was terminated by signal: {}", signal);
                }
                _ => {
                    eprintln!("Child process failed");
                }
            }
        }
    }
}
