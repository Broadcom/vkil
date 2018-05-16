#ifndef VK_ERROR_H__
#define VK_ERROR_H__

#include <errno.h>
#include <stdint.h>

#define VKERROR_MAKE(layer,role,function,type)  \
(-(((abs(layer)&0x7F) << 24) | ((abs(role)&0xFF) << 16) | \
((abs(function)&0xFF) <<  8) | (((abs(type)&0xFF) << 0))))

typedef enum {
    VKAPI       = 0,
    VKIL        = 1,
    VKDRV       = 3,
    VKSIM       = 4,
    VKLAYER_MAX = 0x7F
}  vk_layer_t;

#endif