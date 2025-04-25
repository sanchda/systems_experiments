#include <array>
//#include <algorithm> // put this back to fix it
#include <vector>
#include <iostream>

using namespace std::literals;

int main() {
//  static constexpr std::array lu{"one"sv, "two"sv, "three"sv};
  std::array<std::string, 3> lu{"one", "two", "three"};
  std::string val{"two"};
  if (std::find(lu.begin(), lu.end(), val) != lu.end())
    std::cout << "Found it!" << std::endl;
  else
    std::cout << "Did not find it." << std::endl;
  return 0;
}
