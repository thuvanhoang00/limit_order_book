#include "../hdr/client.h"
#include <thread>

void doRun()
{
    thu::Client client;
    client.run();
}

int main()
{
    doRun();

    return 0;
}