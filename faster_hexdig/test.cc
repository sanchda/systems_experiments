#include <algorithm>
#include <iostream>
#include <string>
#include <cstring>
#include <cctype>
#include <x86intrin.h>

bool lower_cond(char c) {
  return (c >= 'a' && c <= 'f') || (c >= '0' && c <= '9');
}

bool lower_ism(char c) {
  return isxdigit(c) && !isupper(c);
}


bool lookup[256];
void init_lu() {
  for (size_t i = 0; i < 256; ++i)
    lookup[i] = lower_cond(i);
}
bool lower_lu(unsigned char c) {
  return c < 256 && lookup[c];
}

class BenchState {
  size_t count_cond = 0;
  size_t count_ism = 0;
  size_t count_lu = 0;
  uint64_t cycles_cond = 0;
  uint64_t cycles_ism = 0;
  uint64_t cycles_lu = 0;
  size_t trials_ran = 0;
  size_t characters_processed = 0;

public:
  void run(const std::string_view &sv) {
    // Warmup
    for (const auto &c : sv)
      count_cond += lower_cond(c);
    for (const auto &c : sv)
      count_cond += lower_cond(c);

    // Trial
    size_t cycles = __rdtsc();
    for (const auto &c : sv)
      count_cond += lower_cond(c);
    cycles_cond += __rdtsc() - cycles;


    // Warmup
    for (const auto &c : sv)
      count_ism += lower_ism(c);
    for (const auto &c : sv)
      count_ism += lower_ism(c);

    // Trial
    cycles = __rdtsc();
    for (const auto &c : sv)
      count_ism += lower_ism(c);
    cycles_ism += __rdtsc() - cycles;

    // Warmup
    for (const auto &c : sv)
      count_lu += lower_lu(c);
    for (const auto &c : sv)
      count_lu += lower_lu(c);

    // Trial
    cycles = __rdtsc();
    for (const auto &c : sv)
      count_lu += lower_lu(c);
    cycles_lu += __rdtsc() - cycles;

    trials_ran++;
    characters_processed += sv.size();

    // Check
    if (count_cond != count_ism || count_cond != count_lu) {
      std::cout << "!!! " << sv << " !!!  broke the test" << std::endl;
      std::exit(-1);
    }
  }

  void print() {
    std::cout << "Trials: " << trials_ran << std::endl
              << "Characters: " << characters_processed << std::endl
              << "cond: " << cycles_cond << std::endl
              << "ism:  " << cycles_ism << std::endl
              << "lu:  " << cycles_lu << std::endl;

    uint64_t cycles_min = std::min({cycles_cond, cycles_ism, cycles_lu});

    std::cout << "cond was " << 100.0 * (cycles_cond - cycles_min) / cycles_cond << "\% slower" << std::endl;
    std::cout << "ism was " << 100.0 * (cycles_ism - cycles_min) / cycles_ism << "\% slower" << std::endl;
    std::cout << "lu was " << 100.0 * (cycles_lu - cycles_min) / cycles_lu << "\% slower" << std::endl;
  }
};

int main(int c, char **V) {
  init_lu();
  BenchState bench{};
  if (c > 1) {
    bench.run(V[1]);
  } else {
    std::string line;
    while (std::getline(std::cin, line))
      bench.run(line);
  }
  bench.print();
  return 0;
}
