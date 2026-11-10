/*
 * 跨平台的算法、数据结构或其它公共函数等，
 * 用于解决某些平台默认的 C 库无法满足需求的情况，
 * 包括但不限于以下情况（以 C99 为基准）：
 * - 默认的 malloc 可分配的堆内存过小；
 * - 字符串操作无区分大小写版本；
 * - 某些函数需要特殊平台、扩展或更高版本的 C 标准才提供；
*/

#ifndef M_2026_H
#define M_2026_H

#ifdef __cplusplus
extern "C" {
#endif

extern void* m_malloc(unsigned long size);

extern void m_free(void* ptr);

extern char* m_strstr(const char* s, const char* p, int cs);

extern int m_strcmp(const char* s1, const char* s2, int cs);

extern int m_strncmp(const char* s1, const char* s2, unsigned long n, int cs);


#ifdef __cplusplus
}
#endif

#endif
