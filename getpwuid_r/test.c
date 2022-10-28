#include <ctype.h> // isdigit
#include <errno.h>
#include <dirent.h> // opendir, readdir
#include <fcntl.h> // open
#include <stdbool.h>
#include <stdlib.h> //
#include <string.h> // memset
#include <sys/stat.h> // open
#include <sys/types.h>
#include <pwd.h> //getpwnam_r, getpwuid_r
#include <unistd.h>


#define LOG(x)                                                    \
{                                                                 \
  if (__builtin_types_compatible_p(typeof(x), typeof(&(x)[0])))   \
    write(1, (x), sizeof(x));                                     \
  else                                                            \
    write(1, (x), strlen(x));                                     \
}

#define BUF_SZ 4096
bool uid_from_user(const char *user, uid_t *id) {
  int stashed_errno;
  if (!user || !*user)
    return false;

  struct passwd pwd;
  struct passwd *result;
  char buf[BUF_SZ] = {0}; memset(buf, 0, BUF_SZ);
  size_t buf_sz = BUF_SZ;

  getpwnam_r(user, &pwd, buf, buf_sz, &result);
  stashed_errno = errno;
  if (!result) {
    // getpwnam_r puts NULL into `result` on error
    // Print the error without allocations
    const char msg[] = "`getpwnam_r()` failed.  Printing errno: ";
    LOG(msg);
    LOG(strerror(stashed_errno));
    LOG("\n");
    *id = -1;
    return false;
  }

  // If we're here, we succeeded in getting a result
  const char msg[] = "Successfully got user ID\n";
  LOG(msg);
  *id = pwd.pw_uid;
  return true;
}

const char* user_from_uid(uid_t id) {
  int stashed_errno;
  struct passwd pwd;
  struct passwd *result;
  char buf[BUF_SZ] = {0}; memset(buf, 0, BUF_SZ);
  size_t buf_sz = BUF_SZ;

  getpwuid_r(id, &pwd, buf, buf_sz, &result);
  stashed_errno = errno;
  if (!result) {
    // getpwuid_r puts NULL into `result` on error
    // Print the error without allocations
    const char msg[] = "`getpwuid_r()` failed.  Printing errno: ";
    LOG(msg);
    LOG(strerror(stashed_errno));
    LOG("\n");
    return NULL;
  }
  return pwd.pw_name;
}

bool is_number(const char *str) {
  if (!str || !*str) return false;
  while (*str <= '9' && *str >= '0') str++;
  return *str == '\0';
}

void write_number(long num) {
  char buf[sizeof("18446744073709551616")]; // biggest uint64_t
  memset(buf, '\0', sizeof(buf));
  size_t sz = sizeof(buf) - 1;
  if (num == 0)
    buf[sz--] = '0';
  else while (num) {
    buf[sz--] = '0' + (num % 10);
    num /= 10;
  }
  write(1, &buf[sz], sizeof(buf)-sz);
}

void process_argument(const char *str) {
  uid_t uid = 0;
  const char *user = NULL;
  if (!str || !*str) {
    return; // Nothing to do here
  } else if (is_number(str)) {
    uid = strtol(str, NULL, 10);
    const char msg[] = "Running test on UID: ";
    LOG(msg);
    write_number(uid);
    LOG("\n");
    if (!(user = user_from_uid(uid))) {
      const char msg[] = "Failed to get user\n";
      LOG(msg);
      return;
    } else {
      const char pre_msg[] = "Got user: `";
      const char post_msg[] = "`\n";
      LOG(pre_msg);
      LOG(user);
      LOG(post_msg);
    }
  } else {
    user = str;
    const char pre_msg[] = "Running test on user `";
    const char post_msg[] = "`\n";
    LOG(pre_msg);
    LOG(user);
    LOG(post_msg);

    // Call getpwuid_r from a wrapper, so we can ensure transient stack storage
    //for temporaries.
    if (!uid_from_user(user, &uid)) {
      const char msg[] = "Failed to get UID\n";
      LOG(msg);
      return;
    } else {
      const char msg[] = "Got UID: ";
      LOG(msg);
      write_number(uid);
      LOG("\n");
    }
  }
}

char *procpath_from_pid(const char *pid) {
  static char path[sizeof("/proc//stat") + sizeof("32768")]; // returned
  size_t sz = 0, x = 0;
  memset(path, ' ', sizeof(path));
  memcpy(path + sz, "/proc/",  x=sizeof("/proc/"));   sz+=x-1;
  memcpy(path + sz, pid,       x=strlen(pid));        sz+=x;
  memcpy(path + sz, "/status", sizeof("/status"));
  return path;
}

char *uid_from_pid(const char *pid) {
  // Strong assumption here that the status fits in a page.
  static char uid[sizeof("18446744073709551616")] = {0}; // returned
  int fd = open(procpath_from_pid(pid), O_RDONLY);
  char _buf[4096] = {0}; char *buf = _buf;
  memset(uid, 0, sizeof(uid));
  if (-1 == fd)
    return NULL;

  size_t sz = read(fd, buf, sizeof(_buf));
  if (sz <= 0) {
    close(fd); // Not sure what to do here
    return NULL;
  }

  // Do it stupidly
  size_t n = 0;
  while (n++ < sz + 5 && (buf[0] != 'U' || buf[1] != 'i' || buf[2] != 'd'))
    buf++;
  if (n >= sz + 5) {
    close(fd);
    return NULL;
  }

  // If we're here, `buf` points at the top of the UID line
  while (!isdigit(*buf)) buf++;
  char *p = buf;
  while (isdigit(*p)) p++;
  memcpy(uid, buf, p-buf);
  close(fd);
  return uid;
}

void poll_proc() {
  DIR *proc = opendir("/proc");
  struct dirent *entry;

  if (!proc) {
    const char msg[] = "Couldn't open /proc\n";
    LOG(msg);
    return;
  }

  while ((entry = readdir(proc))) {
    if (entry->d_type == DT_DIR && isdigit(*entry->d_name))
      process_argument(uid_from_pid(entry->d_name));
  }
  closedir(proc);
}

int main(int n, char **V) {
  if (n == 1) {
    while(true)
      poll_proc();
  } else {
    int i = 0;
    n--;
    V++;
    while (true)
      process_argument(V[i++ % n]);
  }


  // Loop over the input arguments over and over

  return 0;
}