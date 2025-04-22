#include "nopeeking.h"
#include <arpa/inet.h>
#include <limits.h>
#include <netinet/in.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

// Multicast context structure
typedef struct {
    int sockfd;
    struct sockaddr_in addr;
    char multicast_addr[INET_ADDRSTRLEN];
    int port;  // only used for receiver
} multicast_context;

// Message structure with snowflake
typedef struct {
    uint64_t snowflake;  // Combined message size and sequence ID
    char data[];         // Flexible array member for message content
} np_message_t;

// Global variables
static multicast_context g_multicast_ctx = {0};
static _Atomic uint32_t g_sequence_id = 0;

// Error codes for the multicast functions
#define MCAST_SUCCESS 0
#define MCAST_ERROR_INVALID_ADDR -1
#define MCAST_ERROR_SOCKET -2
#define MCAST_ERROR_TTL -3
#define MCAST_ERROR_BIND -4
#define MCAST_ERROR_MEMBERSHIP -5
#define MCAST_ERROR_CONNECT -6
#define MCAST_ERROR_SEND -7
#define MCAST_ERROR_RECV -8

// This function generates a multicast address in the 239.x.y.z range
// 239.x.y.z is reserved for organization-local scope, so it's safe to use
inline static bool generate_safe_multicast_address(char* buffer, size_t size)
{
    // Seed random number generator if not already seeded
    static int seeded = 0;
    if (!seeded) {
        srand(time(NULL));
        seeded = 1;
    }

    // Use 239.x.y.z format (organization-local scope)
    unsigned char x = rand() % 256;
    unsigned char y = rand() % 256;
    unsigned char z = rand() % 256;

    ssize_t sz = snprintf(buffer, size, "239.%d.%d.%d", x, y, z);
    return (sz > 0 && (size_t)sz < size);
}

// validate if an address is a valid multicast address
// Probably unnecessary, but here for completeness
inline static int is_valid_multicast_address(const char* addr_str)
{
    static const unsigned long long mcast_min_addr = 0xE0000000;  // 224.0.0.0
    static const unsigned long long mcast_max_addr = 0xEFFFFFFF;  // 239.255.255.255
    struct in_addr addr;

    // Check if the address format is valid
    if (inet_pton(AF_INET, addr_str, &addr) != 1) {
        return 0;
    }

    // Check if it's in the multicast range
    uint32_t addr_val = ntohl(addr.s_addr);
    return (addr_val >= mcast_min_addr && addr_val <= mcast_max_addr);
}

// initializes a multicast sender socket
int init_multicast_sender(multicast_context* ctx, const char* multicast_addr)
{
    struct in_addr mcast;
    char addr_buf[INET_ADDRSTRLEN];

    // Use provided address or generate one
    if (multicast_addr == NULL) {
        generate_safe_multicast_address(addr_buf, sizeof(addr_buf));
        multicast_addr = addr_buf;
    }

    // Validate multicast address
    if (!is_valid_multicast_address(multicast_addr) || inet_pton(AF_INET, multicast_addr, &mcast) != 1) {
        return MCAST_ERROR_INVALID_ADDR;
    }

    // Create UDP socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        return MCAST_ERROR_SOCKET;
    }

    // Set TTL; 1 is what we use for local network
    int ttl = 1;
    if (setsockopt(sockfd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0) {
        close(sockfd);
        return MCAST_ERROR_TTL;
    }

    // Configure destination address
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = mcast.s_addr;
    addr.sin_port = htons(0);  // OS will assign a random port
    ctx->port = 0;             // No port for sender

    // Set socket as connected to the multicast address to simplify sending
    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sockfd);
        return MCAST_ERROR_CONNECT;
    }

    // Store information in context
    ctx->sockfd = sockfd;
    memcpy(&ctx->addr, &addr, sizeof(addr));
    strncpy(ctx->multicast_addr, multicast_addr, INET_ADDRSTRLEN);

    return MCAST_SUCCESS;
}

// Initialize a multicast receiver socket
static inline int init_multicast_receiver(multicast_context* ctx, const char* multicast_addr, int port)
{
    struct ip_mreq mreq;
    int reuse = 1;

    // Validate multicast address
    if (!is_valid_multicast_address(multicast_addr)) {
        return MCAST_ERROR_INVALID_ADDR;
    }

    // Initialize multicast request structure
    if (inet_pton(AF_INET, multicast_addr, &mreq.imr_multiaddr) != 1) {
        return MCAST_ERROR_INVALID_ADDR;
    }

    // Create UDP socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        return MCAST_ERROR_SOCKET;
    }

    // Allow multiple sockets to use the same port
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        close(sockfd);
        return MCAST_ERROR_SOCKET;
    }

    // Configure local address
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    // Bind to receive multicast
    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sockfd);
        return MCAST_ERROR_BIND;
    }

    // Join multicast group
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        close(sockfd);
        return MCAST_ERROR_MEMBERSHIP;
    }

    // Store information in context
    ctx->sockfd = sockfd;
    memcpy(&ctx->addr, &addr, sizeof(addr));
    strncpy(ctx->multicast_addr, multicast_addr, INET_ADDRSTRLEN);
    ctx->port = port;

    return MCAST_SUCCESS;
}

/**
 * Send data to the multicast group
 *
 * @param ctx Pointer to a multicast_context structure (sender)
 * @param data Pointer to the data to send
 * @param len Length of the data
 * @return Number of bytes sent or error code
 */
int multicast_send(multicast_context* ctx, const void* data, size_t len)
{
    ssize_t send_len = send(ctx->sockfd, data, len, MSG_NOSIGNAL | MSG_DONTWAIT);

    if (send_len < 0) {
        return MCAST_ERROR_SEND;
    }

    return (int)send_len;
}

// read data from the multicast group
int multicast_receive(multicast_context* ctx, void* buffer, size_t max_len, int flags)
{
    ssize_t recv_len = recv(ctx->sockfd, buffer, max_len, flags);

    if (recv_len < 0) {
        return MCAST_ERROR_RECV;
    }

    return (int)recv_len;
}

// Close the multicast socket
void multicast_close(multicast_context* ctx)
{
    close(ctx->sockfd);
    ctx->sockfd = -1;
}

// error to string
const char* multicast_strerror(int error_code)
{
    switch (error_code) {
        case MCAST_SUCCESS:
            return "Success";
        case MCAST_ERROR_INVALID_ADDR:
            return "Invalid multicast address";
        case MCAST_ERROR_SOCKET:
            return "Socket error";
        case MCAST_ERROR_TTL:
            return "Failed to set TTL";
        case MCAST_ERROR_BIND:
            return "Bind error";
        case MCAST_ERROR_MEMBERSHIP:
            return "Failed to join multicast group";
        case MCAST_ERROR_CONNECT:
            return "Connect error";
        case MCAST_ERROR_SEND:
            return "Send error";
        case MCAST_ERROR_RECV:
            return "Receive error";
        default:
            return "Unknown error";
    }
}

// Helper functions for snowflake handling
static inline uint64_t create_snowflake(uint32_t size, uint32_t seq_id)
{
    return ((uint64_t)size << 32) | seq_id;
}

static inline void parse_snowflake(uint64_t snowflake, uint32_t* size, uint32_t* seq_id)
{
    *size = (uint32_t)(snowflake >> 32);
    *seq_id = (uint32_t)(snowflake & 0xFFFFFFFF);
}

bool np_socket_init(void)
{
    int err = init_multicast_sender(&g_multicast_ctx, NULL);
    if (err != MCAST_SUCCESS) {
        fprintf(stderr, "Failed to initialize multicast sender: %s\n", multicast_strerror(err));
        return -1;
    }

    // Try to reposition the socket to a higher number
    int positioned_fd = -1;
    for (int power = 30; power >= 0; power--) {  // 2^30 is near INT_MAX on most systems
        int target_fd = 1 << power;

        // Skip if target is below or equal to original
        if (target_fd <= g_multicast_ctx.sockfd) {
            continue;
        }

        if (dup2(g_multicast_ctx.sockfd, target_fd) != -1) {
            positioned_fd = target_fd;
            break;
        }
    }

    if (positioned_fd == -1) {
        multicast_close(&g_multicast_ctx);
        return -1;
    }

    // Close original fd, now we have the high-numbered fd
    close(g_multicast_ctx.sockfd);
    g_multicast_ctx.sockfd = positioned_fd;
    return true;
}

static inline unsigned long get_socket_inode(pid_t pid, int fd)
{
    char path[4096];
    char link[4096];
    ssize_t len;
    unsigned long inode = -1;

    snprintf(path, sizeof(path), "/proc/%d/fd/%d", pid, fd);

    len = readlink(path, link, sizeof(link) - 1);
    if (len != -1) {
        link[len] = '\0';

        // Parse socket:[12345] format
        if (strncmp(link, "socket:[", 8) == 0) {
            char* end;
            inode = strtoul(link + 8, &end, 10);
            if (end && *end == ']') {
                return inode;
            }
        }
    }

    return -1;
}

int init_multicast_context_from_pid_fd(multicast_context* ctx, pid_t pid, int fd)
{
    FILE* fp;
    char line[4096];
    unsigned long inode;
    int found = 0;

    if (!ctx) {
        return UINT_MAX;
    }

    memset(ctx, 0, sizeof(multicast_context));

    // Get socket inode from pid and fd
    inode = get_socket_inode(pid, fd);
    printf("Socket inode: %lu\n", inode);
    if (inode == UINT_MAX) {
        fprintf(stderr, "Failed to get socket inode for PID %d, FD %d\n", pid, fd);
        return -1;
    }

    // Open /proc/net/udp for socket information
    fp = fopen("/proc/net/udp", "r");
    if (!fp) {
        perror("Cannot open /proc/net/udp");
        return -1;
    }

    // Skip header line
    fgets(line, sizeof(line), fp);

    // Search for the socket inode
    while (fgets(line, sizeof(line), fp)) {
        unsigned int local_addr[4];
        unsigned int local_port;
        unsigned int remote_addr[4];
        unsigned int remote_port;
        unsigned int state, socket_inode;

        if (sscanf(line,
                   " %*d: %02x%02x%02x%02x:%04x %02x%02x%02x%02x:%04x %x %*x:%*x %*x:%*x %*x %*d %*d %u",
                   &local_addr[0],
                   &local_addr[1],
                   &local_addr[2],
                   &local_addr[3],
                   &local_port,
                   &remote_addr[0],
                   &remote_addr[1],
                   &remote_addr[2],
                   &remote_addr[3],
                   &remote_port,
                   &state,
                   &socket_inode) == 12) {
            if (socket_inode == inode) {
                found = 1;
                ctx->port = local_port;

                // Uh, this should always be the *remote* address, right???
                unsigned long ipaddr =
                    (local_addr[0] << 24) | (local_addr[1] << 16) | (local_addr[2] << 8) | local_addr[3];
                unsigned long remote_ipaddr =
                    (remote_addr[0] << 24) | (remote_addr[1] << 16) | (remote_addr[2] << 8) | remote_addr[3];

                if ((ipaddr & 0x000000F0) == 0x000000E0 || (remote_ipaddr & 0x000000F0) == 0x000000E0) {
                    // If local address is multicast, use it, otherwise use remote address
                    unsigned long multicast_addr = 0;
                    if ((ipaddr & 0x000000F0) == 0x000000E0) {
                        multicast_addr = ipaddr;
                    } else if ((remote_ipaddr & 0x000000F0) == 0x000000E0) {
                        multicast_addr = remote_ipaddr;
                    } else {
                    }

                    // Set multicast address string
                    inet_ntop(AF_INET, &ctx->addr.sin_addr, ctx->multicast_addr, INET_ADDRSTRLEN);

                    // Set up sockaddr_in structure
                    ctx->addr.sin_family = AF_INET;
                    ctx->addr.sin_port = htons(local_port);
                    ctx->addr.sin_addr.s_addr = multicast_addr;

                    // Create a new socket for the context
                    ctx->sockfd = socket(AF_INET, SOCK_DGRAM, 0);
                    if (ctx->sockfd == -1) {
                        perror("Failed to create socket");
                        fclose(fp);
                        return -1;
                    }

                    // Set socket options for multicast
                    int opt = 1;
                    if (setsockopt(ctx->sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
                        perror("Failed to set SO_REUSEADDR");
                        close(ctx->sockfd);
                        fclose(fp);
                        return -1;
                    }

                    // Try to bind to the same port
                    struct sockaddr_in bind_addr;
                    memset(&bind_addr, 0, sizeof(bind_addr));
                    bind_addr.sin_family = AF_INET;
                    bind_addr.sin_port = htons(0);
                    bind_addr.sin_addr.s_addr = INADDR_ANY;

                    if (bind(ctx->sockfd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
                        perror("Failed to bind socket");
                        close(ctx->sockfd);
                        fclose(fp);
                        return -1;
                    }

                    // Join the multicast group
                    struct ip_mreq mreq;
                    mreq.imr_multiaddr.s_addr = multicast_addr;
                    mreq.imr_interface.s_addr = INADDR_ANY;

                    if (setsockopt(ctx->sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
                        perror("Failed to join multicast group");
                        close(ctx->sockfd);
                        fclose(fp);
                        return -1;
                    }
                } else {
                    fprintf(stderr, "Socket is not bound to a multicast address\n");
                    fclose(fp);
                    return -1;
                }

                break;
            }
        }
    }

    fclose(fp);

    if (!found) {
        fprintf(stderr, "Socket with inode %lu not found in /proc/net/udp\n", inode);
        return -1;
    }

    return 0;
}

char* np_peek_message(pid_t pid)
{
    multicast_context ctx;
    if (init_multicast_context_from_pid_fd(&ctx, pid, g_multicast_ctx.sockfd) != 0) {
        fprintf(stderr, "Failed to initialize multicast context from PID %d\n", pid);
        return NULL;
    }
    int fd = ctx.sockfd;

    // First peek at the snowflake
    uint64_t snowflake;
    ssize_t bytes_read = recv(fd, &snowflake, sizeof(snowflake), MSG_PEEK | MSG_DONTWAIT);

    if (bytes_read <= 0) {
        // No message or error
        return NULL;
    }

    // Parse the snowflake to get message size
    uint32_t size, seq_id;
    parse_snowflake(snowflake, &size, &seq_id);

    if (size == 0) {
        // Invalid size
        return NULL;
    }

    // Now peek at the entire message
    size_t total_size = sizeof(snowflake) + size;
    char* buffer = malloc(total_size);

    if (!buffer) {
        perror("malloc");
        return NULL;
    }

    bytes_read = recv(fd, buffer, total_size, MSG_PEEK);

    if (bytes_read < 0 || (size_t)bytes_read != total_size) {
        // Didn't get the whole message
        free(buffer);
        return NULL;
    }

    // Verify snowflake consistency
    np_message_t* msg = (np_message_t*)buffer;
    uint32_t peek_size, peek_seq_id;
    parse_snowflake(msg->snowflake, &peek_size, &peek_seq_id);

    if (peek_size != size || peek_seq_id != seq_id) {
        // Inconsistent snowflake, possible race condition
        free(buffer);
        return NULL;
    }

    // Create the return string (just the data part)
    char* result = malloc(size + 1);  // +1 for null terminator

    if (!result) {
        perror("malloc");
        free(buffer);
        return NULL;
    }

    memcpy(result, msg->data, size);
    result[size] = '\0';  // Null-terminate

    free(buffer);
    return result;
}

bool np_modify_message(const char* new_message)
{
    if (!new_message) {
        return false;
    }

    int fd = g_multicast_ctx.sockfd;
    if (fd < 0) {
        return false;
    }
    uint32_t msg_len = (uint32_t)strlen(new_message);
    size_t total_size = sizeof(uint64_t) + msg_len;  // snowflake + message

    // Allocate memory for the message
    char* buffer = malloc(total_size);

    if (!buffer) {
        perror("malloc");
        return false;
    }

    // Create a new snowflake with incremented sequence ID
    uint32_t seq_id = atomic_fetch_add(&g_sequence_id, 1);
    np_message_t* msg = (np_message_t*)buffer;
    msg->snowflake = create_snowflake(msg_len, seq_id);

    // Copy the message data
    memcpy(msg->data, new_message, msg_len);

    // Empty the socket first (consume any existing messages)
    char temp_buf[1024];
    while (recv(fd, temp_buf, sizeof(temp_buf), MSG_DONTWAIT) > 0) {
        // Keep consuming until empty
    }

    // Send the new message
    ssize_t sent = send(fd, buffer, total_size, 0);
    free(buffer);

    return (sent == (ssize_t)total_size);
}

void np_socket_cleanup(void)
{
    multicast_close(&g_multicast_ctx);
}
