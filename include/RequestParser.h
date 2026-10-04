#pragma once

#include <string>

class RequestParser {
public:
    void append(const std::string& data);

    bool next_request(std::string& request);

private:
    std::string input_buffer_;
};