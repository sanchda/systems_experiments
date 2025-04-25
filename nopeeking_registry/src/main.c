#include "nopeeking.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void)
{
    // Initialize the socket
    if (!np_socket_init()) {
        fprintf(stderr, "Failed to initialize socket\n");
        return 1;
    }

    // Test message modification
    const char* test_msg = "Hello, World!";
    if (!np_modify_message(test_msg)) {
        fprintf(stderr, "Failed to write message\n");
        np_socket_cleanup();
        return 1;
    }

    // Create an indefinite pause for testing
    printf("Message sent: %s\n", test_msg);
    printf("PID is %d\n", getpid());
    printf("Press Enter to continue...\n");
    getchar();

    // Peek the message
    char* msg = np_peek_message(getpid());
    if (msg) {
        printf("Peeked message: %s\n", msg);
        free(msg);
    } else {
        printf("No message available\n");
    }

    // Test message modification again
    const char* new_msg = "This is a new message!";
    if (!np_modify_message(new_msg)) {
        fprintf(stderr, "Failed to update message\n");
    }

    msg = np_peek_message(getpid());
    if (msg) {
        printf("Peeked updated message: %s\n", msg);
        free(msg);
    }

    // Clean up the socket
    np_socket_cleanup();
    printf("Socket cleaned up\n");

    return 0;
}
