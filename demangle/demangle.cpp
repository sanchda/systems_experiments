// Simple application for demangling a string
#include <iostream>
#include <string>
#include <cxxabi.h>

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cout << "Usage: demangler <mangled name>" << std::endl;
        return 1;
    }

    std::string demangled{abi::__cxa_demangle(argv[1], 0, 0, 0)};
    if (demangled.empty())
    {
        std::cout << "Unable to demangle '" << argv[1] << "'" << std::endl;
        return 1;
    }
    std::cout << demangled << std::endl;
    return 0;
}
