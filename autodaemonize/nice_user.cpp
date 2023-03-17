#include "daemon.hpp"

// Client 
int main() {
  Daemon daemon{};
  daemon.send("Hello, daemon!");
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  daemon.shutdown();
  return 0;
}
