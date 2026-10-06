#include "Server.h"

#include <cstddef>
#include <iostream>
#include <string>

int main(int argc, char* argv[])
{
    std::size_t worker_count = 4;

    if (argc >= 2) {
        try {
            worker_count = std::stoull(argv[1]);
        } catch (const std::exception&) {
            std::cerr << "Invalid worker count: " << argv[1] << std::endl;
            return 1;
        }
    }

    if (worker_count == 0) {
        std::cerr << "Worker count must be greater than 0." << std::endl;
        return 1;
    }

    Server server(worker_count);

    if (!server.init()) {
        return 1;
    }

    server.run();

    return 0;
}