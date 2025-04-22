#pragma once
#include <stdbool.h>
#include <unistd.h>

// Server functions
bool np_socket_init(void);
bool np_modify_message(const char* new_message);
void np_socket_cleanup(void);

// Client functions
char* np_peek_message(pid_t pid);
