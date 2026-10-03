#include "Server.h"

int main()
{
    Server server;

    if (!server.init()) {
        return 1;
    }

    server.run();

    return 0;
}