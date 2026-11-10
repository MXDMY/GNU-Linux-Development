
/*
 * m 模块（m.h）接口测试。
 * 覆盖 m_malloc / m_free / m_strstr / m_strcmp / m_strncmp 的正常路径、边界
 * 条件与错误路径。字符串比较接口只断言返回值符号（< 0 / 0 / > 0），不绑定
 * 具体差值，与 strcmp 系列的约定保持一致。
 *
 * 注意：本文件所属的可执行目标同时链接 m 与 mhttp，当前只测 m；mhttp 实现
 * 后可继续在本文件追加其接口测试。
*/

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "m.h"

static int g_passed;
static int g_failed;

#define CHECK(cond) \
    do \
    { \
        if (cond) \
            g_passed++; \
        else \
        { \
            g_failed++; \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

/*
 * 把整数归一化为 -1 / 0 / 1，便于只比较符号。
 * @param value 待归一化的值
 * @return 负数返回 -1，零返回 0，正数返回 1
*/
static int sign_of(int value)
{
    if (0 > value)
        return -1;
    if (0 < value)
        return 1;

    return 0;
}

/*
 * 断言返回值符号符合预期，失败时输出接口名、用例下标与归一化符号。
 * @param tag    接口名
 * @param index  用例下标
 * @param expect 期望符号
 * @param value  实际返回值
*/
static void expect_sign(const char* tag, size_t index, int expect, int value)
{
    if (expect == sign_of(value))
        g_passed++;
    else
    {
        g_failed++;
        fprintf(stderr, "FAIL %s case %zu: expect sign %d, got %d\n", tag, index, expect, sign_of(value));
    }
}

/*
 * 测试 m_malloc / m_free：可写性、对齐、零字节、超大申请、NULL 释放、
 * 多块分配互不重叠。
*/
static void test_m_malloc_free(void)
{
    static unsigned long const sizes[] = {1, 7, 64, 255, 4096, 65536};
    size_t i;

    // 不同大小分配后写入并回读首尾字节
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
    {
        unsigned char* p = (unsigned char*)m_malloc(sizes[i]);

        CHECK(NULL != p);
        if (! p)
            continue;

        memset(p, 0xA5, sizes[i]);
        CHECK(0xA5 == p[0]);
        CHECK(0xA5 == p[sizes[i] - 1]);

        m_free(p);
    }

    // 返回地址至少满足指针宽度的对齐
    {
        void* p = m_malloc(64);

        CHECK(NULL != p);
        if (p)
        {
            CHECK(0 == ((uintptr_t)p % sizeof(void*)));
            m_free(p);
        }
    }

    // 释放 NULL 不得崩溃
    m_free(NULL);

    // 0 字节申请：返回值可为 NULL 或唯一指针，仅要求可安全释放
    m_free(m_malloc(0));

    // 无法满足的超大申请应返回 NULL
    CHECK(NULL == m_malloc((unsigned long)-1));

    // 连续分配多块，地址互不重叠，再逐一释放
    {
        void* blocks[32];
        int k;

        memset(blocks, 0, sizeof(blocks));
        for (k = 0; k < 32; k++)
            blocks[k] = m_malloc(64);

        for (k = 0; k < 32; k++)
            CHECK(NULL != blocks[k]);

        for (k = 0; k < 32; k++)
        {
            int j;

            for (j = k + 1; j < 32; j++)
            {
                if (blocks[k] && blocks[j])
                    CHECK(blocks[k] != blocks[j]);
            }
        }

        for (k = 0; k < 32; k++)
            m_free(blocks[k]);
    }
}

typedef struct
{
    const char* text;
    const char* pattern;
    int cs;     // 非零为区分大小写
    int found;  // 1 期望找到，0 期望返回 NULL
    int index;  // 找到时的相对下标
} strstr_case;

/*
 * 测试 m_strstr：区分/不区分大小写、KMP 失配回退、首次出现、空串与 NULL。
*/
static void test_m_strstr(void)
{
    static const strstr_case cases[] =
    {
        {"hello world", "world", 1, 1, 6},
        {"Hello World", "World", 1, 1, 6},
        {"Hello World", "world", 0, 1, 6},
        {"Hello World", "world", 1, 0, -1},
        {"Hello World", "WORLD", 0, 1, 6},
        {"abcabcabd", "abcabd", 1, 1, 3},
        {"aaaaab", "aaab", 1, 1, 2},
        {"abababab", "ababab", 1, 1, 0},
        {"abababab", "abababa", 1, 1, 0},
        {"xxhello", "hello", 1, 1, 2},
        {"hello", "hello", 1, 1, 0},
        {"hello", "hell", 1, 1, 0},
        {"hello", "hellox", 1, 0, -1},
        {"hello", "", 1, 0, -1},
        {"", "a", 1, 0, -1},
        {"", "", 1, 0, -1},
        {"a", "A", 0, 1, 0},
        {"a", "A", 2, 0, -1},
        {"x1y2z", "1Y2", 0, 1, 1},
        {"abc", "c", 1, 1, 2},
        {"abc", "d", 1, 0, -1},
        {"ABAB", "abab", 0, 1, 0},
        {"ABAB", "ab", 0, 1, 0},
        {"zzzabczzz", "abc", 1, 1, 3},
        {"abcabc", "abc", 1, 1, 0},
        {"aBcAbC", "abc", 0, 1, 0},
        {"aBcAbC", "abc", 1, 0, -1},
        {"aAab", "Aab", 0, 1, 1},
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        const strstr_case* c = &cases[i];
        char* got = m_strstr(c->text, c->pattern, c->cs);
        int ok;

        if (c->found)
            ok = (NULL != got && got == c->text + c->index);
        else
            ok = (NULL == got);

        if (ok)
            g_passed++;
        else
        {
            g_failed++;
            fprintf(stderr, "FAIL m_strstr case %zu: text=[%s] pattern=[%s] cs=%d, expect %s\n",
                i, c->text, c->pattern, c->cs, c->found ? "found" : "NULL");
        }
    }

    // NULL 与空串按接口约定返回 NULL
    CHECK(NULL == m_strstr(NULL, "a", 1));
    CHECK(NULL == m_strstr("a", NULL, 1));
    CHECK(NULL == m_strstr(NULL, NULL, 1));
    CHECK(NULL == m_strstr("", "a", 1));
    CHECK(NULL == m_strstr("a", "", 1));
    CHECK(NULL == m_strstr("", "", 1));

    // 不得改动入参内容
    {
        char text[] = "Hello World";
        char pattern[] = "world";
        char text_copy[sizeof(text)];
        char pattern_copy[sizeof(pattern)];
        char* got;

        memcpy(text_copy, text, sizeof(text));
        memcpy(pattern_copy, pattern, sizeof(pattern));

        got = m_strstr(text, pattern, 0);
        CHECK(NULL != got);
        CHECK(got == text + 6);
        CHECK(0 == memcmp(text, text_copy, sizeof(text)));
        CHECK(0 == memcmp(pattern, pattern_copy, sizeof(pattern)));
    }

    // 与 libc strstr 做差分对照（区分大小写、非空模式）
    {
        static const char* const texts[] =
        {
            "", "a", "ab", "abc", "abcabc", "abababab", "hello world", "zzzabczzz",
        };
        static const char* const patterns[] =
        {
            "a", "ab", "abc", "abcd", "b", "zzz", "world", "abab",
        };
        size_t x;
        size_t y;

        for (x = 0; x < sizeof(texts) / sizeof(texts[0]); x++)
        {
            for (y = 0; y < sizeof(patterns) / sizeof(patterns[0]); y++)
            {
                CHECK(strstr(texts[x], patterns[y]) == m_strstr(texts[x], patterns[y], 1));
            }
        }
    }

    // 长文本末尾命中，覆盖内部动态分配路径
    {
        enum { BIG_TEXT_SIZE = 1024 };
        static char big_text[BIG_TEXT_SIZE + 1];
        char* got;

        memset(big_text, 'x', BIG_TEXT_SIZE);
        big_text[BIG_TEXT_SIZE] = '\0';
        memcpy(big_text + BIG_TEXT_SIZE - 6, "needle", 6);

        got = m_strstr(big_text, "needle", 1);
        CHECK(NULL != got);
        CHECK(got == big_text + BIG_TEXT_SIZE - 6);
    }
}

typedef struct
{
    const char* s1;
    const char* s2;
    int cs;
    int sign;
} cmp_case;

/*
 * 测试 m_strcmp：相等、大小写、前后缀、NULL、空串，并与 libc 差分对照。
*/
static void test_m_strcmp(void)
{
    static const cmp_case cases[] =
    {
        {"abc", "abc", 1, 0},
        {"ABC", "abc", 0, 0},
        {"HeLLo", "hello", 0, 0},
        {"aBc", "AbC", 0, 0},
        {"abc", "ABC", 1, 1},
        {"ABC", "abc", 1, -1},
        {"abc", "abd", 1, -1},
        {"abd", "abc", 1, 1},
        {"abc", "abcd", 1, -1},
        {"abcd", "abc", 1, 1},
        {"", "", 1, 0},
        {"", "a", 1, -1},
        {"a", "", 1, 1},
        {"HeLLo", "hellp", 0, -1},
        {"HeLLo", "hellp", 1, -1},
        {"abc", "ABC", 2, 1},
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        const cmp_case* c = &cases[i];

        expect_sign("m_strcmp", i, c->sign, m_strcmp(c->s1, c->s2, c->cs));
    }

    // NULL 参与比较时的约定：均为 NULL 相等，单边 NULL 一侧大/小
    CHECK(0 == sign_of(m_strcmp(NULL, NULL, 1)));
    CHECK(0 < sign_of(m_strcmp("a", NULL, 1)));
    CHECK(0 > sign_of(m_strcmp(NULL, "a", 1)));

    // 不得改动入参内容
    {
        char a[] = "abc";
        char b[] = "abd";
        char a_copy[sizeof(a)];
        char b_copy[sizeof(b)];

        memcpy(a_copy, a, sizeof(a));
        memcpy(b_copy, b, sizeof(b));

        CHECK(0 > m_strcmp(a, b, 0));
        CHECK(0 == memcmp(a, a_copy, sizeof(a)));
        CHECK(0 == memcmp(b, b_copy, sizeof(b)));
    }

    // 与 libc strcmp 做差分对照（区分大小写）
    {
        static const char* const texts[] =
        {
            "", "a", "ab", "abc", "abd", "abcd", "zzz", "ZZZ", "aBc", "AbC",
        };
        size_t x;
        size_t y;

        for (x = 0; x < sizeof(texts) / sizeof(texts[0]); x++)
        {
            for (y = 0; y < sizeof(texts) / sizeof(texts[0]); y++)
            {
                CHECK(sign_of(strcmp(texts[x], texts[y])) == sign_of(m_strcmp(texts[x], texts[y], 1)));
            }
        }
    }
}

typedef struct
{
    const char* s1;
    const char* s2;
    unsigned long n;
    int cs;
    int sign;
} ncmp_case;

/*
 * 测试 m_strncmp：长度截断、大小写、NULL、n 为 0，并与 libc 差分对照。
*/
static void test_m_strncmp(void)
{
    static const ncmp_case cases[] =
    {
        {"abc", "abc", 0, 1, 0},
        {"abc", "abd", 1, 1, 0},
        {"abc", "abd", 2, 1, 0},
        {"abc", "abd", 3, 1, -1},
        {"abc", "abd", 100, 1, -1},
        {"abc", "abcd", 3, 1, 0},
        {"abc", "abcd", 4, 1, -1},
        {"abcd", "abc", 4, 1, 1},
        {"ABC", "abd", 2, 0, 0},
        {"ABC", "abd", 3, 0, -1},
        {"A", "a", 1, 0, 0},
        {"abC", "abc", 3, 0, 0},
        {"abcX", "abcY", 3, 1, 0},
        {"abcX", "abcY", 4, 1, -1},
        {"HeLLo", "hello", 10, 0, 0},
        {"HeLLo", "hellp", 10, 0, -1},
        {"abc", "ABC", 10, 2, 1},
        {"abc", "ABCD", 3, 1, 1},
        {"", "", 10, 1, 0},
        {"", "a", 10, 1, -1},
        {"a", "", 10, 1, 1},
        {"a", "abcdef", 1, 1, 0},
        {"abcdef", "a", 1, 1, 0},
        {"abcdef", "a", 100, 1, 1},
        {"a", "abcdef", 100, 1, -1},
        {"ab", "ab", 100, 0, 0},
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        const ncmp_case* c = &cases[i];

        expect_sign("m_strncmp", i, c->sign, m_strncmp(c->s1, c->s2, c->n, c->cs));
    }

    // NULL 与 n 为 0 的约定；n 为 0 时优先返回 0
    CHECK(0 == sign_of(m_strncmp(NULL, NULL, 5, 1)));
    CHECK(0 < sign_of(m_strncmp("a", NULL, 5, 1)));
    CHECK(0 > sign_of(m_strncmp(NULL, "a", 5, 1)));
    CHECK(0 == sign_of(m_strncmp(NULL, "a", 0, 1)));
    CHECK(0 == sign_of(m_strncmp(NULL, NULL, 0, 1)));

    // 不得改动入参内容
    {
        char a[] = "abcX";
        char b[] = "abcY";
        char a_copy[sizeof(a)];
        char b_copy[sizeof(b)];

        memcpy(a_copy, a, sizeof(a));
        memcpy(b_copy, b, sizeof(b));

        CHECK(0 == m_strncmp(a, b, 3, 0));
        CHECK(0 == memcmp(a, a_copy, sizeof(a)));
        CHECK(0 == memcmp(b, b_copy, sizeof(b)));
    }

    // 与 libc strncmp 做差分对照（区分大小写，覆盖多种长度）
    {
        static const char* const texts[] =
        {
            "", "a", "ab", "abc", "abd", "abcd", "abcdX", "zzz",
        };
        static unsigned long const lens[] = {0, 1, 2, 3, 5, 100};
        size_t x;
        size_t y;
        size_t z;

        for (x = 0; x < sizeof(texts) / sizeof(texts[0]); x++)
        {
            for (y = 0; y < sizeof(texts) / sizeof(texts[0]); y++)
            {
                for (z = 0; z < sizeof(lens) / sizeof(lens[0]); z++)
                {
                    CHECK(sign_of(strncmp(texts[x], texts[y], lens[z])) == sign_of(m_strncmp(texts[x], texts[y], lens[z], 1)));
                }
            }
        }
    }
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    test_m_malloc_free();
    test_m_strstr();
    test_m_strcmp();
    test_m_strncmp();

    printf("mhttp_test/m: %d passed, %d failed\n", g_passed, g_failed);

    return g_failed ? 1 : 0;
}

