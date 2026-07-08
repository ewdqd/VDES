#include "vdes_service.h"
#include "socket_utils.h"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    VdesService service;
    if (!service.start()) {
        std::cerr << "Failed to start VDES service" << std::endl;
        return 1;
    }
    std::cout << "VDES Service running. Press Ctrl+C to stop." << std::endl;
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    service.stop();
    return 0;
}