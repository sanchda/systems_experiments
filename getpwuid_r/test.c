#include <errno.h>
#include <stdbool.h>
#include <string.h> // memset
#include <sys/types.h>
#include <pwd.h> //getpwnam_r, getpwuid_r
#include <unistd.h>


#define BUF_SZ 4096
bool user_to_id(const char *user, uid_t *id) {
  int stashed_errno;
  if (!user || !*user)
    return false;

  struct passwd pwd;
  struct passwd *result;
  static char buf[BUF_SZ] = {0}; memset(buf, 0, BUF_SZ);
  size_t buf_sz = BUF_SZ;

  getpwnam_r(user, &pwd, buf, buf_sz, &result);
  stashed_errno = errno;
  if (!result) {
    // getpwnam_r puts NULL into `result` on error
    // Print the error without allocations
    static char err_msg[] = "`getpwnam_r()` failed.  Printing errno: ";
    write(1, err_msg, sizeof(err_msg));
    write(1, strerror(stashed_errno), strlen(strerror(stashed_errno)));
    write(1, "\n", 1);
    *id = -1;
    return false;
  }

  // If we're here, we succeeded in getting a result
  static char success_msg[] = "Successfully got user ID\n";
  write(1, success_msg, sizeof(success_msg));
  *id = pwd.pw_uid;
  return true;
}

bool check_id(uid_t id) {
  int stashed_errno;
  struct passwd pwd;
  struct passwd *result;
  static char buf[BUF_SZ] = {0}; memset(buf, 0, BUF_SZ);
  size_t buf_sz = BUF_SZ;

  getpwuid_r(id, &pwd, buf, buf_sz, &result);
  stashed_errno = errno;
  if (!result) {
    // getpwuid_r puts NULL into `result` on error
    // Print the error without allocations
    static char err_msg[] = "`getpwuid_r()` failed.  Printing errno: ";
    write(1, err_msg, sizeof(err_msg));
    write(1, strerror(stashed_errno), strlen(strerror(stashed_errno)));
    write(1, "\n", 1);
    return false;
  }

  // If we're here, good job we succeeded :thumbsup:
  static char success_msg[] = "Successfully called getpwuid_r()\n";
  write(1, success_msg, sizeof(success_msg));
  return true;
}

int main(int n, char **V) {
  static const char default_user[] = "nobody";
  const char *user = default_user;
  uid_t id = -1;

  // If an argument is supplied, treat it as a user
  if (n > 1)
    user = V[1];

  static char pre_msg[] = "Running test on user `";
  static char post_msg[] = "`\n";
  write(1, pre_msg, sizeof(pre_msg));
  write(1, user, strlen(user));
  write(1, post_msg, sizeof(post_msg));

  // Call getpwuid_r from a wrapper, so we can ensure transient stack storage
  //for temporaries.
  if (!user_to_id(user, &id))
    return -1;
  if (!check_id(id))
    return -1;
  return 0;
}
