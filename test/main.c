#include "common.h"

void async_signal_safety(void);

int main(int argc, char* argv[])
{
    printf("hello GNU/Linux test\n");

    async_signal_safety();

    return 0;
}
