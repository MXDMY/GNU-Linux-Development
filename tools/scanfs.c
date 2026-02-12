#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <linux/input.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/wait.h>
#include <errno.h>
#include <limits.h>

static char* ignore_dir[] = {
    "/dev", "/proc", "/tmp", "/var",
    "/mnt", "/sys", "/etc/config"
    };

#define errno_printf(syscall, file) printf("%s on %s failed: %s\n", #syscall, file, strerror(errno));
static void print_checksum(char* file)
{
    struct stat st;
    if (lstat(file, &st) == -1)
    {
        errno_printf(lstat, file);
        return;
    }
    
    uint32_t sum = 0;
    if (S_ISLNK(st.st_mode))
    {
        static uint8_t buf[PATH_MAX + 1];
        ssize_t r = readlink(file, (char*)buf, PATH_MAX);
        if (r < 0)
        {
            errno_printf(readlink, file);
            return;
        }
        buf[r] = '\0';
        for (int i = 0; i < r; i++)
            sum += buf[i];
    }
    else
    {
        int fd = open(file, O_RDONLY);
        if (fd < 0)
        {
            errno_printf(open, file);
            return;
        }

        uint8_t ch;
        ssize_t r;
        for (;;)
        {
            r = read(fd, &ch, 1);
            if (r < 0)
            {
                errno_printf(read, file);
                return;
            }
            else if (0 == r)
                break;
            else
                sum += ch;
        }
        close(fd);
    }
    printf("0x%08X %s\n", sum, file);
}

typedef struct
{
    char* path;
    uint8_t is_alloc; // path 指向内存是否为动态分配
}dirstack_item;

#define DIRSTACK_MAX_DEPTH 4096
static dirstack_item dirstack[DIRSTACK_MAX_DEPTH];
static uint32_t dirstack_top = 0;
static pthread_spinlock_t dirstack_slock;

// 栈满返回 0
static uint8_t dirstack_push(dirstack_item item)
{
    uint8_t r = 1;
    pthread_spin_lock(&dirstack_slock);
    if (dirstack_top >= DIRSTACK_MAX_DEPTH)
        r = 0;
    else
    {
        dirstack[dirstack_top] = item;
        dirstack_top++;
    }
    pthread_spin_unlock(&dirstack_slock);
    return r;
}

// 栈空返回 0
static uint8_t dirstack_pop(dirstack_item* item_p)
{
    uint8_t r = 1;
    pthread_spin_lock(&dirstack_slock);
    if (0 == dirstack_top)
        r = 0;
    else
    {
        dirstack_top--;
        *item_p = dirstack[dirstack_top];
    }
    pthread_spin_unlock(&dirstack_slock);
    return r;
}

// 不考虑符号链接
static uint8_t is_same_dir(char* path1, char* path2)
{
    struct stat stat1, stat2;
    
    if (stat(path1, &stat1) != 0)
    {
        errno_printf(stat, path1);
        return 0;
    }
    if (stat(path2, &stat2) != 0)
    {
        errno_printf(stat, path2);
        return 0;
    }
    
    if (!S_ISDIR(stat1.st_mode) || !S_ISDIR(stat2.st_mode))
        return 0;
    
    // 比较设备号和 inode 号，避免不同文件系统相同 inode
    return stat1.st_dev == stat2.st_dev && stat1.st_ino == stat2.st_ino;
}

static void* dir_traversal(void* arg)
{
    for (;;)
    {
        // 出栈
        dirstack_item item;
        if (0 == dirstack_pop(&item))
            break;
        char* dirpath = item.path;
        // 跳过要忽略的目录
        for (int i = 0; i < sizeof(ignore_dir) / sizeof(ignore_dir[0]); i++)
        {
            if (is_same_dir(dirpath, ignore_dir[i]))
                goto LOOP_END;
        }

        /* 遍历出栈目录子项 */
        DIR* dir = opendir(dirpath);
        if (NULL == dir)
        {
            errno_printf(opendir, dirpath);
            goto LOOP_END;
        }

        for (;;)
        {
            struct dirent* dt = readdir(dir);
            if (NULL == dt) // 所有子项被读取，不递归
                break;
            if (0 == strcmp(dt->d_name, ".") || 0 == strcmp(dt->d_name, ".."))
                continue;

            char* path = (char*)malloc(strlen(dirpath) + strlen(dt->d_name) + 10);
            if (NULL == path)
            {
                errno_printf(malloc, dt->d_name);
                continue;
            }
            sprintf(path, "%s/%s", dirpath, dt->d_name);

            struct stat st;
            // 使用 lstat 以避免跟随符号链接
            if (lstat(path, &st) == -1)
            {
                errno_printf(lstat, path);
                free(path);
                continue;
            }

            if (S_ISDIR(st.st_mode))
            {
                if (0 == dirstack_push((dirstack_item){.path = path, .is_alloc = 1}))
                {
                    printf("full-stack, ignore %s\n", path);
                    free(path);
                    continue;
                }
            }
            else
            {
                print_checksum(path);
                free(path);
            }
        }

        closedir(dir);
LOOP_END:
        if (item.is_alloc)
            free(item.path);
    }

    return NULL;
}

int main(int argc, char *argv[])
{
    pthread_spin_init(&dirstack_slock, PTHREAD_PROCESS_PRIVATE);

START:
    // 确保入栈的目录存在且不是符号链接
    dirstack_push((dirstack_item){.path = argv[1], .is_alloc = 0});
    
    dir_traversal(NULL);
    goto START;

    pthread_spin_destroy(&dirstack_slock);

    return 0;
}
