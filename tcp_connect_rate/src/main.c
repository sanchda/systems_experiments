#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/sysinfo.h>

#define DEFAULT_PORT 8080
#define DEFAULT_CLIENTS 10000
#define DEFAULT_HOST "127.0.0.1"

// Function to create a TCP server socket and start listening
int create_server_socket(int port) {
    int server_fd;
    struct sockaddr_in address;

    // Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    // Setup server address structure
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    // Bind + listen
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, SOMAXCONN) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", port);
    return server_fd;
}

// Function to run a client
// Function to run a client with multiple connections
void run_client(const char *host, int port, int connections_per_client) {
    int *sockets = malloc(connections_per_client * sizeof(int));
    struct sockaddr_in serv_addr;

    // Initialize server address structure once
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host, &serv_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        exit(EXIT_FAILURE);
    }

    // Create K connections in sequence
    for (int i = 0; i < connections_per_client; i++) {
        if ((sockets[i] = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            perror("Socket creation error");
            exit(EXIT_FAILURE);
        }

        // Connect
        if (connect(sockets[i], (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            perror("Connection failed");
            exit(EXIT_FAILURE);
        }
    }

    // Blocking read only on the last socket
    char buffer[1];
    read(sockets[connections_per_client - 1], buffer, 1);  // Will block until server closes

    // Close all sockets
    for (int i = 0; i < connections_per_client; i++) {
        close(sockets[i]);
    }
    free(sockets);
    exit(EXIT_SUCCESS);
}

// Function to spawn clients based on CPU cores
void spawn_clients(const char *host, int port, int total_connections) {
    int nprocs = get_nprocs();
    int num_clients = nprocs - 1;
    if (num_clients <= 0) num_clients = 1;

    int connections_per_client = (total_connections + num_clients - 1) / num_clients; // Ceiling division

    printf("System has %d processors\n", nprocs);
    printf("Spawning %d client processes with %d connections each (total: %d)...\n",
           num_clients, connections_per_client, total_connections);

    for (int i = 0; i < num_clients; i++) {
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            exit(EXIT_FAILURE);
        }

        if (pid == 0) {
            // Child process
            // Calculate actual connections for this client (handle remainder)
            int my_connections = connections_per_client;
            if (i == num_clients - 1) {
                // Last client might need to handle fewer connections
                my_connections = total_connections - (connections_per_client * (num_clients - 1));
            }
            run_client(host, port, my_connections);
            // Child should not return from run_client
            exit(EXIT_SUCCESS);
        }
    }

    printf("All client processes spawned\n");
}

// Function to accept connections and measure performance
void accept_connections(int server_fd, int num_clients) {
    int client_sockets[num_clients];
    int connection_count = 0;
    struct timeval start_time, end_time;

    printf("Starting to accept connections...\n");

    // Start timer
    gettimeofday(&start_time, NULL);

    // Main server loop
    while (connection_count < num_clients) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        int new_socket = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (new_socket < 0) {
            perror("accept");
            continue;
        }

        // Store the new client socket
        client_sockets[connection_count] = new_socket;
        connection_count++;

        printf("\rConnections: %d/%d", connection_count, num_clients);
        fflush(stdout);
    }

    // Stop timer and calculate statistics
    gettimeofday(&end_time, NULL);
    double elapsed_time = (end_time.tv_sec - start_time.tv_sec) * (1e6) +
                          (end_time.tv_usec - start_time.tv_usec);

    double avg_connect_time = elapsed_time / num_clients;

    printf("\n\nResults:\n");
    printf("Total connections: %d\n", connection_count);
    printf("Total time: %.2f us\n", elapsed_time);
    printf("Average time per connection: %.2f us\n", avg_connect_time);
    printf("Connection rate: %.2f connections/second\n", (num_clients * 1000.0 * 1000.0) / elapsed_time);
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    int num_clients = DEFAULT_CLIENTS;
    char host[256] = DEFAULT_HOST;

    if (argc > 1) {
        port = atoi(argv[1]);
    }
    if (argc > 2) {
        num_clients = atoi(argv[2]);
    }

    // Create a server socket and start listening
    int server_fd = create_server_socket(port);
    spawn_clients(host, port, num_clients);
    accept_connections(server_fd, num_clients);
    close(server_fd);

    // Parent is bash, so no need to wait on clients--it'll reap them
    return 0;
}

