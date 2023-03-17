#include "daemon.hpp"

void temporary_daemon() {
  Daemon daemon{};
  daemon.send("I'll love you forever, daemon!");
}

int main() {
  temporary_daemon();
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  std::cout << "[client] Love is fickle!" << std::endl;
  return 0;
}
