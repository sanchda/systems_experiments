# SMAPS Spy TUI

A rich Terminal User Interface (TUI) for analyzing `/proc/*/maps` and `/proc/*/smaps` files. This is a Rust reimplementation of the original Python `maps.py` script with enhanced interactive features.

## Features

- **Multiple View Modes**:
  - **Histogram** - Memory usage grouped by size and permissions
  - **Mappings** - Individual memory mapping details
  - **Chart** - Visual bar chart of memory usage by permissions
  - **Details** - Summary statistics and selected mapping details

- **Interactive Navigation**:
  - Arrow keys or j/k for navigation
  - 1-4 keys to switch between views
  - Tab to cycle through views
  - 's' to cycle through sort options

- **Process Attachment**:
  - Attach to running processes by PID
  - Real-time memory monitoring with `--watch`
  - Automatic process name detection

- **Enhanced Display**:
  - Color-coded information
  - Human-readable memory sizes (KB/MB/GB)
  - Multiple sorting options (count, size, permissions, RSS, PSS)

## Usage

### Analyze a saved maps/smaps file:
```bash
cargo run -- maps.txt
cargo run -- smaps.txt
```

### Attach to a running process:
```bash
# Attach to process by PID
cargo run -- --pid 1234

# Enable real-time updates
cargo run -- --pid 1234 --watch
```

### Get your own process memory info:
```bash
# Get your shell's PID and analyze it
cargo run -- --pid $$
```

### Non-interactive mode (dump histogram):
```bash
# Dump histogram to stdout and exit
cargo run -- --dump smaps.txt
cargo run -- --dump --pid 1234

# Sort by different criteria
cargo run -- --dump --sort size smaps.txt
cargo run -- --dump --sort rss_total --pid 1234
```

## Key Bindings

- `h` - Toggle help
- `q` or `Esc` - Quit (or close help)
- `r` - Reload data
- `s` - Cycle sort options (histogram view)
- `1-4` - Switch to specific view
- `Tab` - Cycle through views
- `↑/↓` or `j/k` - Navigate table rows

## Views

1. **Histogram (1)** - Groups mappings by size/permissions showing counts and totals
2. **Mappings (2)** - Shows individual memory mappings with addresses and paths
3. **Chart (3)** - Visual bar chart of memory usage by permission type
4. **Details (4)** - Summary statistics and detailed info for selected mappings

## Building

```bash
cargo build --release
```

## Improvements over Original Python Version

- Interactive TUI with real-time navigation
- Multiple visualization modes
- Process attachment by PID
- Real-time monitoring capabilities
- **Non-interactive dump mode** for scripting and analysis
- Enhanced memory formatting and statistics
- Color-coded display with sorting options
- Built-in help system

## Sort Options

Available sort criteria for `--sort`:
- `count` (default) - Sort by number of mappings
- `size` - Sort by mapping size
- `perms` - Sort by permissions
- `rss_total` - Sort by total RSS memory
- `rss_avg` - Sort by average RSS per mapping
- `pss_total` - Sort by total PSS memory  
- `pss_avg` - Sort by average PSS per mapping