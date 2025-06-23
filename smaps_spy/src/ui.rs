use crate::parser::MemoryStats;
use crate::utils::{format_bytes, calculate_average, SIZE_4KB, SIZE_64KB, SIZE_1MB, SIZE_16MB, SIZE_256MB, TOP_CHART_RANGES, CHART_BAR_WIDTH, BYTES_PER_KB};
use ratatui::{
    layout::{Alignment, Constraint, Direction, Layout, Rect},
    style::{Color, Modifier, Style},
    text::{Line, Span},
    widgets::{
        BarChart, Block, Borders, Cell, Clear, Paragraph, Row,
        Table, TableState, Tabs,
    },
    Frame,
};

#[derive(Debug, Clone)]
pub enum SortBy {
    Count,
    Size,
    Permissions,
    TotalRss,
    AvgRss,
    TotalPss,
    AvgPss,
}

#[derive(Debug, Clone)]
pub enum ChartMetric {
    Size,
    Count,
    TotalSize,
    TotalRss,
    AverageRss,
}

#[derive(Debug, Clone)]
pub enum ViewMode {
    Histogram,
    Mappings,
    Chart,
    Details,
}

pub struct AppState {
    pub memory_stats: Option<MemoryStats>,
    pub view_mode: ViewMode,
    pub chart_metric: ChartMetric,
    pub sort_by: SortBy,
    pub table_state: TableState,
    pub selected_mapping: Option<usize>,
    pub show_help: bool,
    pub file_path: String,
    pub pid: Option<u32>,
    pub status_message: String,
}

impl AppState {
    pub fn new(file_path: String, pid: Option<u32>) -> Self {
        Self {
            memory_stats: None,
            view_mode: ViewMode::Histogram,
            chart_metric: ChartMetric::Size,
            sort_by: SortBy::Count,
            table_state: TableState::default(),
            selected_mapping: None,
            show_help: false,
            file_path,
            pid,
            status_message: String::new(),
        }
    }

    pub fn next_sort(&mut self) {
        self.sort_by = match self.sort_by {
            SortBy::Count => SortBy::Size,
            SortBy::Size => SortBy::Permissions,
            SortBy::Permissions => SortBy::TotalRss,
            SortBy::TotalRss => SortBy::AvgRss,
            SortBy::AvgRss => SortBy::TotalPss,
            SortBy::TotalPss => SortBy::AvgPss,
            SortBy::AvgPss => SortBy::Count,
        };
    }

    pub fn next_view(&mut self) {
        self.view_mode = match self.view_mode {
            ViewMode::Histogram => ViewMode::Mappings,
            ViewMode::Mappings => ViewMode::Chart,
            ViewMode::Chart => ViewMode::Details,
            ViewMode::Details => ViewMode::Histogram,
        };
    }

    pub fn next_chart_metric(&mut self) {
        self.chart_metric = match self.chart_metric {
            ChartMetric::Size => ChartMetric::Count,
            ChartMetric::Count => ChartMetric::TotalSize,
            ChartMetric::TotalSize => ChartMetric::TotalRss,
            ChartMetric::TotalRss => ChartMetric::AverageRss,
            ChartMetric::AverageRss => ChartMetric::Size,
        };
    }

    pub fn table_next(&mut self) {
        if let Some(stats) = &self.memory_stats {
            let len = if matches!(self.view_mode, ViewMode::Histogram) {
                stats.histogram.len()
            } else {
                stats.mappings.len()
            };
            
            if len > 0 {
                let current = self.table_state.selected().unwrap_or(0);
                let next = if current >= len - 1 { 0 } else { current + 1 };
                self.table_state.select(Some(next));
                self.selected_mapping = Some(next);
            }
        }
    }

    pub fn table_previous(&mut self) {
        if let Some(stats) = &self.memory_stats {
            let len = if matches!(self.view_mode, ViewMode::Histogram) {
                stats.histogram.len()
            } else {
                stats.mappings.len()
            };
            
            if len > 0 {
                let current = self.table_state.selected().unwrap_or(0);
                let previous = if current == 0 { len - 1 } else { current - 1 };
                self.table_state.select(Some(previous));
                self.selected_mapping = Some(previous);
            }
        }
    }
}

pub fn draw(f: &mut Frame, app: &mut AppState) {
    let size = f.size();

    if app.show_help {
        draw_help_popup(f, size);
        return;
    }

    let chunks = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Length(3), // Header
            Constraint::Min(0),    // Main content
            Constraint::Length(3), // Footer
        ])
        .split(size);

    draw_header(f, chunks[0], app);
    draw_main_content(f, chunks[1], app);
    draw_footer(f, chunks[2], app);
}

fn draw_header(f: &mut Frame, area: Rect, app: &AppState) {
    let tabs = vec![
        "Histogram (1)",
        "Mappings (2)",
        "Chart (3)",
        "Details (4)",
    ];
    
    let selected_tab = match app.view_mode {
        ViewMode::Histogram => 0,
        ViewMode::Mappings => 1,
        ViewMode::Chart => 2,
        ViewMode::Details => 3,
    };

    let title = if let Some(pid) = app.pid {
        if let Some(stats) = &app.memory_stats {
            if let Some(name) = &stats.process_name {
                format!("SMAPS Spy TUI - PID: {} ({})", pid, name)
            } else {
                format!("SMAPS Spy TUI - PID: {}", pid)
            }
        } else {
            format!("SMAPS Spy TUI - PID: {}", pid)
        }
    } else {
        "SMAPS Spy TUI".into()
    };

    let tabs = Tabs::new(tabs)
        .block(Block::default().borders(Borders::ALL).title(title))
        .style(Style::default().fg(Color::White))
        .highlight_style(
            Style::default()
                .fg(Color::Yellow)
                .add_modifier(Modifier::BOLD),
        )
        .select(selected_tab);

    f.render_widget(tabs, area);
}

fn draw_main_content(f: &mut Frame, area: Rect, app: &mut AppState) {
    match app.view_mode {
        ViewMode::Histogram => draw_histogram_view(f, area, app),
        ViewMode::Mappings => draw_mappings_view(f, area, app),
        ViewMode::Chart => draw_chart_view(f, area, app),
        ViewMode::Details => draw_details_view(f, area, app),
    }
}

fn draw_histogram_view(f: &mut Frame, area: Rect, app: &mut AppState) {
    if let Some(stats) = &app.memory_stats {
        let mut histogram_data: Vec<_> = stats.histogram.iter().collect();
        
        // Sort based on current sort criteria
        match app.sort_by {
            SortBy::Count => histogram_data.sort_by(|a, b| b.1.count.cmp(&a.1.count)),
            SortBy::Size => histogram_data.sort_by(|a, b| b.0.0.cmp(&a.0.0)),
            SortBy::Permissions => histogram_data.sort_by(|a, b| a.0.1.cmp(&b.0.1)),
            SortBy::TotalRss => histogram_data.sort_by(|a, b| b.1.total_rss.cmp(&a.1.total_rss)),
            SortBy::AvgRss => {
                histogram_data.sort_by(|a, b| {
                    let avg_a = calculate_average(a.1.total_rss, a.1.count);
                    let avg_b = calculate_average(b.1.total_rss, b.1.count);
                    avg_b.cmp(&avg_a)
                });
            }
            SortBy::TotalPss => histogram_data.sort_by(|a, b| b.1.total_pss.cmp(&a.1.total_pss)),
            SortBy::AvgPss => {
                histogram_data.sort_by(|a, b| {
                    let avg_a = calculate_average(a.1.total_pss, a.1.count);
                    let avg_b = calculate_average(b.1.total_pss, b.1.count);
                    avg_b.cmp(&avg_a)
                });
            }
        }

        let header_cells = vec![
            Cell::from("Size"),
            Cell::from("Perms"),
            Cell::from("Count"),
            Cell::from("Total Size"),
            Cell::from("Total RSS"),
            Cell::from("Avg RSS"),
            Cell::from("Total PSS"),
            Cell::from("Avg PSS"),
        ];
        
        let header = Row::new(header_cells)
            .style(Style::default().fg(Color::Yellow).add_modifier(Modifier::BOLD))
            .height(1);

        let rows: Vec<Row> = histogram_data
            .iter()
            .map(|((size, perms), entry)| {
                let avg_rss = calculate_average(entry.total_rss, entry.count);
                let avg_pss = calculate_average(entry.total_pss, entry.count);
                
                Row::new(vec![
                    Cell::from(format_bytes(*size)),
                    Cell::from(perms.as_str()),
                    Cell::from(entry.count.to_string()),
                    Cell::from(format_bytes(entry.total_size)),
                    Cell::from(format_bytes(entry.total_rss)),
                    Cell::from(format_bytes(avg_rss)),
                    Cell::from(format_bytes(entry.total_pss)),
                    Cell::from(format_bytes(avg_pss)),
                ])
            })
            .collect();

        let widths = [
            Constraint::Length(12),
            Constraint::Length(8),
            Constraint::Length(8),
            Constraint::Length(12),
            Constraint::Length(12),
            Constraint::Length(12),
            Constraint::Length(12),
            Constraint::Length(12),
        ];

        let table = Table::new(rows, widths)
            .header(header)
            .block(
                Block::default()
                    .borders(Borders::ALL)
                    .title(format!("Memory Histogram (Sort: {:?})", app.sort_by)),
            )
            .highlight_style(
                Style::default()
                    .bg(Color::DarkGray)
                    .add_modifier(Modifier::BOLD),
            );

        f.render_stateful_widget(table, area, &mut app.table_state);
    } else {
        let paragraph = Paragraph::new("No data loaded")
            .block(Block::default().borders(Borders::ALL).title("Memory Histogram"))
            .alignment(Alignment::Center);
        f.render_widget(paragraph, area);
    }
}

fn draw_mappings_view(f: &mut Frame, area: Rect, app: &mut AppState) {
    if let Some(stats) = &app.memory_stats {
        let header_cells = vec![
            Cell::from("Start"),
            Cell::from("End"),
            Cell::from("Size"),
            Cell::from("Perms"),
            Cell::from("RSS"),
            Cell::from("PSS"),
            Cell::from("Path"),
        ];
        
        let header = Row::new(header_cells)
            .style(Style::default().fg(Color::Yellow).add_modifier(Modifier::BOLD))
            .height(1);

        let rows: Vec<Row> = stats
            .mappings
            .iter()
            .map(|mapping| {
                Row::new(vec![
                    Cell::from(format!("{:016x}", mapping.start_addr)),
                    Cell::from(format!("{:016x}", mapping.end_addr)),
                    Cell::from(format_bytes(mapping.size)),
                    Cell::from(mapping.permissions.as_str()),
                    Cell::from(mapping.rss.map_or("N/A".to_string(), format_bytes)),
                    Cell::from(mapping.pss.map_or("N/A".to_string(), format_bytes)),
                    Cell::from(mapping.path.as_deref().unwrap_or("")),
                ])
            })
            .collect();

        let widths = [
            Constraint::Length(18),
            Constraint::Length(18),
            Constraint::Length(12),
            Constraint::Length(8),
            Constraint::Length(12),
            Constraint::Length(12),
            Constraint::Min(20),
        ];

        let table = Table::new(rows, widths)
            .header(header)
            .block(
                Block::default()
                    .borders(Borders::ALL)
                    .title("Memory Mappings"),
            )
            .highlight_style(
                Style::default()
                    .bg(Color::DarkGray)
                    .add_modifier(Modifier::BOLD),
            );

        f.render_stateful_widget(table, area, &mut app.table_state);
    } else {
        let paragraph = Paragraph::new("No data loaded")
            .block(Block::default().borders(Borders::ALL).title("Memory Mappings"))
            .alignment(Alignment::Center);
        f.render_widget(paragraph, area);
    }
}

fn draw_chart_view(f: &mut Frame, area: Rect, app: &AppState) {
    if let Some(stats) = &app.memory_stats {
        let size_ranges = vec![
            ("< 4KB", 0, SIZE_4KB),
            ("4KB-64KB", SIZE_4KB, SIZE_64KB),
            ("64KB-1MB", SIZE_64KB, SIZE_1MB),
            ("1MB-16MB", SIZE_1MB, SIZE_16MB),
            ("16MB-256MB", SIZE_16MB, SIZE_256MB),
            (">= 256MB", SIZE_256MB, u64::MAX),
        ];

        let mut data_vec: Vec<(String, u64)> = Vec::new();
        let title = match app.chart_metric {
            ChartMetric::Size => {
                for (label, min_size, max_size) in &size_ranges {
                    let total_size: u64 = stats.mappings
                        .iter()
                        .filter(|m| m.size >= *min_size && m.size < *max_size)
                        .map(|m| m.size)
                        .sum();
                    if total_size > 0 {
                        data_vec.push((label.to_string(), total_size / BYTES_PER_KB / BYTES_PER_KB));
                    }
                }
                "Memory Usage by Size Range (MB)"
            },
            ChartMetric::Count => {
                for (label, min_size, max_size) in &size_ranges {
                    let count: u64 = stats.mappings
                        .iter()
                        .filter(|m| m.size >= *min_size && m.size < *max_size)
                        .count() as u64;
                    if count > 0 {
                        data_vec.push((label.to_string(), count));
                    }
                }
                "Mapping Count by Size Range"
            },
            ChartMetric::TotalSize => {
                for (label, min_size, max_size) in &size_ranges {
                    let total_size: u64 = stats.mappings
                        .iter()
                        .filter(|m| m.size >= *min_size && m.size < *max_size)
                        .map(|m| m.size)
                        .sum();
                    if total_size > 0 {
                        data_vec.push((label.to_string(), total_size / BYTES_PER_KB / BYTES_PER_KB));
                    }
                }
                "Total Size by Range (MB)"
            },
            ChartMetric::TotalRss => {
                for (label, min_size, max_size) in &size_ranges {
                    let total_rss: u64 = stats.mappings
                        .iter()
                        .filter(|m| m.size >= *min_size && m.size < *max_size)
                        .map(|m| m.rss.unwrap_or(0))
                        .sum();
                    if total_rss > 0 {
                        data_vec.push((label.to_string(), total_rss / BYTES_PER_KB));
                    }
                }
                "Total RSS by Size Range (KB)"
            },
            ChartMetric::AverageRss => {
                for (label, min_size, max_size) in &size_ranges {
                    let mappings_in_range: Vec<_> = stats.mappings
                        .iter()
                        .filter(|m| m.size >= *min_size && m.size < *max_size)
                        .collect();
                    if !mappings_in_range.is_empty() {
                        let total_rss: u64 = mappings_in_range
                            .iter()
                            .map(|m| m.rss.unwrap_or(0))
                            .sum();
                        let avg_rss = total_rss / mappings_in_range.len() as u64;
                        if avg_rss > 0 {
                            data_vec.push((label.to_string(), avg_rss / BYTES_PER_KB));
                        }
                    }
                }
                "Average RSS by Size Range (KB)"
            },
        };

        data_vec.sort_by(|a, b| b.1.cmp(&a.1));
        let bars: Vec<_> = data_vec
            .iter()
            .take(TOP_CHART_RANGES)
            .map(|(label, value)| (label.as_str(), *value))
            .collect();

        let chart = BarChart::default()
            .block(
                Block::default()
                    .borders(Borders::ALL)
                    .title(format!("{} (Press 'm' to switch metrics)", title)),
            )
            .data(&bars)
            .bar_width(CHART_BAR_WIDTH)
            .bar_style(Style::default().fg(Color::Green))
            .value_style(Style::default().fg(Color::Yellow));

        f.render_widget(chart, area);
    } else {
        let paragraph = Paragraph::new("No data loaded")
            .block(Block::default().borders(Borders::ALL).title("Memory Chart"))
            .alignment(Alignment::Center);
        f.render_widget(paragraph, area);
    }
}

fn draw_details_view(f: &mut Frame, area: Rect, app: &AppState) {
    if let Some(stats) = &app.memory_stats {
        let chunks = Layout::default()
            .direction(Direction::Vertical)
            .constraints([
                Constraint::Length(6), // Summary
                Constraint::Min(0),    // Detailed info
            ])
            .split(area);

        // Summary section
        let mut summary_text = Vec::new();
        
        // Add process information if available
        if let Some(pid) = stats.pid {
            summary_text.push(Line::from(vec![
                Span::styled("Process ID: ", Style::default().fg(Color::Cyan)),
                Span::raw(format!("{}", pid)),
            ]));
            
            if let Some(name) = &stats.process_name {
                summary_text.push(Line::from(vec![
                    Span::styled("Process Name: ", Style::default().fg(Color::Cyan)),
                    Span::raw(name.clone()),
                ]));
            }
            summary_text.push(Line::from(""));
        }
        
        summary_text.extend(vec![
            Line::from(vec![
                Span::styled("Total Mappings: ", Style::default().fg(Color::Yellow)),
                Span::raw(format!("{}", stats.mappings.len())),
            ]),
            Line::from(vec![
                Span::styled("Total Virtual Memory: ", Style::default().fg(Color::Yellow)),
                Span::raw(format_bytes(stats.total_virtual)),
            ]),
            Line::from(vec![
                Span::styled("Total RSS: ", Style::default().fg(Color::Yellow)),
                Span::raw(format_bytes(stats.total_rss)),
            ]),
            Line::from(vec![
                Span::styled("Total PSS: ", Style::default().fg(Color::Yellow)),
                Span::raw(format_bytes(stats.total_pss)),
            ]),
        ]);

        let summary = Paragraph::new(summary_text)
            .block(Block::default().borders(Borders::ALL).title("Summary"));
        f.render_widget(summary, chunks[0]);

        // Detailed breakdown
        if let Some(selected_idx) = app.selected_mapping {
            if let Some(mapping) = stats.mappings.get(selected_idx) {
                let detail_text = vec![
                    Line::from(vec![
                        Span::styled("Selected Mapping Details:", Style::default().fg(Color::Cyan).add_modifier(Modifier::BOLD)),
                    ]),
                    Line::from(vec![
                        Span::styled("Address Range: ", Style::default().fg(Color::Yellow)),
                        Span::raw(format!("{:016x}-{:016x}", mapping.start_addr, mapping.end_addr)),
                    ]),
                    Line::from(vec![
                        Span::styled("Size: ", Style::default().fg(Color::Yellow)),
                        Span::raw(format_bytes(mapping.size)),
                    ]),
                    Line::from(vec![
                        Span::styled("Permissions: ", Style::default().fg(Color::Yellow)),
                        Span::raw(&mapping.permissions),
                    ]),
                    Line::from(vec![
                        Span::styled("RSS: ", Style::default().fg(Color::Yellow)),
                        Span::raw(mapping.rss.map_or("N/A".to_string(), format_bytes)),
                    ]),
                    Line::from(vec![
                        Span::styled("PSS: ", Style::default().fg(Color::Yellow)),
                        Span::raw(mapping.pss.map_or("N/A".to_string(), format_bytes)),
                    ]),
                    Line::from(vec![
                        Span::styled("Path: ", Style::default().fg(Color::Yellow)),
                        Span::raw(mapping.path.as_deref().unwrap_or("N/A")),
                    ]),
                ];

                let details = Paragraph::new(detail_text)
                    .block(Block::default().borders(Borders::ALL).title("Details"));
                f.render_widget(details, chunks[1]);
            }
        } else {
            let paragraph = Paragraph::new("Select a mapping to view details")
                .block(Block::default().borders(Borders::ALL).title("Details"))
                .alignment(Alignment::Center);
            f.render_widget(paragraph, chunks[1]);
        }
    } else {
        let paragraph = Paragraph::new("No data loaded")
            .block(Block::default().borders(Borders::ALL).title("Details"))
            .alignment(Alignment::Center);
        f.render_widget(paragraph, area);
    }
}

fn draw_footer(f: &mut Frame, area: Rect, app: &AppState) {
    let help_text = if app.status_message.is_empty() {
        "Press 'h' for help, 'q' to quit, 'r' to reload, '1-4' to switch views, 's' to change sort"
    } else {
        &app.status_message
    };

    let paragraph = Paragraph::new(help_text)
        .block(Block::default().borders(Borders::ALL))
        .alignment(Alignment::Center);
    f.render_widget(paragraph, area);
}

fn draw_help_popup(f: &mut Frame, area: Rect) {
    let popup_area = centered_rect(80, 60, area);
    
    f.render_widget(Clear, popup_area);
    
    let help_text = vec![
        Line::from(vec![
            Span::styled("SMAPS Spy TUI - Help", Style::default().fg(Color::Cyan).add_modifier(Modifier::BOLD)),
        ]),
        Line::from(""),
        Line::from("Navigation:"),
        Line::from("  ↑/↓ or j/k - Navigate table rows"),
        Line::from("  1-4        - Switch between views"),
        Line::from("  Tab        - Next view"),
        Line::from(""),
        Line::from("Sorting (Histogram view):"),
        Line::from("  s          - Cycle through sort options"),
        Line::from(""),
        Line::from("Other:"),
        Line::from("  r          - Reload file"),
        Line::from("  h          - Toggle this help"),
        Line::from("  q/Esc      - Quit"),
        Line::from(""),
        Line::from("Views:"),
        Line::from("  1 - Histogram: Memory usage grouped by size/permissions"),
        Line::from("  2 - Mappings: Individual memory mappings"),
        Line::from("  3 - Chart: Visual representation of memory usage"),
        Line::from("  4 - Details: Summary and selected mapping details"),
        Line::from(""),
        Line::from("Press 'h' or Esc to close this help"),
    ];

    let help = Paragraph::new(help_text)
        .block(
            Block::default()
                .borders(Borders::ALL)
                .title("Help")
                .style(Style::default().fg(Color::White)),
        )
        .alignment(Alignment::Left);
    
    f.render_widget(help, popup_area);
}

fn centered_rect(percent_x: u16, percent_y: u16, r: Rect) -> Rect {
    let popup_layout = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Percentage((100 - percent_y) / 2),
            Constraint::Percentage(percent_y),
            Constraint::Percentage((100 - percent_y) / 2),
        ])
        .split(r);

    Layout::default()
        .direction(Direction::Horizontal)
        .constraints([
            Constraint::Percentage((100 - percent_x) / 2),
            Constraint::Percentage(percent_x),
            Constraint::Percentage((100 - percent_x) / 2),
        ])
        .split(popup_layout[1])[1]
}

