use smaps_spy_tui::parser::{parse_file, FileType, detect_file_type};

#[test]
fn test_file_type_detection() {
    // Test with smaps test file
    let file_type = detect_file_type("data/smaps.test.txt").expect("Should detect file type");
    assert_eq!(file_type, FileType::Smaps);
    
    // Test with maps test file
    let file_type = detect_file_type("data/maps.test.txt").expect("Should detect file type");
    assert_eq!(file_type, FileType::Maps);
}

#[test]
fn test_smaps_parsing_basic() {
    let stats = parse_file("data/smaps.test.txt").expect("Should parse smaps test file successfully");
    
    // Basic sanity checks
    assert!(stats.mappings.len() > 0, "Should have mappings");
    assert!(stats.total_virtual > 0, "Should have virtual memory");
    
    println!("Total mappings: {}", stats.mappings.len());
    println!("Total virtual: {} bytes", stats.total_virtual);
    println!("Total RSS: {} bytes", stats.total_rss);
    println!("Total PSS: {} bytes", stats.total_pss);
}

#[test]
fn test_smaps_rss_pss_data() {
    let stats = parse_file("data/smaps.test.txt").expect("Should parse smaps test file successfully");
    
    // We know smaps test file has RSS/PSS data, so these should be > 0
    assert!(stats.total_rss > 0, "Total RSS should be greater than 0, got: {}", stats.total_rss);
    assert!(stats.total_pss > 0, "Total PSS should be greater than 0, got: {}", stats.total_pss);
    
    // Check that at least some mappings have RSS/PSS data
    let mappings_with_rss = stats.mappings.iter().filter(|m| m.rss.is_some()).count();
    let mappings_with_pss = stats.mappings.iter().filter(|m| m.pss.is_some()).count();
    
    assert!(mappings_with_rss > 0, "Should have mappings with RSS data");
    assert!(mappings_with_pss > 0, "Should have mappings with PSS data");
    
    println!("Mappings with RSS: {}/{}", mappings_with_rss, stats.mappings.len());
    println!("Mappings with PSS: {}/{}", mappings_with_pss, stats.mappings.len());
}

#[test]
fn test_histogram_has_rss_pss() {
    let stats = parse_file("data/smaps.test.txt").expect("Should parse smaps test file successfully");
    
    // Check histogram entries have RSS/PSS totals
    let histogram_entries_with_rss = stats.histogram.values().filter(|e| e.total_rss > 0).count();
    let histogram_entries_with_pss = stats.histogram.values().filter(|e| e.total_pss > 0).count();
    
    assert!(histogram_entries_with_rss > 0, "Histogram should have entries with RSS data");
    assert!(histogram_entries_with_pss > 0, "Histogram should have entries with PSS data");
    
    println!("Histogram entries with RSS: {}/{}", histogram_entries_with_rss, stats.histogram.len());
    println!("Histogram entries with PSS: {}/{}", histogram_entries_with_pss, stats.histogram.len());
}

#[test]
fn test_first_few_mappings() {
    let stats = parse_file("data/smaps.test.txt").expect("Should parse smaps test file successfully");
    
    // Look at the first few mappings to see what we're getting
    for (i, mapping) in stats.mappings.iter().take(5).enumerate() {
        println!("Mapping {}: {:016x}-{:016x} {} size={} RSS={:?} PSS={:?}", 
                 i, mapping.start_addr, mapping.end_addr, mapping.permissions, 
                 mapping.size, mapping.rss, mapping.pss);
    }
    
    // At least the first mapping should have RSS/PSS data based on our test file
    let first_mapping = &stats.mappings[0];
    assert!(first_mapping.rss.is_some(), "First mapping should have RSS data");
    assert!(first_mapping.pss.is_some(), "First mapping should have PSS data");
}

#[test]
fn test_maps_parsing() {
    let stats = parse_file("data/maps.test.txt").expect("Should parse maps test file successfully");
    
    // Basic sanity checks for maps format
    assert!(stats.mappings.len() > 0, "Should have mappings");
    assert!(stats.total_virtual > 0, "Should have virtual memory");
    
    // Maps format should NOT have RSS/PSS data
    assert_eq!(stats.total_rss, 0, "Maps format should not have RSS data");
    assert_eq!(stats.total_pss, 0, "Maps format should not have PSS data");
    
    // Check that mappings don't have RSS/PSS data
    let mappings_with_rss = stats.mappings.iter().filter(|m| m.rss.is_some()).count();
    let mappings_with_pss = stats.mappings.iter().filter(|m| m.pss.is_some()).count();
    
    assert_eq!(mappings_with_rss, 0, "Maps format should not have mappings with RSS data");
    assert_eq!(mappings_with_pss, 0, "Maps format should not have mappings with PSS data");
    
    println!("Maps format - Total mappings: {}", stats.mappings.len());
    println!("Maps format - Total virtual: {} bytes", stats.total_virtual);
}