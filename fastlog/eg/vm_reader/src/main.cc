#include <cstddef>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "libmmlog.h"

int main()
{
    const std::string filename = "test.log";
    const size_t chunk_size = 4 * 4096;  // 16KB chunks

    // Test messages
    std::vector<std::string> messages = {
        "Hello, MMLOG!",
        "This is a test message.",
        "Another message to verify storage.",
        "Short",
        "This is a longer message that should span more bytes to test larger payloads in the system.",
        "Final test message"};

    // Open log and write messages
    std::cout << "Writing messages to log...\n";
    log_handle_t* handle = mmlog_open(filename.c_str(), chunk_size);
    if (!handle) {
        std::cerr << "Failed to open log" << std::endl;
        return 1;
    }

    for (const auto& msg : messages) {
        if (!mmlog_insert(handle, msg.c_str(), msg.length())) {
            std::cerr << "Failed to insert" << std::endl;
            free(handle);
            return 1;
        }
        if (!mmlog_insert(handle, "\n", 1)) {
            std::cerr << "Failed to insert newline" << std::endl;
            free(handle);
            return 1;
        }
        std::cout << "Wrote: " << msg << std::endl;
    }

    // Clean up and close
    free(handle);

    // Read file back and validate
    std::cout << "\nValidating messages in log...\n";
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open file for reading\n";
        return 1;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    int found = 0;
    for (const auto& msg : messages) {
        if (content.find(msg) != std::string::npos) {
            std::cout << "Found: " << msg << std::endl;
            found++;
        } else {
            std::cout << "MISSING: " << msg << std::endl;
        }
    }

    std::cout << "\nResult: " << found << "/" << messages.size() << " messages validated" << std::endl;

    return (found == messages.size()) ? 0 : 1;
}
