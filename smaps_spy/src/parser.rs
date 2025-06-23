use anyhow::{Context, Result};
use crate::utils::{MAX_LINES_AFTER_MAPPING, BYTES_PER_KB};
use std::collections::HashMap;
use std::fs::File;
use std::io::{BufRead, BufReader};
use std::path::Path;

#[derive(Debug, Clone)]
pub struct MemoryMapping {
    pub start_addr: u64,
    pub end_addr: u64,
    pub size: u64,
    pub permissions: String,
    pub rss: Option<u64>,
    pub pss: Option<u64>,
    pub path: Option<String>,
}

#[derive(Debug, Clone)]
pub struct MemoryStats {
    pub mappings: Vec<MemoryMapping>,
    pub histogram: HashMap<(u64, String), HistogramEntry>,
    pub total_virtual: u64,
    pub total_rss: u64,
    pub total_pss: u64,
    pub process_name: Option<String>,
    pub pid: Option<u32>,
}

#[derive(Debug, Clone)]
pub struct HistogramEntry {
    pub count: u32,
    pub total_size: u64,
    pub total_rss: u64,
    pub total_pss: u64,
}

impl MemoryStats {
    pub fn new() -> Self {
        Self {
            mappings: Vec::new(),
            histogram: HashMap::new(),
            total_virtual: 0,
            total_rss: 0,
            total_pss: 0,
            process_name: None,
            pid: None,
        }
    }

    pub fn build_histogram(&mut self) {
        self.histogram.clear();
        self.total_virtual = 0;
        self.total_rss = 0;
        self.total_pss = 0;

        for mapping in &self.mappings {
            let key = (mapping.size, mapping.permissions.clone());
            let entry = self.histogram.entry(key).or_insert(HistogramEntry {
                count: 0,
                total_size: 0,
                total_rss: 0,
                total_pss: 0,
            });

            entry.count += 1;
            entry.total_size += mapping.size;
            if let Some(rss) = mapping.rss {
                entry.total_rss += rss;
            }
            if let Some(pss) = mapping.pss {
                entry.total_pss += pss;
            }

            self.total_virtual += mapping.size;
            if let Some(rss) = mapping.rss {
                self.total_rss += rss;
            }
            if let Some(pss) = mapping.pss {
                self.total_pss += pss;
            }
        }
    }
}

pub fn detect_file_type<P: AsRef<Path>>(file_path: P) -> Result<FileType> {
    let file = File::open(&file_path)
        .with_context(|| format!("Failed to open file: {:?}", file_path.as_ref()))?;
    let reader = BufReader::new(file);

    let mut found_mapping = false;
    let mut lines_after_mapping = 0;
    
    for line in reader.lines() {
        let line = line?;
        
        if line.starts_with("Rss:") {
            return Ok(FileType::Smaps);
        }
        
        if line.contains('-') && line.split_whitespace().count() >= 2 {
            // Found a mapping line
            found_mapping = true;
            lines_after_mapping = 0;
        } else if found_mapping {
            lines_after_mapping += 1;
            // Look for RSS within the next 20 lines after a mapping
            if lines_after_mapping > MAX_LINES_AFTER_MAPPING {
                break;
            }
        }
    }
    Ok(FileType::Maps)
}

#[derive(Debug, PartialEq)]
pub enum FileType {
    Maps,
    Smaps,
}

pub fn parse_maps_file<P: AsRef<Path>>(file_path: P) -> Result<MemoryStats> {
    let file = File::open(&file_path)
        .with_context(|| format!("Failed to open maps file: {:?}", file_path.as_ref()))?;
    let reader = BufReader::new(file);

    let mut stats = MemoryStats::new();

    for line in reader.lines() {
        let line = line?;
        let parts: Vec<&str> = line.split_whitespace().collect();
        
        if parts.len() < 2 {
            continue;
        }

        let address_range = parts[0];
        let permissions = parts[1].into();

        if !address_range.contains('-') {
            continue;
        }

        let addr_parts: Vec<&str> = address_range.split('-').collect();
        if addr_parts.len() != 2 {
            continue;
        }

        let start_addr = u64::from_str_radix(addr_parts[0], 16)
            .with_context(|| format!("Failed to parse start address: {}", addr_parts[0]))?;
        let end_addr = u64::from_str_radix(addr_parts[1], 16)
            .with_context(|| format!("Failed to parse end address: {}", addr_parts[1]))?;

        let size = end_addr - start_addr;
        let path = if parts.len() > 5 { Some(parts[5..].join(" ")) } else { None };

        stats.mappings.push(MemoryMapping {
            start_addr,
            end_addr,
            size,
            permissions,
            rss: None,
            pss: None,
            path,
        });
    }

    stats.build_histogram();
    Ok(stats)
}

pub fn parse_smaps_file<P: AsRef<Path>>(file_path: P) -> Result<MemoryStats> {
    let file = File::open(&file_path)
        .with_context(|| format!("Failed to open smaps file: {:?}", file_path.as_ref()))?;
    let reader = BufReader::new(file);

    let mut stats = MemoryStats::new();
    let mut current_mapping: Option<MemoryMapping> = None;

    for line in reader.lines() {
        let line = line?;
        let line = line.trim();

        if line.is_empty() {
            continue;
        }

        let parts: Vec<&str> = line.split_whitespace().collect();

        // Check if this is a mapping line
        if parts.len() >= 2 && parts[0].contains('-') {
            // Save previous mapping if it exists
            if let Some(mapping) = current_mapping.take() {
                stats.mappings.push(mapping);
            }

            let address_range = parts[0];
            let permissions = parts[1].into();

            let addr_parts: Vec<&str> = address_range.split('-').collect();
            if addr_parts.len() != 2 {
                continue;
            }

            let start_addr = u64::from_str_radix(addr_parts[0], 16)
                .with_context(|| format!("Failed to parse start address: {}", addr_parts[0]))?;
            let end_addr = u64::from_str_radix(addr_parts[1], 16)
                .with_context(|| format!("Failed to parse end address: {}", addr_parts[1]))?;

            let size = end_addr - start_addr;
            let path = if parts.len() > 5 { Some(parts[5..].join(" ")) } else { None };

            current_mapping = Some(MemoryMapping {
                start_addr,
                end_addr,
                size,
                permissions,
                rss: None,
                pss: None,
                path,
            });
        } else if let Some(ref mut mapping) = current_mapping {
            // Parse memory statistics lines
            if line.starts_with("Rss:") && parts.len() >= 2 {
                if let Ok(rss_kb) = parts[1].parse::<u64>() {
                    mapping.rss = Some(rss_kb * BYTES_PER_KB); // Convert to bytes
                }
            } else if line.starts_with("Pss:") && parts.len() >= 2 {
                if let Ok(pss_kb) = parts[1].parse::<u64>() {
                    mapping.pss = Some(pss_kb * BYTES_PER_KB); // Convert to bytes
                }
            }
        }
    }

    // Don't forget the last mapping
    if let Some(mapping) = current_mapping {
        stats.mappings.push(mapping);
    }

    stats.build_histogram();
    Ok(stats)
}

pub fn parse_file<P: AsRef<Path>>(file_path: P) -> Result<MemoryStats> {
    let file_type = detect_file_type(&file_path)?;
    
    let mut stats = match file_type {
        FileType::Maps => parse_maps_file(&file_path)?,
        FileType::Smaps => parse_smaps_file(&file_path)?,
    };
    
    // Try to extract PID and process name from path
    if let Some(path_str) = file_path.as_ref().to_str() {
        if path_str.starts_with("/proc/") {
            if let Some(pid_str) = path_str.strip_prefix("/proc/").and_then(|s| s.split('/').next()) {
                if let Ok(pid) = pid_str.parse::<u32>() {
                    stats.pid = Some(pid);
                    stats.process_name = get_process_name(pid).ok();
                }
            }
        }
    }
    
    Ok(stats)
}

fn get_process_name(pid: u32) -> Result<String> {
    let comm_path = format!("/proc/{}/comm", pid);
    let contents = std::fs::read_to_string(&comm_path)
        .with_context(|| format!("Failed to read process name from {}", comm_path))?;
    Ok(contents.trim().to_string())
}