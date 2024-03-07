#!/bin/bash
gcc -o libtest.so -shared -fPIC -std=c11 test.c  || { echo "Dummy libtest compilation failed"; exit 1; }
gcc -o libtest_requirer.so -shared -fPIC -std=c11 -L. test_requirer.c -Wl,-rpath,'${ORIGIN}' -ltest || { echo "libtest_requirer compilation failed"; exit 1; }
gcc -o test_tester -std=c11 -L. test_tester.c -Wl,-rpath,'${ORIGIN}' -ltest -lpthread || { echo "test_tester compilation failed"; exit 1; }
gcc -o test_dlopener dlopener.c -DTARGET="libtest.so" -ldl -lpthread || { echo "test_dlopener compilation failed"; exit 1; }
gcc -o test_requirer_dlopener dlopener.c -DTARGET="libtest_requirer.so" -ldl -lpthread || { echo "test_requirer_dlopener compilation failed"; exit 1; }

for i in {0..25}; do
    # First of all, build the shared library with the specified amount of TLS required
    gcc -o libtest.so -shared -fPIC -std=c11 -DTLS_SIZE=$((2**i)) test.c

    # Print out the TLS section
    readelf -lW libtest.so | grep TLS

    # Run a binary that requires libtest directly.
    ./test_tester

    # Run a binary that calls `dlopen()` on libtest
    ./test_dlopener

    # Run a binary that calls `dlopen()` on a library (libtest_requirer) that requires libtest
    ./test_requirer_dlopener
done

# Can also audit some of the local variables with TLS requirements using `readelf -sW libtest.so | grep TLS`
