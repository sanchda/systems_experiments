#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef __APPLE__
#  define MSG_NOSIGNAL 0
#elif __linux__
#  define SO_NOSIGPIPE 0
#else
#  error lol wrong os
#endif

void listener_worker(int fd, int closeit) {
  static char res[] = "OK";
  char buf[4096] = {0};
  int n = 0;
  recv(fd, &buf, sizeof(buf), MSG_NOSIGNAL); // We don't care
  send(fd, &res, sizeof(res), MSG_NOSIGNAL); // We don't care
  if (closeit)
    close(fd); // implied by exit(1)???  glibc remembers
  printf(".");
  exit(1);
}

int main(int argc, char** argv) {
  int port = -1;
  int clos = 0;
  if(1>=argc)               return printf("Need to specify a port\n"), -1;
  if(2>=argc)               return printf("Need to specify close behavior\n"), -1;
  if(!(port=atoi(argv[1]))) return printf("Couldn't parse port '%s'\n", argv[1]), -1;
  clos = atoi(argv[2]); // only close if arg is 1

  // Bind
  int lfd;
  struct sockaddr_in sa = {
    .sin_family = AF_INET,
    .sin_port   = htons(port),
    .sin_addr   = (struct in_addr){INADDR_ANY}};
  if(-1 == (lfd = socket(AF_INET, SOCK_STREAM, SO_NOSIGPIPE))            ||
     -1 == bind(lfd, (struct sockaddr *)&sa, sizeof(struct sockaddr_in)) ||
     -1 == listen(lfd, 1000)) {
    return printf("Couldn't bind/listen to port %d\n", port), -1;
  }

  struct rlimit rl = {0};
  getrlimit(RLIMIT_NOFILE, &rl);
  rl.rlim_cur = rl.rlim_max;
  setrlimit(RLIMIT_NOFILE, &rl);
  while(1) {
    struct sockaddr_in si = {0};
    int rfd = accept(lfd, NULL, 0);
    if (-1 == rfd && errno == EMFILE) {
      printf("Too many files");
      return -1;
    }
    if(!fork()) {
      listener_worker(rfd, clos);  // exits here
      exit(-1); // never gets here, but still
    }
    if (clos)
      close(rfd); // hangup in parent
  }

  return 0;
}
