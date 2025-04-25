#include <ranges>
#include <iostream>
#include <string_view>
#include <string>

int main() {
  std::string s = "key1=val1;key2=val2; key3=val3 ; key4=val4 ;; key5==val5";

  auto range_to_trimmed_sv = [](auto &&rng) {
    char *start = &*rng.begin();
    auto end_finder = rng.begin();
    while (end_finder != rng.end())
        ++end_finder;
    char *end = &*end_finder;
    while (isspace(*start))
      ++start;
    while (isspace(*end))
      --end;
    return std::string_view(start, end);
  };
  auto view = s
      | std::ranges::views::split(';')
      | std::ranges::views::transform(range_to_trimmed_sv);


  for (const auto &el : view) {
    if (el.empty()) {
      std::cout << "Empty!" << std::endl;
    } else {
      std::cout << el << std::endl;
    }
  }
}
