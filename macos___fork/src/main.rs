use libc::{c_int, perror, printf};
use std::process;

// Declare the external `__fork` function
extern "C" {
    fn __fork() -> c_int;
}

fn main() {
    unsafe {
        // Call `__fork`
        let pid = __fork();

        if pid < 0 {
            perror("Fork failed\0".as_ptr() as *const i8);  // Print error if fork failed
            process::exit(1);
        } else if pid == 0 {
            // This is the child process
            printf("This is the child process.\n\0".as_ptr() as *const i8);
        } else {
            // This is the parent process
            printf("This is the parent process.\n\0".as_ptr() as *const i8);
        }
    }
}
