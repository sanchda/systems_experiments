#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

// This function reads /proc/maps and returns the total addressable memory
size_t get_addressable() {
  std::ifstream maps("/proc/self/maps");
  std::string line;
  size_t total = 0;
  while (std::getline(maps, line)) {
    std::istringstream iss(line);
    size_t start, end;
    char dash;
    iss >> std::hex >> start >> dash >> end;
    total += end - start;
  }

  return total / 1024;
}

// Reads entries from /proc/self/smaps_rollup
// Desired lines are given as an unordered set of strings
std::unordered_map<std::string, size_t> read_smaps(const std::unordered_set<std::string> &names) {
  std::ifstream smaps("/proc/self/smaps_rollup");
  std::string line;
  std::unordered_map<std::string, size_t> result;
  // Initial sweep to populate with defaults
  while (std::getline(smaps, line)) {
    for (auto &name : names) {
      if (line.find(name) != std::string::npos) {
        std::istringstream iss(line);
        std::string rss;
        size_t value;
        iss >> rss >> value;
        result.insert({name, value});
      }
    }
  }

  return result;
}

// Prints the memory usage of the current process
// Includes the overhead of buffering the data to print
void print_mem(std::string_view msg) {
  std::cout << "[" << msg << "] " << "maps: " << get_addressable() << std::endl;
  for (auto &entry : read_smaps({"Rss:", "Pss:"})) {
    std::cout << "[" << msg << "] " << entry.first << " " << entry.second << std::endl;
  }
}

#include <vector>
int main() {
  print_mem("Initial");
  auto foo = std::vector<int>(1e6, 1);
  print_mem("Step 1");
  return 0;
}
