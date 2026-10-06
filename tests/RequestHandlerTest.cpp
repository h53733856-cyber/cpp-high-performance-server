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

    assert(
        handler.process_request("PRIME 1") ==
        "ERROR\n"
    );

    assert(
        handler.process_request("PRIME 2") ==
        "1"
    );

    assert(
        handler.process_request("PRIME 10") ==
        "4"
    );

    assert(
        handler.process_request("PRIME 100") ==
        "25"
    );

    std::cout << "RequestHandler test passed." << std::endl;

    return 0;
}