#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

std::string file_from_addr(void* addr) {
  std::ifstream maps("/proc/self/maps");
  std::string line;
  while (std::getline(maps, line)) {
    auto pos = line.find('-');
    if (pos != std::string::npos) {
      std::uintptr_t start_addr = std::stoull(line.substr(0, pos), nullptr, 16);
      std::uintptr_t end_addr = std::stoull(line.substr(pos + 1), nullptr, 16);

      if (reinterpret_cast<std::uintptr_t>(addr) >= start_addr && reinterpret_cast<std::uintptr_t>(addr) < end_addr) {
        auto last_space = line.rfind(' ');
        if (last_space != std::string::npos) {
          return line.substr(last_space + 1);
        }
      }
    }
  }
  return "";
}

int fingerprint = 0;
int main() {
  std::cout << file_from_addr(&fingerprint) << std::endl;
}
