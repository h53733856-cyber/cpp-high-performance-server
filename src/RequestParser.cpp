#include "RequestParser.h"

void RequestParser::append(const std::string& data)
{
    input_buffer_ += data;
}

bool RequestParser::next_request(std::string& request)
{
    const auto newline_pos = input_buffer_.find('\n');

    if (newline_pos == std::string::npos) {
        return false;
    }

    request = input_buffer_.substr(0, newline_pos);

    input_buffer_.erase(0, newline_pos + 1);

    return true;
}