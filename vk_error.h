#ifndef vk_error_h__
#define vk_error_h__

#include <stdint.h>
#include <errno.h>

#define VKERROR_MAKE(layer,role,function,type)  \
(-(((layer&0x7F)<<24) | ((role&0xFF)<<16) | \
((function&0xFF)<<8)|  (((type&0xFF)<<0))))


typedef enum {
    VKAPI       = 0,
    VKIL	    = 1,
    VKDRV	    = 3,
    VKLAYER_MAX = 0x7F	
}  vk_layer_t;




#endif  // vkil_error_h__