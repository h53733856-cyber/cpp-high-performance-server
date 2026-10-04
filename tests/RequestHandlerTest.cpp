#include "RequestHandler.h"

#include <cassert>
#include <iostream>

int main()
{
    RequestHandler handler;

    assert(
        handler.process_request("ECHO hello") ==
        "hello"
    );

    assert(
        handler.process_request("REVERSE hello") ==
        "olleh"
    );

    assert(
        handler.process_request("CALC 123 456") ==
        "579"
    );

    assert(
        handler.process_request("UNKNOWN test") ==
        "ERROR\n"
    );

    assert(
        handler.process_request("CALC 123") ==
        "ERROR\n"
    );

    std::cout << "RequestHandler test passed." << std::endl;

    return 0;
}