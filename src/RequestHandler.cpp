#include "RequestHandler.h"

#include <sstream>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <iostream>

std::string RequestHandler::process_request(const std::string& request)
{
    std::istringstream stream(request);

    std::string command;
    stream >> command;

    if (command == "ECHO") {
        std::string message;

        std::getline(stream, message);

        if (!message.empty() && message.front() == ' ') {
            message.erase(0, 1);
        }

        return message;
    }

    if (command == "REVERSE") {
        std::string message;

        std::getline(stream, message);

        if (!message.empty() && message.front() == ' ') {
            message.erase(0, 1);
        }

        return std::string(message.rbegin(), message.rend());
    }

    if (command == "CALC") {
        long long a;
        long long b;

        if (!(stream >> a >> b)) {
            return "ERROR\n";
        }

        return std::to_string(a + b);
    }

    if (command == "SLEEP") {
        int milliseconds;

        if (!(stream >> milliseconds) || milliseconds < 0) {
            return "ERROR\n";
        }

        std::cout << "[worker] SLEEP start: "
                << milliseconds << " ms"
                << std::endl;

        std::this_thread::sleep_for(
            std::chrono::milliseconds(milliseconds)
        );

        std::cout << "[worker] SLEEP finish"
                << std::endl;

        return "OK\n";
    }
    
    if (command == "PRIME") {
        int n;

        if (!(stream >> n) || n < 2) {
            return "ERROR\n";
        }

        int count = 0;

        for (int i = 2; i <= n; ++i) {
            bool is_prime = true;

            for (int j = 2; j * j <= i; ++j) {
                if (i % j == 0) {
                    is_prime = false;
                    break;
                }
            }

            if (is_prime) {
                ++count;
            }
        }

        return std::to_string(count);
    }
    
    return "ERROR\n";
}