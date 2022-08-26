#include <array>
#include <vector>
#include <iostream>

using namespace std::literals;

int main() {
  static constexpr std::array lu{"one"sv, "two"sv, "three"sv}; // hint: static constexpr doesn't matter for the issue we're looking at 
  const std::basic_string_view<char> &val = "two";
  if (std::find(lu.begin(), lu.end(), val) != lu.end())
    std::cout << "Found it!" << std::endl;
  else
    std::cout << "Did not find it." << std::endl;
  return 0;
}
