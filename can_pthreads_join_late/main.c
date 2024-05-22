#include <stdio.h>
#include <dlfcn.h>

// Function calls dlopen on the specified library and then dlsym to get the "foo" function
void (*foo)() = NULL;
void get_foo(const char *libname) {
    void *handle = dlopen(libname, RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return;
    }

    foo = (void (*)())dlsym(handle, "foo");
    if (!foo) {
        fprintf(stderr, "dlsym failed: %s\n", dlerror());
        return;
    }

    foo();
    dlclose(handle);
}

int main() {
  get_foo("./libfoo.so");
  foo();
  return 0;
}
