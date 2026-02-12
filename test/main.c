#include "common.h"

void async_signal_unsafety(void);

int main(int argc, char* argv[])
{
    printf("Hello GNU/Linux test\n");

    async_signal_unsafety();

    return 0;
}
