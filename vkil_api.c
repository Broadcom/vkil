#include <stdio.h>
#include <errno.h>
#include "vkil_api.h"

int32_t vkil_init(void ** handle)
{
    if (*handle==NULL) {
        // create the handle
        if (!(*handle = (void*)malloc(sizeof(vkil_context_essential))))
            goto fail_malloc;
        // we don't know anything yet on the component, just return the handle
    }
    else { // read the handle, to proper cast it, and start to init the component

      // here we gonna call the driver to effectivey load and init the component.

    }
    return 0;

fail_malloc:
    return ENOMEM;
};

int32_t vkil_deinit(void *handle)
{
    return 0;
};

int32_t  vkil_set_parameter(const void *handle, const int32_t field, const void *value)
{
    return 0;
};

int32_t  vkil_get_parameter(const void *handle, const int32_t field, void **value)
{
    return 0;
};

int32_t vkil_send_buffer(const void *component_handle, const void *buffer_handle, const enum vkil_command_type cmd)
{
    return 0;
};

int32_t vkil_receive_buffer(const void *component_handle, void **buffer_handle)
{
    return 0;
};

 // start dma operation
int32_t vkil_upload_buffer(const void *component_handle, const void *host_buffer, const enum vkil_command_type cmd)
{
    return 0;
};

int32_t vkil_download_buffer(const void *component_handle, const void *host_buffer)
{
    return 0;
};

 // poll dma operation status
int32_t vkil_uploaded_buffer(const void *component_handle, const void *host_buffer)
{
    return 0;
};


int32_t vkil_downloaded_buffer(const void *component_handle, const void *host_buffer)
{
    return 0;
};

vkil_api ilapi = {
    .init                  = vkil_init,
    .deinit                = vkil_deinit,
    .set_parameter         = vkil_set_parameter,
    .get_parameter         = vkil_get_parameter,
    .send_buffer           = vkil_send_buffer,
    .receive_buffer        = vkil_receive_buffer,
    .upload_buffer         = vkil_upload_buffer,
    .download_buffer       = vkil_download_buffer,
    .uploaded_buffer       = vkil_uploaded_buffer,
    .downloaded_buffer     = vkil_downloaded_buffer
};
