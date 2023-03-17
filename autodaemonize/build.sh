#!/bin/bash
g++-11 -std=c++20 -Wall -Wextra -fanalyzer nice_user.cpp -o nice_user
g++-11 -std=c++20 -Wall -Wextra -fanalyzer selfish_user.cpp -o selfish_user
g++-11 -std=c++20 -Wall -Wextra -fanalyzer disposable.cpp -o disposable
