#include <stdio.h>
#include <stdlib.h>
#include "vkil_api.h"
#include "vkil_error.h"
#include "vkil_backend.h"

#include "vkdrv_access.h" // TODO: OK at this time the driver is a shared library !

#include <assert.h>

#define vk_assert0 assert

// this structure is copied thru PCIE bridge and is currenlty limited to 16 bytes
typedef struct _vkil_context_internal
{
    void *      fd_dummy;
} vkil_context_internal;

int32_t vkil_init(void ** handle)
{
    printf("[VKIL] vkil_init %x \n", *handle);
    if (*handle==NULL) {
        // create the handle
        if (!(*handle = (void*)malloc(sizeof(vkil_context))))
            goto fail_malloc;
        memset(*handle,0,sizeof(vkil_context));
        // we don't know anything yet on the component, just return the handle
    }
    else{
        vkil_context *ilctx = (vkil_context *)(* handle);
        if (ilctx->context_essential.component_role && !ilctx->priv_data)
        {
            vkil_context_internal * ilpriv;
            vk_comm_from_host message;
            // we knwo the component, but this one has not been created yet

            // the priv_data structure size could be component specific
            if (!(ilctx->priv_data = (void*)malloc(sizeof(vkil_context_internal))))
                goto fail_malloc;
            memset(ilctx->priv_data,0,sizeof(vkil_context_internal));
            ilpriv = (vkil_context_internal *) ilctx->priv_data;
            // instanciate the driver
            ilpriv->fd_dummy = vkdrv_open();
            printf("[VKIL] %s driver inited %x\n", __FUNCTION__, ilpriv->fd_dummy);

            message.queue_id    = ilctx->context_essential.queue_id;
            message.function_id = vkil_get_function_id("init");
            message.context_id = (int32_t)handle;
            message.args[0] = ilctx->context_essential.component_role;
            vkdrv_write(ilpriv->fd_dummy,&message,sizeof(message));
            // here, we will need to wait for the HW to start to create the component
            // poll_wait() wait for the hw to complete (interrupt or probing).
            // vkdrv_read(ilpriv->fd_dummy,&message,sizeof(message)); to get the status of the hw.
            printf("[VKIL] %s card inited %x\n", __FUNCTION__, ilpriv->fd_dummy);
        }
    }
    return 0;

fail_malloc:
    return VKILERROR(ENOMEM);

};

int32_t vkil_deinit(void *handle)
{
    vkil_context *ilctx = (vkil_context *)(handle);
    vk_assert0(handle);
    printf("[VKIL] vkil_deinit\n");
    if (ilctx->priv_data){
        vkil_context_internal * ilpriv = ilctx->priv_data;
        if (ilpriv->fd_dummy){
            vkdrv_close(ilpriv->fd_dummy);
        }
    }
    free(ilctx);
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


 // start dma operation
int32_t vkil_upload_buffer(const void *component_handle, const void *host_buffer, const vkil_command_t cmd)
{
    vkil_context *ilctx = (vkil_context *)(component_handle);
    vkil_context_internal *ilpriv;
    vk_comm_from_host message;

    printf("[VKIL] vkil_upload_buffer\n");

    //sanity check
    vk_assert0(component_handle);
    vk_assert0(host_buffer);
    vk_assert0(cmd);
    ilpriv = (vkil_context_internal *) ilctx->priv_data;
    vk_assert0(ilpriv);

    // here we need to write the dma command
    message.queue_id    = ilctx->context_essential.queue_id;
    message.function_id = vkil_get_function_id("copy_buffer");
    message.context_id = component_handle;
    message.args[0] = host_buffer; // TODO: to clarify
    message.args[1] = VK_CMD_UPLOAD;

    return vkdrv_write(ilpriv->fd_dummy,&message,sizeof(message));
};

int32_t vkil_download_buffer(const void *component_handle, void **host_buffer, const vkil_command_t cmd)
{
    printf("[VKIL] vkil_download_buffer\n");
    vk_assert0(component_handle);
    vk_assert0(host_buffer);
    vk_assert0(cmd);
    return 0;
};

 // poll dma operation status
int32_t vkil_uploaded_buffer(const void *component_handle, const void *host_buffer, const vkil_command_t cmd)
{
    printf("[VKIL] vkil_uploaded_buffer\n");
    vk_assert0(component_handle);
    vk_assert0(host_buffer);
    vk_assert0(cmd);
    return 0;
};

int32_t vkil_downloaded_buffer(const void *component_handle, const void *host_buffer, const vkil_command_t cmd)
{
    printf("[VKIL] vkil_downloaded_buffer\n");
    vk_assert0(component_handle);
    vk_assert0(host_buffer);
    vk_assert0(cmd);
    return 0;
};

int32_t vkil_send_buffer(const void *component_handle, const void *buffer_handle, const vkil_command_t cmd)
{
    printf("[VKIL] vkil_send_buffer\n");
    vk_assert0(component_handle);
    vk_assert0(buffer_handle);
    vk_assert0(cmd);
    switch (cmd) {
        // untunneled operations
        case VK_CMD_UPLOAD:
            vkil_upload_buffer(component_handle, buffer_handle, cmd);
            // here we need to wait the buffer has effectively been uploaded
            return vkil_uploaded_buffer(component_handle, buffer_handle, VK_CMD_BLOCKING);
        default:
            break;
    }
    // tunneled operations
    return 0;
};

int32_t vkil_receive_buffer(const void *component_handle, void **buffer_handle, const vkil_command_t cmd)
{
    printf("[VKIL] vkil_receive_buffer\n");
    vk_assert0(component_handle);
    vk_assert0(buffer_handle);
    switch (cmd) {
        // untunneled operations
        case VK_CMD_DOWNLOAD:
            vkil_download_buffer(component_handle, buffer_handle, cmd);
            // here we need to wait the buffer has effectively been uploaded
            return vkil_uploaded_buffer(component_handle, buffer_handle, VK_CMD_BLOCKING);
        default:
            break;
    }
    // tunneled operations
    return 0;
};

void* vkil_create_api(void)
{
    printf("[VKIL] %s\n",__FUNCTION__);
    vkil_api* ilapi = (vkil_api*) malloc(sizeof(vkil_api));
    if(!ilapi)
        return NULL;
    *ilapi = (vkil_api) {
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
    return ilapi;
}

int vkil_destroy_api(void* ilapi)
{
    printf("[VKIL] %s\n",__FUNCTION__);
    if((vkil_api*) ilapi)
        free(ilapi);
    return 0;
}