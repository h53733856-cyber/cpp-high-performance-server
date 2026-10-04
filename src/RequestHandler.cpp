#include "RequestHandler.h"

#include <sstream>
#include <stdexcept>

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

    return "ERROR\n";
}