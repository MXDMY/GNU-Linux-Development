#include "common.h"
/*
 * 模式 1：printf 异步信号不安全测试。运行一段时间后，父进程继续，子进程两个线程都被锁死睡眠
 * 模式 2：互斥锁异步信号不安全测试
*/
#ifdef TEST_MODE
#undef TEST_MODE
#endif
#define TEST_MODE 1

static pthread_mutex_t mutex;

static void handle_signal(int sig)
{
#if 1 == TEST_MODE
    printf("capture signal %d\n", sig);
#elif 2 == TEST_MODE
    pthread_mutex_lock(&mutex);
    write(1, "capture signal\n", 15);
    pthread_mutex_unlock(&mutex);
#endif
}

static pthread_t tid;
static void* thread(void* arg)
{
    unsigned i = 0;
    while (1)
    {
        printf("thread %lu is alive: %u\n", tid, ++i);
    }
    return NULL;
}

void async_signal_unsafety(void)
{
    pthread_mutex_init(&mutex, NULL);
    signal(SIGUSR1, handle_signal);

    pid_t pid = fork();
    if (0 == pid)
		pthread_create(&tid, NULL, thread, NULL);
    else
    {
        // 父进程先睡眠 1 秒，方便查看信号处理之前，子进程各线程输出情况
        sleep(1);
    }
    
    unsigned i = 0;
    for (;;)
    {
#if 1 == TEST_MODE
        printf("process %d is alive: %u\n",getpid(), ++i);
#elif 2 == TEST_MODE
        pthread_mutex_lock(&mutex);
        printf("process %d is alive: %u\n",getpid(), ++i);
        pthread_mutex_unlock(&mutex);
#endif
        if (pid > 0)
        {
            kill(pid, SIGUSR1);
            printf("process %d is alive: %u\n",getpid(), ++i);
        }
    }

    pthread_mutex_destroy(&mutex);
}
