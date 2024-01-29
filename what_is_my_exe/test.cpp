#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

std::string file_from_addr(void* addr) {
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line)) {
        std::istringstream iss(line);
        std::string addr_str;
        iss >> addr_str;
        if (addr_str.find('-') != std::string::npos) {
            std::istringstream addr_iss(addr_str);
            void* start_addr;
            void* end_addr;
            addr_iss >> start_addr;
            addr_iss.ignore();
            addr_iss >> end_addr;
            if (addr >= start_addr && addr <= end_addr) {
              std::string path;
              std::string token;
              while (iss >> token)
                path = token;
              return path;
            }
        }
    }
    return "";
}

int fingerprint = 0;
int main() {
  std::cout << file_from_addr(&fingerprint) << std::endl;
}
