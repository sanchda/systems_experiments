use nix::unistd::chroot;
use std::process::Stdio;
use std::env;
use std::process::Command;

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
    let stdout_stream = Stdio::null();
    let stderr_stream = Stdio::null();

    let _child = Command::new("/bin/echo")
        .stdout(stdout_stream)
        .stderr(stderr_stream)
        .spawn()
        .expect("Failed to execute command");
}
