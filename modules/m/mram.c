
#include "m.h"

#include <stdlib.h>

void* m_malloc(unsigned long size)
{
    return malloc(size);
}

void m_free(void* ptr)
{
    free(ptr);
}
