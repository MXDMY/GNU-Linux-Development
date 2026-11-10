
#include "m.h"

#include <stdint.h>
#include <ctype.h>
#include <string.h>

/*
 * 构造 next 数组，定义如下：
 * 对于模式串 p，next[i] 表示子串 p[0..i] 中，最长的相等前缀和后缀的长度，例如模式串：
 * p = "ababaca"
 * i    子串        最长相等前后缀    next[i]
 * 0    a          -               0
 * 1    ab         -               0
 * 2    aba        "a"             1
 * 3    abab       "ab"            2
 * 4    ababa      "aba"           3
 * 5    ababac     -               0
 * 6    ababaca    "a"             1
 * 所以：next = [0, 0, 1, 2, 3, 0, 1]
 * 有些实现会把 next 数组整体右移一位，或者用 -1 表示不存在，这是写法差异，本质相同
*/

static void build_next(const char* p, int m, int* next, int cs)
{
    next[0] = 0;
    int len = 0; // 当前最长相等前后缀长度
    int i = 1;

    while (i < m)
    {
        if (cs ? p[i] == p[len] : tolower((uint8_t)p[i]) == tolower((uint8_t)p[len]))
        {
            len++;
            next[i] = len;
            i++;
        }
        else
        {
            if (len > 0)
                len = next[len - 1];
            else
            {
                next[i] = 0;
                i++;
            }
        }
    }
}

/*
 * KMP 算法（Knuth-Morris-Pratt）是一种线性时间字符串匹配算法，用于在文本串中查找模式串
 * 第一次出现的位置。时间复杂度为 O(n + m)，其中 n 是文本长度，m 是模式长度。相比朴素匹配
 * 的 O(n * m)，KMP 利用已经匹配的信息，避免文本指针回溯。匹配过程如下：
 * 
 * 设文本串为 s，模式串为 p
 * i 指向文本串当前字符
 * j 指向模式串当前已匹配的长度
 * 规则：
 *     如果 s[i] == p[j]，则 i++, j++
 *     如果 j == m，说明匹配成功
 *     如果 s[i] != p[j]：
 *         若 j > 0，则 j = next[j - 1]，即利用已匹配的前后缀信息，模式串向右滑动
 *         若 j == 0，则 i++
 * 文本指针 i 从不回退
*/

char* m_strstr(const char* s, const char* p, int cs)
{
    if (! s || ! p)
        return NULL;

    int n = (int)strlen(s);
    int m = (int)strlen(p);

    if (0 == n || 0 == m)
        return NULL;

    int* next = (int*)m_malloc(sizeof(int) * m);
    if (! next)
        return NULL;
    build_next(p, m, next, cs);

    int i = 0;
    int j = 0;
    while (i < n)
    {
        if (! (cs ? s[i] - p[j] : tolower((uint8_t)s[i]) - tolower((uint8_t)p[j])))
        {
            i++;
            j++;
            if (j == m)
            {
                m_free(next);
                return s + i - m;
            }
        }
        else
        {
            if (j > 0)
                j = next[j - 1];
            else
                i++;
        }
    }

    m_free(next);
    return NULL;
}

int m_strcmp(const char* s1, const char* s2, int cs)
{
    if (s1 == s2)
        return 0;
    if (s1 && ! s2)
        return 1;
    if (! s1 && s2)
        return -1;

    int i = 0;
    while ('\0' != s1[i] && '\0' != s2[i])
    {
        int r = cs ? s1[i] - s2[i] : tolower((uint8_t)s1[i]) - tolower((uint8_t)s2[i]);

        if (r > 0)
            return 1;
        else if (r < 0)
            return -1;
        else
            i++;
    }

    return s1[i] - s2[i];
}

int m_strncmp(const char* s1, const char* s2, unsigned long n, int cs)
{
    if (s1 == s2 || 0 == n)
        return 0;
    if (s1 && ! s2)
        return 1;
    if (! s1 && s2)
        return -1;

    int i = 0;
    while ('\0' != s1[i] && '\0' != s2[i] && i < n - 1)
    {
        int r = cs ? s1[i] - s2[i] : tolower((uint8_t)s1[i]) - tolower((uint8_t)s2[i]);

        if (r > 0)
            return 1;
        else if (r < 0)
            return -1;
        else
            i++;
    }

    return cs ? s1[i] - s2[i] : tolower((uint8_t)s1[i]) - tolower((uint8_t)s2[i]);
}
