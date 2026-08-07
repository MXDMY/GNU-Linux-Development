/*******************************************************************************************
 帧收发实现模板
 作者：毕洁

 2026.04 更新：
 采用循环缓冲区，减少内存拷贝与内存分配，减少内存碎片同时避免拷贝开销。生产者收数据放入缓冲区，
 消费者从缓冲区提取数据。采用原子操作隔离，以降低互斥开销。溢出计数通过取余回绕。
 
 2026.06 更新：
 收发保留对循环缓冲区的利用。考虑到不同编译器 C 标准支持程度以及自身特性不同，原子操作语句变化大
 ，选择统一生产者与消费者部分功能，不再使用原子操作隔开。接收数据与检出帧全部由生产者负责，并放
 入帧列表，由应用线程选择性提取。

 2026.08 更新：
 将生产者与消费者重新拆开，删除帧列表，查找帧列表时间复杂度高。原子操作要求与 C11 或 GCC 接口对齐
 ，至少支持 C11 或 GCC 的 seq_cst 严格内存顺序实现。受 Linux 内核 kfifo 启发，使用 2 的幂次方 &
 运算，替换掉原来计数溢出的 % 运算。
*******************************************************************************************/

/**************************************** 适配部分 ****************************************/

// 头文件适配
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
// 依赖接口适配：原子操作需要适配为 seq_cst 严格内存顺序（内存屏障）
#define tpl_msleep(time)                        usleep((time) * 1000)
#define tpl_atomic_static_uint_var(name, val)   static uint32_t name = val
#define tpl_atomic_load(name)                   __atomic_load_n(&name, __ATOMIC_SEQ_CST)
#define tpl_atomic_store(name, val)             __atomic_store_n(&name, val, __ATOMIC_SEQ_CST)
#define tpl_atomic_add(name, val)               __atomic_fetch_add(&name, val, __ATOMIC_SEQ_CST)
#define tpl_atomic_exchange(name, val)          __atomic_exchange_n(&name, val, __ATOMIC_SEQ_CST)
#define tpl_mem_alloc(size)                     malloc(size)
#define tpl_mem_cpy(dst, src, size)             memcpy(dst, src, size)
#define tpl_mem_set(dst, val, size)             memset(dst, val, size)
#define tpl_mem_free(block)                     free(block)
#define tpl_pr(fmt, ...)                        printf("\033[1;34m[tpl]"fmt"\033[0m", ##__VA_ARGS__)
// 生产、消费者线程处理间隔
#define TPL_P_SLEEP_MS 5
#define TPL_C_SLEEP_MS 1
// 缓冲区大小，要求为 2 的幂次方，单位：字节
#define TPL_BUF_SIZE 1024
// 缓冲区最大索引，无需修改
#define TPL_BUF_MAX_IDX (TPL_BUF_SIZE - 1)
// 使用动态分配缓冲区
#define TPL_BUF_USE_DYNAMIC 0
// 数据端序，仅使用 'B' 与 'L'，分别表示大端与小端
static char tpl_data_endian = 'L';
// 本地端序，无需修改，自动配置，可能的值同上
static char tpl_local_endian;
// 帧结构，自行修改内容（仅用于展示，模板不使用。虽非本模板要求，但请注意字节对齐与填充）
typedef struct 
{
    uint8_t head[2];
    uint8_t len[2];
    uint8_t cmd[2];
    uint8_t data[1024];
    uint8_t checksum;
} tpl_frame;
// 帧固定区域长度
#define TPL_FRAME_FIXED_SIZE 7

/*
 * 生产者读数据接口，在保留接口语义基础上，自行修改实现内容
 * @param buf   缓冲区
 * @param size  期望读取字节数
 * @return      实际读取字节数，错误需返回负数
*/
static int tpl_read(void* buf, int size)
{
    /* 示例，模拟不断收帧 */
    static int index = 0;
    uint8_t data1[] 
    = { 
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
        0xAA, 0xBB, 0x00, 0x05, 0x10, 0x01, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0xB7,
        0xBB, 0xAA, 0x05, 0x00, 0x20, 0x11, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0xD7,
        1, 2, 89, 65, 12, 8, 13 ,89 ,165 ,10, 61, 33,
        0xBB, 0xAA, 0x01, 0x00, 0x10, 0x11, 0x0A, 0x91,
        0xAA, 0xBB, 0x00, 0x01, 0x10, 0x02, 0x0A, 0x82,
    };
    int cpsize = size > sizeof(data1) ? sizeof(data1) : size;
    for (int i = 0; i < cpsize; i++)
    {
        ((uint8_t*)buf)[i] = data1[index];
        index = (index + 1) % sizeof(data1);
    }

    return cpsize;
}

/*
 * 查找头部起始位置，在保留接口语义基础上，自行修改实现内容
 * @param buf       缓冲区
 * @param start     出发位置
 * @param size      可用字节数
 * @return          查找到帧头，返回相对于 start 的偏移；否则，返回负数
 * @note            start + 偏移 若溢出，检查需要回绕；注意端序
*/
static int tpl_search_head(void* buf, int start, int size)
{
    /* 示例 */
    uint16_t head = 0xAABB;
    uint16_t tmp = 0;
    for (int i = 0; i + 1 < size; i++)
    {
        ((uint8_t*)(&tmp))[0] = ((uint8_t*)buf)[(start + i) & TPL_BUF_MAX_IDX];
        ((uint8_t*)(&tmp))[1] = ((uint8_t*)buf)[(start + i + 1) & TPL_BUF_MAX_IDX];

        if (tpl_local_endian != tpl_data_endian)
            tmp = tmp >> 8 | tmp << 8;

        if (tmp == head)
            return i;
    }

    return -1;
}

/*
 * 获取完整帧大小，在保留接口语义基础上，自行修改实现内容
 * @param buf       缓冲区
 * @param start     出发位置
 * @param size      可用字节数
 * @return          完整帧大小，失败返回负数
 * @note            注意溢出；注意端序
*/
static int tpl_get_size(void* buf, int start, int size)
{
    /* 示例 */
    if (size < TPL_FRAME_FIXED_SIZE)
        return -1;

    uint16_t len;
    ((uint8_t*)(&len))[0] = ((uint8_t*)buf)[(start + 2) & TPL_BUF_MAX_IDX];
    ((uint8_t*)(&len))[1] = ((uint8_t*)buf)[(start + 3) & TPL_BUF_MAX_IDX];

    if (tpl_local_endian != tpl_data_endian)
        len = len >> 8 | len << 8;

    return len + TPL_FRAME_FIXED_SIZE;
}

/*
 * 校验帧并进行下一步处理，在保留接口语义基础上，自行修改实现内容
 * @param frame       帧地址
 * @param frame_size  帧大小
 * @note              frame 指向一个动态分配的内存；无需注意溢出回绕；注意端序；
*/
static void tpl_check_todo(void* frame, int frame_size)
{
    /* 示例 */
    uint8_t* f = (uint8_t*)frame;
    uint8_t checksum = 0;
    for (int i = 0; i < frame_size - 1; i++)
        checksum += f[i];
    if (checksum != f[frame_size - 1])
    {
        tpl_pr("{%s}checksum error\n", __func__);
        tpl_mem_free(f);
        return;
    }

    tpl_pr("{%s}detect frame: ", __func__);
    for (int i = 0; i < frame_size; i++)
        tpl_pr("%02X ", f[i]);
    tpl_pr("\n");
    tpl_mem_free(f);
}

/************************************ 算法部分无需适配 ************************************/

#if TPL_BUF_USE_DYNAMIC
static uint8_t* tpl_buf = NULL;
#else
static uint8_t tpl_buf[TPL_BUF_SIZE];
#endif

tpl_atomic_static_uint_var(tpl_run, 1);
tpl_atomic_static_uint_var(tpl_p_exit, 0);
tpl_atomic_static_uint_var(tpl_c_exit, 0);
tpl_atomic_static_uint_var(tpl_i1, 0); // producer 读，consumer 写，consumer 从此处拿取数据
tpl_atomic_static_uint_var(tpl_i2, 0); // producer 写，consumer 读，producer 从此处放入数据

static int tpl_frame_transfer_init(void)
{
#if TPL_BUF_USE_DYNAMIC
    if (NULL == tpl_buf)
    {
        tpl_buf = (uint8_t*)tpl_mem_alloc(TPL_BUF_SIZE);
        if (NULL == tpl_buf)
        {
            tpl_pr("{%s}malloc failed\n", __func__);
            return -1;
        }
    }
#endif
    tpl_mem_set(tpl_buf, 0, TPL_BUF_SIZE);

    uint16_t tmp = 0x0011;
    tpl_local_endian = ((uint8_t*)(&tmp))[0] ? 'L' : 'B';
    tpl_pr("{%s}local-data endian: %c-%c\n", __func__, tpl_local_endian, tpl_data_endian);

    tpl_atomic_store(tpl_p_exit, 0);
    tpl_atomic_store(tpl_c_exit, 0);
    tpl_atomic_store(tpl_i1, 0);
    tpl_atomic_store(tpl_i2, 0);
    tpl_atomic_store(tpl_run, 1);

    return 0;
}

static void tpl_frame_transfer_exit(void)
{
    tpl_atomic_store(tpl_run, 0);
    while (! tpl_atomic_load(tpl_p_exit) || ! tpl_atomic_load(tpl_c_exit))
    {
        tpl_msleep(TPL_P_SLEEP_MS + TPL_C_SLEEP_MS);
    }
    tpl_atomic_store(tpl_i1, 0);
    tpl_atomic_store(tpl_i2, 0);

#if TPL_BUF_USE_DYNAMIC
    if (tpl_buf)
    {
        tpl_mem_free(tpl_buf);
        tpl_buf = NULL;
    }
#endif
}

static void tpl_producer(void)
{
    while (tpl_atomic_load(tpl_run))
    {
        tpl_msleep(TPL_P_SLEEP_MS);

        /*
         * 补码推算，例：
         * ~1234 = 8765
         * ~1234 + 1 = 8766
         * 1234 - 1234 = 0000, 1234 + 8766 = 10000
         * 有限位 0 视为 n
         * 
         * 0110 = 6, 1001 = 9
         *           0110 = ~9
         *           0111 = ~9 + 1
         * 1101 = 13
         * 6 - 9 = -3 = 6 + (-9) = 6 + 7 = 13 = 0xd
         *       ...+7......-9....
         *       |      |        |
         * 0123456789abcdef0123456789abcdef
         * 
         * N = 2^32, n = 2^10, (I1, I2) 为 (i1, i2) 在 n 到 N 上的映射
         * I1 = xn + i1, I2 = yn + i2
         * I2 - I1 = yn + i2 + N - (xn + i1) = yn + N - xn + i2 - i1
         * used_size = (I2 - I1) & (n - 1)
        */
        uint32_t I1 = tpl_atomic_load(tpl_i1);
        uint32_t I2 = tpl_atomic_load(tpl_i2);
        uint32_t i1 = I1 & TPL_BUF_MAX_IDX;
        uint32_t i2 = I2 & TPL_BUF_MAX_IDX;
        uint32_t free_size = TPL_BUF_SIZE - ((I2 - I1) & TPL_BUF_MAX_IDX);

        // 空一个不放数据，以区分空载与满载
        if (1 == free_size)
            continue;
        uint32_t size = (i1 <= i2 ? TPL_BUF_SIZE - i2 : free_size) - 1;
        int rsize = tpl_read(tpl_buf + i2, size ? size : 1);
        if (rsize <= 0)
            tpl_pr("{%s}read failed, rsize: %d\n", __func__, rsize);
        else
            tpl_atomic_add(tpl_i2, rsize);
    }

    tpl_atomic_store(tpl_p_exit, 1);
}

static void tpl_consumer(void)
{
    uint8_t is_searched = 0;
    int frame_size = -1;
    while (tpl_atomic_load(tpl_run))
    {
        tpl_msleep(TPL_C_SLEEP_MS);

        uint32_t I1 = tpl_atomic_load(tpl_i1);
        uint32_t I2 = tpl_atomic_load(tpl_i2);
        uint32_t i1 = I1 & TPL_BUF_MAX_IDX;
        uint32_t i2 = I2 & TPL_BUF_MAX_IDX;
        uint32_t used_size = (I2 - I1) & TPL_BUF_MAX_IDX;

        if (used_size < TPL_FRAME_FIXED_SIZE)
            continue;
        if (frame_size > 0 && used_size < frame_size)
            continue;
        
        if (! is_searched)
        {
            int off = tpl_search_head(tpl_buf, i1, used_size);
            if (off >= 0)
            {
                is_searched = 1;
                tpl_atomic_add(tpl_i1, off);
            }
            else
                tpl_atomic_add(tpl_i1, used_size);
            
            continue;
        }

        if (frame_size <= 0)
        {
            frame_size = tpl_get_size(tpl_buf, i1, used_size);
            continue;
        }
        
        void* frame = tpl_mem_alloc(frame_size);
        if (NULL == frame)
        {
            tpl_pr("{%s}malloc failed\n", __func__);
            continue;
        }
        if (i1 < i2 || (i1 > i2 && TPL_BUF_SIZE - i1 >= frame_size))
            tpl_mem_cpy(frame, tpl_buf + i1, frame_size);
        else
        {
            tpl_mem_cpy(frame, tpl_buf + i1, TPL_BUF_SIZE - i1);
            tpl_mem_cpy(frame + TPL_BUF_SIZE - i1, tpl_buf, frame_size - (TPL_BUF_SIZE - i1));
        }
        tpl_atomic_add(tpl_i1, frame_size);
        tpl_check_todo(frame, frame_size);

        is_searched = 0;
        frame_size = -1;
    }

    tpl_atomic_store(tpl_c_exit, 1);
}

/**************************************** 应用部分 ****************************************/

// 调用顺序：init --> producer --> consumer --> exit，示例：

static void* p(void* arg)
{
    (void)arg;
    tpl_producer();
    return NULL;
}

static void* c(void* arg)
{
    (void)arg;
    tpl_consumer();
    return NULL;
}

void main(void)
{
    pthread_t tid[2];
    tpl_frame_transfer_init();
    pthread_create(tid, NULL, p, NULL);
    pthread_create(tid + 1, NULL, c, NULL);
    pthread_detach(tid[0]);
    pthread_detach(tid[1]);
    while (! tpl_atomic_load(tpl_p_exit) || ! tpl_atomic_load(tpl_c_exit))
    {
        tpl_msleep(TPL_P_SLEEP_MS + TPL_C_SLEEP_MS);
    }
    tpl_frame_transfer_exit();
}
