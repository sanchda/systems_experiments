mod parser;
mod ui;
mod utils;

use anyhow::Result;
use clap::Parser;
use crossterm::{
    event::{self, DisableMouseCapture, EnableMouseCapture, Event, KeyCode, KeyEventKind},
    execute,
    terminal::{disable_raw_mode, enable_raw_mode, EnterAlternateScreen, LeaveAlternateScreen},
};
use ratatui::{backend::CrosstermBackend, Terminal};
use std::{
    io,
    path::PathBuf,
    time::{Duration, Instant},
};
use ui::{AppState, ViewMode};
use utils::{format_bytes, calculate_average, validate_pid, validate_file_path, FILE_WATCH_INTERVAL_SECS, STATUS_MESSAGE_TIMEOUT_SECS, INPUT_POLL_INTERVAL_MS};


#[derive(Parser)]
#[command(name = "smaps_spy_tui")]
#[command(about = "A rich TUI for analyzing /proc/*/maps and /proc/*/smaps files")]
struct Args {
    /// Path to the maps or smaps file, or use --pid to attach to a process
    #[arg(conflicts_with = "pid")]
    file_path: Option<PathBuf>,
    
    /// Process ID to attach to (reads /proc/PID/smaps)
    #[arg(short, long, conflicts_with = "file_path")]
    pid: Option<u32>,
    
    /// Enable file watching for real-time updates
    #[arg(short, long)]
    watch: bool,
    
    /// Non-interactive mode: dump histogram and exit
    #[arg(long)]
    dump: bool,
    
    /// Sort criteria for dump mode
    #[arg(long, default_value = "count")]
    sort: String,
}

fn main() -> Result<()> {
    let args = Args::parse();
    
    // Determine the file path or PID with validation
    let (file_path, pid) = match (args.file_path, args.pid) {
        (Some(path), None) => {
            let path_str = path.to_string_lossy().to_string();
            validate_file_path(&path_str)?;
            (path_str, None)
        },
        (None, Some(pid)) => {
            validate_pid(pid)?;
            let smaps_path = format!("/proc/{}/smaps", pid);
            // For smaps, we don't validate the file here since it might not exist (e.g., process with no memory mappings)
            // The parser will handle the file existence check with a better error message
            (smaps_path, Some(pid))
        },
        (None, None) => {
            anyhow::bail!("Must specify either a file path or --pid");
        }
        (Some(_), Some(_)) => unreachable!(), // clap prevents this
    };
    
    // Create app state
    let mut app = AppState::new(file_path, pid);
    
    // Load initial data
    load_data(&mut app)?;
    
    // If dump mode, print histogram and exit
    if args.dump {
        dump_histogram(&app, &args.sort)?;
        return Ok(());
    }
    
    // Setup terminal for interactive mode
    enable_raw_mode()?;
    let mut stdout = io::stdout();
    execute!(stdout, EnterAlternateScreen, EnableMouseCapture)?;
    let backend = CrosstermBackend::new(stdout);
    let mut terminal = Terminal::new(backend)?;
    
    // Run the application
    let res = run_app(&mut terminal, &mut app, args.watch);

    // Restore terminal
    disable_raw_mode()?;
    execute!(
        terminal.backend_mut(),
        LeaveAlternateScreen,
        DisableMouseCapture
    )?;
    terminal.show_cursor()?;

    if let Err(err) = res {
        println!("{:?}", err);
    }

    Ok(())
}

fn run_app<B: ratatui::backend::Backend>(
    terminal: &mut Terminal<B>,
    app: &mut AppState,
    watch: bool,
) -> Result<()> {
    let mut last_reload = Instant::now();
    let reload_interval = Duration::from_secs(FILE_WATCH_INTERVAL_SECS);

    loop {
        terminal.draw(|f| ui::draw(f, app))?;

        // Handle file watching
        if watch && last_reload.elapsed() >= reload_interval {
            if let Err(e) = load_data(app) {
                app.status_message = format!("Error reloading: {}", e);
            } else {
                app.status_message = "Reloaded".into();
            }
            last_reload = Instant::now();
        }

        // Handle input
        if event::poll(Duration::from_millis(INPUT_POLL_INTERVAL_MS))? {
            if let Event::Key(key) = event::read()? {
                if key.kind == KeyEventKind::Press {
                    match key.code {
                        KeyCode::Char('q') | KeyCode::Esc => {
                            if app.show_help {
                                app.show_help = false;
                            } else {
                                break;
                            }
                        }
                        KeyCode::Char('h') => {
                            app.show_help = !app.show_help;
                        }
                        KeyCode::Char('r') => {
                            if let Err(e) = load_data(app) {
                                app.status_message = format!("Error reloading: {}", e);
                            } else {
                                app.status_message = "Reloaded".into();
                            }
                        }
                        KeyCode::Char('s') => {
                            app.next_sort();
                            app.status_message = format!("Sort by: {:?}", app.sort_by);
                        }
                        KeyCode::Char('1') => {
                            app.view_mode = ViewMode::Histogram;
                            app.table_state.select(Some(0));
                        }
                        KeyCode::Char('2') => {
                            app.view_mode = ViewMode::Mappings;
                            app.table_state.select(Some(0));
                        }
                        KeyCode::Char('3') => {
                            app.view_mode = ViewMode::Chart;
                        }
                        KeyCode::Char('4') => {
                            app.view_mode = ViewMode::Details;
                        }
                        KeyCode::Char('m') => {
                            if matches!(app.view_mode, ViewMode::Chart) {
                                app.next_chart_metric();
                                app.status_message = format!("Chart metric: {:?}", app.chart_metric);
                            }
                        }
                        KeyCode::Tab => {
                            app.next_view();
                            if matches!(app.view_mode, ViewMode::Histogram | ViewMode::Mappings) {
                                app.table_state.select(Some(0));
                            }
                        }
                        KeyCode::Down | KeyCode::Char('j') => {
                            if matches!(app.view_mode, ViewMode::Histogram | ViewMode::Mappings) {
                                app.table_next();
                            }
                        }
                        KeyCode::Up | KeyCode::Char('k') => {
                            if matches!(app.view_mode, ViewMode::Histogram | ViewMode::Mappings) {
                                app.table_previous();
                            }
                        }
                        _ => {}
                    }
                }
            }
        }

        // Clear status message after some time
        if !app.status_message.is_empty() && last_reload.elapsed() > Duration::from_secs(STATUS_MESSAGE_TIMEOUT_SECS) {
            app.status_message.clear();
        }
    }

    Ok(())
}

fn load_data(app: &mut AppState) -> Result<()> {
    let stats = parser::parse_file(&app.file_path)?;
    app.memory_stats = Some(stats);
    app.table_state.select(Some(0));
    app.selected_mapping = Some(0);
    Ok(())
}

fn dump_histogram(app: &AppState, sort_by: &str) -> Result<()> {
    if let Some(stats) = &app.memory_stats {
        print_header(stats, &app.file_path);
        print_summary(stats);
        
        let histogram_data = sort_histogram_data(&stats.histogram, sort_by);
        print_histogram_table(&histogram_data, stats.total_rss > 0 || stats.total_pss > 0);
        
        println!();
        println!("Sort options: count, size, perms, rss_total, rss_avg, pss_total, pss_avg");
    } else {
        println!("No data loaded");
    }
    
    Ok(())
}

fn print_header(stats: &parser::MemoryStats, file_path: &str) {
    if let Some(pid) = stats.pid {
        println!("Memory Analysis for PID: {}", pid);
        if let Some(name) = &stats.process_name {
            println!("Process Name: {}", name);
        }
    } else {
        println!("Memory Analysis for file: {}", file_path);
    }
    println!();
}

fn print_summary(stats: &parser::MemoryStats) {
    println!("Summary:");
    println!("  Total Mappings: {}", stats.mappings.len());
    println!("  Total Virtual Memory: {}", format_bytes(stats.total_virtual));
    if stats.total_rss > 0 {
        println!("  Total RSS: {}", format_bytes(stats.total_rss));
    }
    if stats.total_pss > 0 {
        println!("  Total PSS: {}", format_bytes(stats.total_pss));
    }
    println!();
}

fn sort_histogram_data<'a>(
    histogram: &'a std::collections::HashMap<(u64, String), parser::HistogramEntry>,
    sort_by: &str,
) -> Vec<(&'a (u64, String), &'a parser::HistogramEntry)> {
    let mut histogram_data: Vec<_> = histogram.iter().collect();
    
    match sort_by {
        "size" => histogram_data.sort_by(|a, b| b.0.0.cmp(&a.0.0)),
        "perms" => histogram_data.sort_by(|a, b| a.0.1.cmp(&b.0.1)),
        "rss_total" => histogram_data.sort_by(|a, b| b.1.total_rss.cmp(&a.1.total_rss)),
        "rss_avg" => {
            histogram_data.sort_by(|a, b| {
                let avg_a = calculate_average(a.1.total_rss, a.1.count);
                let avg_b = calculate_average(b.1.total_rss, b.1.count);
                avg_b.cmp(&avg_a)
            });
        }
        "pss_total" => histogram_data.sort_by(|a, b| b.1.total_pss.cmp(&a.1.total_pss)),
        "pss_avg" => {
            histogram_data.sort_by(|a, b| {
                let avg_a = calculate_average(a.1.total_pss, a.1.count);
                let avg_b = calculate_average(b.1.total_pss, b.1.count);
                avg_b.cmp(&avg_a)
            });
        }
        _ => histogram_data.sort_by(|a, b| b.1.count.cmp(&a.1.count)), // default: count
    }
    
    histogram_data
}

fn print_histogram_table(
    histogram_data: &[(&(u64, String), &parser::HistogramEntry)],
    has_rss_pss: bool,
) {
    if has_rss_pss {
        print_smaps_table(histogram_data);
    } else {
        print_maps_table(histogram_data);
    }
}

fn print_smaps_table(histogram_data: &[(&(u64, String), &parser::HistogramEntry)]) {
    println!("{:>17} | {:^11} | {:>5} | {:>18} | {:>17} | {:>13} | {:>17} | {:>13}",
        "Size", "Perms", "Count", "Total Size", "Total RSS", "Avg RSS", "Total PSS", "Avg PSS");
    println!("{}", "-".repeat(137));
    
    for ((size, perms), entry) in histogram_data {
        let avg_rss = calculate_average(entry.total_rss, entry.count);
        let avg_pss = calculate_average(entry.total_pss, entry.count);
        
        println!("{:>17} | {:^11} | {:>5} | {:>18} | {:>17} | {:>13} | {:>17} | {:>13}",
            format_bytes(*size),
            perms,
            entry.count,
            format_bytes(entry.total_size),
            format_bytes(entry.total_rss),
            format_bytes(avg_rss),
            format_bytes(entry.total_pss),
            format_bytes(avg_pss),
        );
    }
}

fn print_maps_table(histogram_data: &[(&(u64, String), &parser::HistogramEntry)]) {
    println!("{:>17} | {:^11} | {:>5} | {:>18}",
        "Size", "Perms", "Count", "Total Size");
    println!("{}", "-".repeat(56));
    
    for ((size, perms), entry) in histogram_data {
        println!("{:>17} | {:^11} | {:>5} | {:>18}",
            format_bytes(*size),
            perms,
            entry.count,
            format_bytes(entry.total_size),
        );
    }
}

