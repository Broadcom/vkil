#ifndef vkil_error_h__
#define vkil_error_h__


#include "vk_error.h"

#define EOL "endoflist"


static int32_t vkil_error(char * functionname)
{
    static char * vkil_fun_list[] = {
                        "undefined",
                        "vkil_init",
                        "vkil_deinit",
                        "vkil_set_parameter",
                        "vkil_get_parameter",
                        "vkil_send_buffer",
                        "vkil_receive_buffer",
                        "vkil_upload_buffer",
                        "vkil_download_buffer",
                        "vkil_uploaded_buffer",
                        "vkil_downloaded_buffer",
                        EOL};
    int32_t i=0;
    do{
        i++;
        if (!strcmp(vkil_fun_list[i],functionname))
            return i;
    } while (strcmp(vkil_fun_list[i],EOL));
    return 0;
};


#define VKILERROR(type) VKERROR_MAKE(VKIL,0,vkil_error(__FUNCTION__),type)


#endif  // vkil_error_h__