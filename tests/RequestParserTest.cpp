#include "RequestParser.h"

#include <cassert>
#include <iostream>
#include <string>

int main()
{
    RequestParser parser;

    std::string request;

    parser.append("ECHO hel");

    assert(!parser.next_request(request));

    parser.append("lo\n");

    assert(parser.next_request(request));
    assert(request == "ECHO hello");

    parser.append("REVERSE abc\nCALC 123 456\n");

    assert(parser.next_request(request));
    assert(request == "REVERSE abc");

    assert(parser.next_request(request));
    assert(request == "CALC 123 456");

    assert(!parser.next_request(request));

    std::cout << "RequestParser test passed." << std::endl;

    return 0;
}