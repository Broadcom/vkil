#include <stdio.h>
#include <stdarg.h>
#include "vkil_utils.h"
#include <string.h>

// handle are stored into a hash table
int vk_malloc(void ** ptr, size_t size)
{
    // we use posix_memealign because it provide the more robust way to allocate memory
    // see http://man7.org/linux/man-pages/man3/posix_memalign.3.html
    return posix_memalign(ptr,VK_ALIGN,size);
}

int vk_mallocz(void ** ptr, size_t size)
{
    int ret;
    ret = vk_malloc(ptr,size);
    if (!ret)
        memset(*ptr, 0, size);
    return ret;
}

void vk_free(void ** ptr)
{
    free(*ptr);
    *ptr = NULL; // that prevent some issue. since the validity of pointer is often meachecked by its non "NULL" value
}

void vk_log(const char * prefix, int level, const char *fmt, ...)
{
    int length;
    if (level <= vk_log_level)
    {
        static char buffer[512]; // a line big enough
        va_list vl;
        va_start(vl, fmt);
        length = vsnprintf(buffer, sizeof(buffer)-2, fmt, vl); // the max length is the buffer size - 2 formatting parameters:
                                                               // '\0' to terminate the  string
                                                               // '\r' in case this one is not included
        va_end(vl);
        vk_assert0(length>=0); // if negative, means soemthing got wrong
        if (buffer[strlen(buffer)-1] != '\n')
            strcat(buffer,"\n");
        if (level<VK_LOG_INFO)
             printf("\x1B[31m");
        else if (level>VK_LOG_INFO)
            printf("\x1B[32m");
        printf("%s:%s\x1B[0m",prefix,buffer);
    }
}