#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
#include <array>
#include <vector>

#include <unistd.h>

struct AutoStart {
  AutoStart() noexcept {
    if (getenv("SEE_NO_EVIL"))
      return;
    setenv("SEE_NO_EVIL","lolok", 1);
    std::ifstream cmdline_file("/proc/self/cmdline");
    if (!cmdline_file.is_open()) {
      std::cerr << "Failed to introspect" << std::endl;
      return;
    }

    // Get first command.  Lots of ways to do this, but we'll do it this way.
    std::vector<std::string> args = {
      "                                                                               ",
      "Who",
      "touched",
      "my",
      "keyboard?"
    };
    std::getline(cmdline_file, args[0], '\0');

    // We now probably have all the args.  Since this is a toy example, just launch with our own args.
    std::vector <char *> argv(args.size() + 1);
    for (size_t i = 0; i < args.size(); ++i)
      argv[i] = const_cast<char*>(args[i].c_str());
    argv[args.size()] = nullptr; // comply with interface
    execv(argv[0], argv.data());
  }

  ~AutoStart() {}
};

AutoStart g_autostart;
