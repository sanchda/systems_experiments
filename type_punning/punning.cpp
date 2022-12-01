#include <stdio.h>

#include <tuple>
#include <array>
#include <string>

using ControlCode = std::tuple<unsigned char[8], size_t>;
using ControlValue = std::tuple<unsigned long, size_t>;
using ControlCodes = std::array<ControlCode, 13>;
using ControlLookup = std::array<ControlValue, 13>;
using ControlMask = std::array<unsigned long, 9>;
constexpr ControlCodes init_lookup_pre() {
  constexpr unsigned char simple_codes[][8] = {
    "$SP$",
    "$BP$",
    "$RF$",
    "$LT$",
    "$GT$",
    "$RP$",
  };
  constexpr unsigned char uni_codes[][8] = {
    "$u20$",
    "$u27$",
    "$u5b$",
    "$u5d$",
    "$u7b$",
    "$u7d$",
    "$u7e$",
  };
  ControlCodes codes;
  size_t i = 0;
  for (const auto& code : simple_codes) {
    std::copy(std::begin(code), std::end(code), std::begin(std::get<0>(codes[i])));
    std::get<1>(codes[i]) = 4;
    ++i;
  }
  for (const auto& code : uni_codes) {
    std::copy(std::begin(code), std::end(code), std::begin(std::get<0>(codes[i])));
    std::get<1>(codes[i]) = 5;
    ++i;
  }
  return codes;
}

// constexpr cannot admit UB, so things like `reinterpret_cast<>()` are not
// allowed.  Instead settle for static.
ControlLookup init_lookup() {
  auto codes = init_lookup_pre();
  ControlLookup lu;
  for (size_t i = 0; i < lu.size(); ++i) {
    std::get<0>(lu[i]) = *reinterpret_cast<const unsigned long *>(std::get<0>(codes[i]));
    std::get<1>(lu[i]) = std::get<1>(codes[i]);
  }
  return lu;
}
constexpr ControlMask init_mask() {
  ControlMask mask{
    0x0000000000000000ul,
    0x00000000000000fful,
    0x000000000000fffful,
    0x0000000000fffffful,
    0x00000000fffffffful,
    0x000000fffffffffful,
    0x0000fffffffffffful,
    0x00fffffffffffffful,
    0xfffffffffffffffful,
  };
  return mask;
}

int count_hits(const std::string &str) {
  static ControlLookup lu = init_lookup();
  constexpr ControlMask mask = init_mask();

  // str already assumed > 15 
  int hits = 0;
  const char* ptr = str.c_str();
  for (size_t i = 0; i < str.size() - 15; ++i) {
    auto match = *reinterpret_cast<const unsigned long *>(ptr + i);
    for (size_t j = 0; j < lu.size(); ++j) {
      if ((match & mask[std::get<1>(lu[j])]) == std::get<0>(lu[j]))
        hits++;
    }
  }
  return hits;
}

int main() {
  std::string test = "this is a long string with $SP$ all over and $BP$ and also $u7e$::h123456789abcdef";

  for (size_t i = 0; i < 10e6; i++)
    count_hits(test);

  return 0;
}
