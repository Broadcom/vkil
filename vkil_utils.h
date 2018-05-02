#ifndef vkil_utils_h__
#define vkil_utils_h__

#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <string.h>

#define VK_ALIGN 16

#define vk_assert0 assert
#define vk_assert1 assert
#define vk_assert2 assert

#define VK_LOG_PANIC  (0)
#define VK_LOG_ERROR  (16)
#define VK_LOG_INFO   (32)
#define VK_LOG_DEBUG  (64)


static int vk_log_level = VK_LOG_DEBUG;



#define vkil_log(...) vk_log(__FUNCTION__,__VA_ARGS__)

int vk_malloc(void ** ptr, size_t size)
{
    // we use posix_memealign because it provide the more robust way to allocate memory
    // see http://man7.org/linux/man-pages/man3/posix_memalign.3.html
    return posix_memalign(ptr,VK_ALIGN,size);
};


int vk_mallocz(void ** ptr, size_t size)
{
    int ret;
    ret = vk_malloc(ptr,size);
    if (!ret)
        memset(*ptr, 0, size);
    return ret;
};

void vk_free(void ** ptr)
{
    free(*ptr);
    *ptr = NULL; // that prevent some issue. since the validity of pointer is often meachecked by its non "NULL" value
};

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
};
#endif //vkil_utils_h__
