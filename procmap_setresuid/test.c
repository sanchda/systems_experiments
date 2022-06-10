#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <poll.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define PASS "\033[1m\033[32mPASS\033[39m\033[0m"
#define FAIL "\033[1m\033[31mFAIL\033[39m\033[0m"

typedef union resuid_t {
  struct {
    uid_t ruid;
    uid_t euid;
    uid_t suid;
  };
  uid_t uid[3];
} resuid_t;

int getres(resuid_t *res) {
  return getresuid(res->uid, res->uid + 1, res->uid + 2);
}

int setres(resuid_t *res) {
  return setresuid(res->uid[0], res->uid[1], res->uid[2]);
}

char *uid2name(uid_t uid) {
  static char *unknown = "???";
  struct passwd *pwd = getpwuid(uid);
  return pwd ? pwd->pw_name : unknown;
}

bool resuid_switch(resuid_t *res, resuid_t *old, const char *msg) {
  if (getres(old)) {
    int stashed_errno = errno;
    printf("(%d) %s [SWITCH] " FAIL " getting current UIDs\n",
      getpid(),
      msg);
    errno = stashed_errno;
    return false;
  }
  if (setres(res)) {
    int stashed_errno = errno;
    printf("(%d) %s [SWITCH] " FAIL " setting new UIDs\n", getpid(), msg);
    errno = stashed_errno;
    return false;
  }

  // Print the transition
  char *new_r = uid2name(res->ruid);
  char *new_e = uid2name(res->euid);
  char *new_s = uid2name(res->suid);
  char *old_r = uid2name(old->ruid);
  char *old_e = uid2name(old->euid);
  char *old_s = uid2name(old->suid);
  printf("(%d) %s [SWITCH] " PASS " (%s/%s/%s) -> (%s/%s/%s)\n",
    getpid(),
    msg,
    new_r, new_e, new_s, old_r, old_e, old_s);
  return true;
}

void print_res(const char *msg) {
  resuid_t *res = &(resuid_t){0};
  getres(res);
  printf("(%d) %s [CUR UID] %s/%s/%s\n",
      getpid(),
      msg,
      getpwuid(res->ruid)->pw_name,
      getpwuid(res->ruid)->pw_name,
      getpwuid(res->ruid)->pw_name);
}

bool stat_check(const char *path, const char *msg) {
  struct statx sb = {0};
  static int xfl = AT_STATX_SYNC_AS_STAT | AT_SYMLINK_NOFOLLOW;
  static int xmk = STATX_ALL;
  if (!statx(AT_FDCWD, path, xfl, xmk, &sb))
    printf("(%d) %s [STAT] " PASS " (%s) (%s:%s)\n",
      getpid(),
      msg,
      path,
      getpwuid(sb.stx_uid)->pw_name,
      getgrgid(sb.stx_gid)->gr_name);
  else
    printf("(%d) %s [STAT] " FAIL " (%s)<%s>\n",
      getpid(),
      msg,
      path,
      strerror(errno));
}

void start_server(struct passwd *user_pwd) {
  // Starts a "server" idiomatically listening on a `poll()` loop
  // ... except it doesn't do anything

  // Before doing suexec, print out some info for diagnostics
  static char buf[1024] = {0};
  size_t sz = snprintf(buf, sizeof(buf), "/proc/%d/maps", getpid());
  stat_check(buf, "[SERVER]");

  // Switch to the desired user
  // A relatively comprehensive switch consists of
  // 1. setgid
  // 2. initgroups
  // 3. setuid

  uid_t uid = user_pwd->pw_uid;
  gid_t gid = user_pwd->pw_gid;
  const char *username = user_pwd->pw_name;
  if (setgid(gid)) {
    printf("(%d) [SERVER] " FAIL " setgid (%s)\n", getpid(), strerror(errno));
  }
  if (initgroups(username, gid)) {
    printf("(%d) [SERVER] " FAIL " initgroups (%s/%s) (%s)\n",
      getpid(),
      username,
      getgrgid(gid)->gr_name,
      strerror(errno));
  }
  if (setuid(uid)) {
    printf("(%d) [SERVER] " FAIL " setuid (%s)\n", getpid(), strerror(errno));
  }

  // OK let's hang out for a while.
  poll(&(struct pollfd){.fd = 0, .events = POLLIN}, 1, 3000);
  exit (-1);
}

bool inspect_server(struct passwd *user_pwd, pid_t pid) {
  static char procfs_buf[1024] = {0};
  size_t sz = snprintf(procfs_buf, sizeof(procfs_buf), "/proc/%d/maps", pid);
  resuid_t *res = &(resuid_t){0};
  resuid_t *old = &(resuid_t){0};

  // Check first
  stat_check(procfs_buf, "[INSPECT 0]");

  // If given, switch to the noted user
  if (user_pwd && !resuid_switch(&(resuid_t){user_pwd->pw_uid, user_pwd->pw_uid, -1}, old, "[INSPECT FORWARD]"))
    return false;

  bool ret = false;
  int fd = open(procfs_buf, O_RDONLY);
  if (-1 == fd) {
    print_res("  ");
    goto CLEANUP;
  } else {
    stat_check(procfs_buf, "[INSPECT 1]");
    print_res("  ");
    ret = true;
  }

CLEANUP:
  close(fd);
  if (user_pwd && !resuid_switch(old, res, "[INSPECT BACK]"))
    ret = false;
  stat_check(procfs_buf, "[INSPECT 2]");
  return ret;
}

int main(int n, char **v) {
  if (n < 2) {
    printf("(%d) [MAIN] Please enter a user\n", getpid());
    return -1;
  }
  const char *user_name = v[1];
  struct passwd *user_pwd = getpwnam(user_name);
  if (!user_pwd) {
    printf("(%d) [MAIN] Couldn't get user (%s) information.\n",
      getpid(),
      user_name);
    return -1;
  }

  // Tell the user what we're gonna do.
  {
    struct passwd *old_pwd = getpwuid(geteuid());
    printf("(%d) [MAIN] User <%s> switch to <%s>\n",
      getpid(),
      old_pwd->pw_name, user_pwd->pw_name);
  }

  // Check procfs real quick
  char buf[1024] = {0};
  snprintf(buf, sizeof(buf), "/proc/%d/maps", getpid());
  stat_check(buf, "[INFO]");

  // Spawn the server
  pid_t pid = fork();
  if (!pid)
    start_server(user_pwd);

  // If we're here, we're in the original process.
  usleep(1e6); // Wait for a second
  inspect_server(NULL, pid);
  inspect_server(user_pwd, pid);

  // OK whatever we're done
  return 0;

}
