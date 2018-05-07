#ifndef vk_utils_h__
#define vk_utils_h__

#include <stdlib.h>
#include <assert.h>

#define VK_ALIGN 16

#define vk_assert0 assert
#define vk_assert1 assert
#define vk_assert2 assert

#define VK_LOG_PANIC  (0)
#define VK_LOG_ERROR  (16)
#define VK_LOG_INFO   (32)
#define VK_LOG_DEBUG  (64)


static int vk_log_level = VK_LOG_DEBUG;



int vk_malloc(void ** ptr, size_t size);
int vk_mallocz(void ** ptr, size_t size);
void vk_free(void ** ptr);
void vk_log(const char * prefix, int level, const char *fmt, ...);
#endif //vk_utils_h__
