#include <iostream>
#include <cstddef>
#include <vector>
#include <array>
#include <functional>
#include <iterator>

const std::size_t num_allocations = 500000; // Trials
std::vector<char*> pointers;                // For cleanup
std::array<int, 32> counts;                 // Histogram

void test_allocation(std::function<char*()> allocator, 
                     std::function<void(char*)> deallocator, 
                     const std::string& label) {
  for (auto i = 0u; i < num_allocations; ++i) {
    char* ptr = allocator();
    pointers.push_back(ptr);
    ++counts[reinterpret_cast<std::uintptr_t>(ptr) % size(counts)];
  }
  
  for (std::size_t offset = 0; offset < counts.size(); ++offset) {
      auto freq = counts[offset];
      if (freq)
        std::cout << offset << "-byte offset (" << label << "): " << freq << std::endl;
  }

  for (auto ptr : pointers) 
    deallocator(ptr);
  
  pointers.clear();
  std::fill(begin(counts), end(counts), 0);
}

int main() {
  test_allocation([]() { return static_cast<char*>(operator new(sizeof(char))); },
                  [](char* ptr) { operator delete(ptr); },
                  "cpp");

  std::cout << std::endl; // meh
  test_allocation([]() { return static_cast<char*>(malloc(sizeof(char))); },
                  [](char* ptr) { free(ptr); },
                  "malloc");
  return 0;
}

