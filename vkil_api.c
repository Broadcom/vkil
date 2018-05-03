#include <stdio.h>
#include <stdlib.h>
#include "vkil_api.h"

#include "vkil_error.h"
#include "vkil_utils.h"
#include "vkil_backend.h"
#include "vkil_session.h"

#include "vkdrv_access.h" // TODO: OK at this time the driver is a shared library !




// this structure is copied thru PCIE bridge and is currenlty limited to 16 bytes
typedef struct _vkil_context_internal
{
    void *      fd_dummy;
} vkil_context_internal;

int32_t vkil_init(void ** handle)
{
    vkil_log(VK_LOG_DEBUG,"");
    int ret = 0;
    if (*handle==NULL) {
        // create the handle
        if ((ret=vk_mallocz(handle,sizeof(vkil_context)))!=0)
                goto fail_malloc;
        // we don't know anything yet on the component, just return the handle
    }
    else{
        vkil_context *ilctx = (vkil_context *)(* handle);
        ilctx->context_essential.handle = 0; // this is defined by the vk card (expected to be the address on the valkyrie card)
        if ((ilctx->context_essential.session_id = vkil_get_session_id()) < 0)
            goto fail_session;
        if ((ilctx->context_essential.card_id = vkil_get_card_id()) < 0)
            goto fail_session;
        vkil_log(VK_LOG_DEBUG,"session_id: %i\n", ilctx->context_essential.session_id);
        vkil_log(VK_LOG_DEBUG,"card_id: %i\n", ilctx->context_essential.card_id);
        if (ilctx->context_essential.component_role && !ilctx->priv_data)
        {
            vkil_context_internal * ilpriv;
            host2vk_msg message;
            // we knwo the component, but this one has not been created yet
            // the priv_data structure size could be component specific
            if ((ret=vk_mallocz(&ilctx->priv_data,sizeof(vkil_context_internal)))!=0)
                goto fail_malloc;
            ilpriv = (vkil_context_internal *) ilctx->priv_data;
            // instanciate the driver
            ilpriv->fd_dummy = vkdrv_open();
            message.queue_id    = ilctx->context_essential.queue_id;
            message.function_id = vkil_get_function_id("init");
            message.size = 0;
            message.context_id = ilctx->context_essential.handle;
            message.args[0] = ilctx->context_essential.component_role;
            vkil_log(VK_LOG_DEBUG,"&message %x, &message.function_id %x, message.size %x\n",&message, &message.function_id, &message.size);
            vkdrv_write(ilpriv->fd_dummy,&message,sizeof(message));
            // here, we will need to wait for the HW to start to create the component
            // poll_wait() wait for the hw to complete (interrupt or probing).
            // vkdrv_read(ilpriv->fd_dummy,&message,sizeof(message)); to get the status of the hw.
            vkil_log(VK_LOG_DEBUG,"card inited %x\n",ilpriv->fd_dummy);
        }
    }
    return 0;

fail_malloc:
    vkil_log(VK_LOG_ERROR,"failed malloc");
    return VKILERROR(ret);

fail_session:
    return VKILERROR(ENOSPC);

};

int32_t vkil_deinit(void *handle)
{
    vkil_context *ilctx = (vkil_context *)(handle);
    vk_assert0(handle);
    vkil_log(VK_LOG_DEBUG,"");
    if (ilctx->priv_data){
        vkil_context_internal * ilpriv = ilctx->priv_data;
        if (ilpriv->fd_dummy){
            vkdrv_close(ilpriv->fd_dummy);
        }
        vk_free((void**)&ilpriv);
    }
    vk_free(&handle);
    return 0;
};

int32_t vkil_set_parameter(const void *handle, const int32_t field, const void *value)
{
    vkil_log(VK_LOG_DEBUG,"");
    vk_assert0(handle);
    vk_assert0(field);
    vk_assert0(value);
    return 0;
};

int32_t vkil_get_parameter(const void *handle, const int32_t field, void **value)
{
    vkil_log(VK_LOG_DEBUG,"");
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
    host2vk_msg message;

    vkil_log(VK_LOG_DEBUG,"");

    //sanity check
    vk_assert0(component_handle);
    vk_assert0(host_buffer);
    vk_assert0(cmd);
    ilpriv = (vkil_context_internal *) ilctx->priv_data;
    vk_assert0(ilpriv);

    // here we need to write the dma command
    message.queue_id    = ilctx->context_essential.queue_id;
    message.function_id = vkil_get_function_id("send_buffer");
    message.context_id = ilctx->context_essential.handle;
    message.size = 0;
    message.args[0] = host_buffer; // TODO: to clarify
    message.args[1] = VK_CMD_UPLOAD;
    return vkdrv_write(ilpriv->fd_dummy,&message,sizeof(message));


};

int32_t vkil_download_buffer(const void *component_handle, void **host_buffer, const vkil_command_t cmd)
{
    vkil_log(VK_LOG_DEBUG,"");
    vk_assert0(component_handle);
    vk_assert0(host_buffer);
    vk_assert0(cmd);
    return 0;
};

 // poll dma operation status
int32_t vkil_uploaded_buffer(const void *component_handle, const void *host_buffer, const vkil_command_t cmd)
{
    vkil_log(VK_LOG_DEBUG,"");
    vk_assert0(component_handle);
    vk_assert0(host_buffer);
    vk_assert0(cmd);
    return 0;
};

int32_t vkil_downloaded_buffer(const void *component_handle, const void *host_buffer, const vkil_command_t cmd)
{
    vkil_log(VK_LOG_DEBUG,"");
    vk_assert0(component_handle);
    vk_assert0(host_buffer);
    vk_assert0(cmd);
    return 0;
};

int32_t vkil_send_buffer(const void *component_handle, const void *buffer_handle, const vkil_command_t cmd)
{
    vkil_log(VK_LOG_DEBUG,"");
    //sanity check
    vk_assert0(component_handle);
    vk_assert0(buffer_handle);
    switch (cmd&VK_CMD_MAX) {
        // untunneled operations
        case VK_CMD_UPLOAD:
            vkil_upload_buffer(component_handle, buffer_handle, cmd);
            // here we need to wait the buffer has effectively been uploaded
            if (cmd&VK_CMD_BLOCKING) {
                // here we need to wait the buffer has effectively been uploaded
                return vkil_uploaded_buffer(component_handle, buffer_handle, VK_CMD_BLOCKING);
            }
            break;
        default:
            // tunneled operations
            {
                vkil_context *ilctx = (vkil_context *)(component_handle);
                vkil_context_internal *ilpriv;
                host2vk_msg message;
                ilpriv = (vkil_context_internal *) ilctx->priv_data;
                vk_assert0(ilpriv);

                message.queue_id    = ilctx->context_essential.queue_id;
                message.function_id = vkil_get_function_id("send_buffer");
                message.context_id = ilctx->context_essential.handle;
                message.size = 0;
                message.args[0] = buffer_handle; // TODO: to clarify
                message.args[1] = 0;
                return vkdrv_write(ilpriv->fd_dummy,&message,sizeof(message));
            }
    }
    return 0;
};

int32_t vkil_receive_buffer(const void *component_handle, void **buffer_handle, const vkil_command_t cmd)
{
    vkil_log(VK_LOG_DEBUG,"");
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
    vkil_log(VK_LOG_DEBUG,"");
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
    vkil_log(VK_LOG_DEBUG,"");
    if((vkil_api*) ilapi)
        free(ilapi);
    return 0;
}