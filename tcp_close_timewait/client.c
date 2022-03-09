#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <netinet/tcp.h>
#include <arpa/inet.h>

#ifdef __APPLE__
#  define MSG_NOSIGNAL 0
#elif __linux__
#  define SO_NOSIGPIPE 0
#else
#  error lol wrong os
#endif

int main(int argc, char** argv) {
  int port = -1;
  int clos = 0;
  if(1>=argc)               return printf("Need to specify a port\n"), -1;
  if(2>=argc)               return printf("Need to specify close behavior\n"), -1;
  if(!(port=atoi(argv[1]))) return printf("Couldn't parse port '%s'\n", argv[1]), -1;
  clos = atoi(argv[2]); // only close if arg is 1

  // Connect
  int fd = -1;
  struct sockaddr_in sa = {
    .sin_family = AF_INET,
    .sin_port   = htons(port)};
  inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);

  while(1) {
    if (fd == -1) {
      if(-1 == (fd = socket(AF_INET, SOCK_STREAM, SO_NOSIGPIPE)) ||
         -1 == connect(fd, (const struct sockaddr *)&sa, sizeof(sa))) {
        return printf("Couldn't connect on port %d\n", port), -1;
      }
    }
    if (-1 == send(fd, "0", 1, MSG_NOSIGNAL)) {
      if (errno != EINTR) {
        if (clos)
          close(fd);
        fd = -1;
        continue;
      }
    }
    if (clos)
      close(fd);
    fd = -1;
  }

  return 0;
}
