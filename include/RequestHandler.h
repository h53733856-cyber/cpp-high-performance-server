#pragma once

#include <string>

class RequestHandler {
public:
    std::string process_request(const std::string& request);
};