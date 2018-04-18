#include <stdio.h>
#include <stdlib.h>
#include "vkil_api.h"
#include "vkil_error.h"

#include <assert.h>

#define vk_assert0 assert

int32_t vkil_init(void ** handle)
{
    printf("[VKIL] vkil_init\n");
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
    return VKILERROR(ENOMEM);

};

int32_t vkil_deinit(void *handle)
{
    printf("[VKIL] vkil_deinit\n");
    vk_assert0(handle);
    return 0;
};

int32_t vkil_set_parameter(const void *handle, const int32_t field, const void *value)
{
    printf("[VKIL] vkil_set_parameter\n");
    vk_assert0(handle);
    vk_assert0(field);
    vk_assert0(value);
    return 0;
};

int32_t vkil_get_parameter(const void *handle, const int32_t field, void **value)
{
    printf("[VKIL] vkil_get_parameter\n");
    vk_assert0(handle);
    vk_assert0(field);
    vk_assert0(value);
    return 0;
};

int32_t vkil_send_buffer(const void *component_handle, const void *buffer_handle, const vkil_command_t cmd)
{
    printf("[VKIL] vkil_send_buffer\n");
    vk_assert0(component_handle);
    vk_assert0(buffer_handle);
    vk_assert0(cmd);
    return 0;
};

int32_t vkil_receive_buffer(const void *component_handle, void **buffer_handle)
{
    printf("[VKIL] vkil_receive_buffer\n");
    vk_assert0(component_handle);
    vk_assert0(buffer_handle);
    return 0;
};

 // start dma operation
int32_t vkil_upload_buffer(const void *component_handle, const void *host_buffer, const vkil_command_t cmd)
{
    printf("[VKIL] vkil_upload_buffer\n");
    vk_assert0(component_handle);
    vk_assert0(host_buffer);
    vk_assert0(cmd);
    return 0;
};

int32_t vkil_download_buffer(const void *component_handle, const void *host_buffer)
{
    printf("[VKIL] vkil_download_buffer\n");
    vk_assert0(component_handle);
    vk_assert0(host_buffer);
    return 0;
};

 // poll dma operation status
int32_t vkil_uploaded_buffer(const void *component_handle, const void *host_buffer)
{
    printf("[VKIL] vkil_uploaded_buffer\n");
    vk_assert0(component_handle);
    vk_assert0(host_buffer);
    return 0;
};

int32_t vkil_downloaded_buffer(const void *component_handle, const void *host_buffer)
{
    printf("[VKIL] vkil_downloaded_buffer\n");
    vk_assert0(component_handle);
    vk_assert0(host_buffer);
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
