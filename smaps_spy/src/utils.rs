// File watching and UI constants
pub const FILE_WATCH_INTERVAL_SECS: u64 = 1;
pub const STATUS_MESSAGE_TIMEOUT_SECS: u64 = 3;
pub const INPUT_POLL_INTERVAL_MS: u64 = 100;
pub const MAX_LINES_AFTER_MAPPING: usize = 20;
pub const BYTES_PER_KB: u64 = 1024;
pub const TOP_CHART_RANGES: usize = 8;
pub const CHART_BAR_WIDTH: u16 = 9;

// Memory size constants
pub const SIZE_4KB: u64 = 4 * BYTES_PER_KB;
pub const SIZE_64KB: u64 = 64 * BYTES_PER_KB;
pub const SIZE_1MB: u64 = BYTES_PER_KB * BYTES_PER_KB;
pub const SIZE_16MB: u64 = 16 * SIZE_1MB;
pub const SIZE_256MB: u64 = 256 * SIZE_1MB;

pub fn calculate_average(total: u64, count: u32) -> u64 {
    if count > 0 { 
        total / count as u64 
    } else { 
        0 
    }
}

pub fn validate_pid(pid: u32) -> anyhow::Result<()> {
    if pid == 0 {
        anyhow::bail!("PID cannot be 0");
    }
    
    if pid > 4194304 { // Maximum PID on most Linux systems
        anyhow::bail!("PID {} is too large (max: 4194304)", pid);
    }
    
    // Check if the process directory exists
    let proc_dir = format!("/proc/{}", pid);
    if !std::path::Path::new(&proc_dir).exists() {
        anyhow::bail!("Process with PID {} does not exist", pid);
    }
    
    Ok(())
}

pub fn validate_file_path(path: &str) -> anyhow::Result<()> {
    let file_path = std::path::Path::new(path);
    
    if !file_path.exists() {
        anyhow::bail!("File '{}' does not exist", path);
    }
    
    if !file_path.is_file() {
        anyhow::bail!("'{}' is not a file", path);
    }
    
    // Check if we can read the file
    match std::fs::File::open(file_path) {
        Ok(_) => Ok(()),
        Err(e) => anyhow::bail!("Cannot read file '{}': {}", path, e),
    }
}

pub fn format_bytes(bytes: u64) -> String {
    const UNITS: &[&str] = &["B", "KB", "MB", "GB", "TB"];
    let mut size = bytes as f64;
    let mut unit_index = 0;

    while size >= BYTES_PER_KB as f64 && unit_index < UNITS.len() - 1 {
        size /= BYTES_PER_KB as f64;
        unit_index += 1;
    }

    if unit_index == 0 {
        format!("{} {}", bytes, UNITS[unit_index])
    } else {
        format!("{:.1} {}", size, UNITS[unit_index])
    }
}