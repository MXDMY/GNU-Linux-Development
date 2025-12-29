#include "common.h"

static void handle_signal(int sig)
{
    printf("capture signal %d\n", sig);
}

void async_signal_safety(void)
{
    signal(SIGUSR1, handle_signal);

    pid_t pid = fork();
    
    unsigned i = 0;
    for (;;)
    {
        printf("process %d is alive: %u\n",getpid(), ++i);
        if (pid > 0)
        {
            kill(pid, SIGUSR1);
            printf("process %d is alive: %u\n",getpid(), ++i);
        }
    }
}
