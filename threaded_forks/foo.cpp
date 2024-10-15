#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <random>
#include <unistd.h>

std::mutex globalMutex;

void threadFunction() {
    while (true) {
        {
            std::lock_guard<std::mutex> lock(globalMutex);
            // Do some work here
        }
        // Introduce a random sleep to simulate work and create unpredictability
        std::this_thread::sleep_for(std::chrono::milliseconds(rand() % 100));
    }
}

int main() {
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back(threadFunction);
    }

    std::cout << "Starting 5 child processes..." << std::endl;
    for (int i = 0; i < 5; ++i) {
        if (fork() == 0) {
            // Child process
            threadFunction();
        }
    }
    sleep(1);

    std::cout << "Done" << std::endl;
    return 0;
}

