/*
 * Copyright 2018 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation (the "GPL").
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 (GPLv2) for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 (GPLv2) along with this source code.
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include "vkdrv_access.h"
#include "vkil_api.h"
#include "vkil_backend.h"
#include "vkil_error.h"
#include "vkil_session.h"
#include "vkil_utils.h"

/**
 * This structure is copied thru PCIE bridge
 * Currently, it is limited to 16 bytes
 */
typedef struct _vkil_context_internal {
	int fd;
} vkil_context_internal;


static int32_t vkil_init1_ctx(void *handle)
{
	int32_t ret, i;
	vkil_context *ilctx = handle;
	vkil_context_internal *ilpriv;
	host2vk_msg msg2vk;
	vk2host_msg msg2host;
	char dev_name[30]; /* format: /dev/bcm-vk.x */

	VK_ASSERT(!ilctx->priv_data);
	/*
	 * we know the context, but this one has not been created yet on the
	 * vk card,
	 */
	ilctx->context_essential.handle = VK_NEW_CTX;

	/* we first get the session and card id*/

	ilctx->context_essential.session_id = vkil_get_session_id();
	if (ilctx->context_essential.session_id < 0)
		goto fail_session;

	ilctx->context_essential.card_id = vkil_get_card_id();
	if (ilctx->context_essential.card_id < 0)
		goto fail_session;

	if (!snprintf(dev_name, sizeof(dev_name), "/dev/bcm-vk.%d",
			ilctx->context_essential.card_id))
		goto fail_session;

	/* the priv_data structure size could be component specific */
	ret = vk_mallocz(&ilctx->priv_data, sizeof(vkil_context_internal));
	if (ret)
		goto fail_malloc;

	ilpriv = ilctx->priv_data;
	ilpriv->fd = vkdrv_open(dev_name, O_RDWR);

	VKIL_LOG(VK_LOG_DEBUG, "ilpriv->fd: %i\n", ilpriv->fd);

	msg2vk.queue_id = ilctx->context_essential.queue_id;
	msg2vk.function_id = vkil_get_function_id("init");
	msg2vk.size        = 0;
	msg2vk.context_id  = ilctx->context_essential.handle;
	if (msg2vk.context_id == VK_NEW_CTX) {
		/*
		 * the context is not yet existing on the device the arguments
		 * carries the context essential allowing the device to create
		 * it
		 */
		memcpy(msg2vk.args, &ilctx->context_essential,
			sizeof(vkil_context_essential));
	}

	vkdrv_write(ilpriv->fd, &msg2vk, sizeof(msg2vk));

	memset(&msg2host, 0, sizeof(msg2host));

	/*
	 * in the init phase the card will instantiate some stuff we don't have
	 * visibility at vkil, but it is expected this take longer time than
	 * usual so we don't abort at the first timeout
	 */
	for (i = 0 ; i < 4; i++) {
		ret = vkil_wait_probe_msg(vkdrv_read, ilpriv->fd, &msg2host,
						sizeof(msg2host));
		if (ret ==  (-ETIMEDOUT))
			continue;
		else
			break;
	}
	if (ret < 0)
		goto fail_message_queue;

	ilctx->context_essential.handle = msg2host.context_id;

	VKIL_LOG(VK_LOG_DEBUG, "card inited %d\n, with context_id=%llx",
			ilpriv->fd, ilctx->context_essential.handle);
	return 0;

fail_message_queue:
	VKIL_LOG(VK_LOG_ERROR, "failed message queue %d\n", ret);
	return VKILERROR(ret);

fail_malloc:
	VKIL_LOG(VK_LOG_ERROR, "failed malloc\n");
	return VKILERROR(ret);

fail_session:
	return VKILERROR(ENOSPC);
}

/**
 * De-initializes a vkil_context, and its associated h/w contents
 *
 * @param handle    handle to a vkil_context
 * @return          zero on succes, error code otherwise
 */
int32_t vkil_deinit(void **handle)
{
	vkil_context *ilctx = *handle;

	VKIL_LOG(VK_LOG_DEBUG, "");

	if (!ilctx) {
		VKIL_LOG(VK_LOG_ERROR, "unexpected call\n");
		return 0;
	}

	if (ilctx->priv_data) {
		vkil_context_internal *ilpriv;

		ilpriv = ilctx->priv_data;
		if (ilpriv->fd)
			vkdrv_close(ilpriv->fd);
		vk_free((void **)&ilpriv);
	}
	vk_free(handle);

	return 0;
};

/**
 * Initialize a vkil_context
 * The first pass creates the vkil_context, done in s/w
 * The second pass invokes the h/w
 *
 * @param handle    reference of a handle to a vkil_context
 * @return          zero on success, error codes otherwise
 */
int32_t vkil_init(void **handle)
{
	int32_t ret;

	VKIL_LOG(VK_LOG_DEBUG, "");

	if (*handle == NULL) {
		ret = vk_mallocz(handle, sizeof(vkil_context));
		if (ret)
			goto fail_malloc;
		/* no information on the component, just return the handle */
	} else {
		vkil_context *ilctx = *handle;

		if (!ilctx->priv_data)
			ret = vkil_init1_ctx(*handle);
			if (ret)
				goto fail;
	}
	return 0;

fail_malloc:
	VKIL_LOG(VK_LOG_ERROR, "failed malloc\n");
	return VKILERROR(ret);

fail:
	vkil_deinit(handle);
	return VKILERROR(ret);
};

/**
 * return the size in bytes of the structure associated to the field
 *
 * @param field
 * @return          size of the structure aasociated to field
 */
static int32_t vkil_get_struct_size(const vkil_parameter_t field)
{
	/* that is the default value*/
	return sizeof(int32_t);
}

/**
 * Sets a parameter of a vkil_context
 *
 * @param handle    handle to a vkil_context
 * @param field     field to set
 * @param value     value to set the field to
 * @param cmd       some cmd file
 * @return          zero on success, error code otherwise
 */
int32_t vkil_set_parameter(const void *handle,
			   const vkil_parameter_t field,
			   const void *value,
			   const vkil_command_t cmd)
{
	const vkil_context *ilctx = handle;
	int32_t ret;
	vkil_context_internal *ilpriv;
	int32_t field_size = vkil_get_struct_size(field);
	/* message size is expressed in 16 bytes unit */
	int32_t msg_size = ((field_size + 15)/16) - 1;
	host2vk_msg message[msg_size + 1];

	VKIL_LOG(VK_LOG_DEBUG, "");
	VK_ASSERT(handle); /* sanity check */

	/* TODO: non blocking option not yet implemented */
	VK_ASSERT(cmd & VK_CMD_BLOCKING);

	ilpriv = ilctx->priv_data;
	VK_ASSERT(ilpriv);

	message->queue_id    = ilctx->context_essential.queue_id;
	message->function_id = vkil_get_function_id("set_parameter");
	message->context_id  = ilctx->context_essential.handle;
	message->size        = msg_size;
	message->args[0]     = field;
	memcpy(&message->args[1], value, field_size);

	VKIL_LOG(VK_LOG_DEBUG, "message->context_id %llx", message->context_id);

	ret = vkdrv_write(ilpriv->fd, &message, sizeof(message));
	if (ret < 0)
		goto fail_write;

	if (cmd & VK_CMD_BLOCKING) {
		/* we wait for the the card response */
		vk2host_msg response;

		response.queue_id    = ilctx->context_essential.queue_id;
		response.context_id  = ilctx->context_essential.handle;
		response.size        = 0;
		ret = vkil_wait_probe_msg(vkdrv_read, ilpriv->fd, &response,
						sizeof(response));
		if (ret < 0)
			goto fail_read;
	}
	return 0;

fail_write:
	/* the queue could be full (ENOFUS), so not a real error */
	VKIL_LOG(VK_LOG_DEBUG, "failure %d on writing message", ret);
	return ret;

fail_read:
	/*
	 * the response could take more time to return (ETIMEOUT),
	 * so not a real error
	 */
	VKIL_LOG(VK_LOG_DEBUG, "failure %d on reading message ", ret);
	return ret;
};

/**
 * Gets a parameter value of a vkil_context
 *
 * @param handle    handle to a vkil_context
 * @param field     field to get
 * @param value     field value retrieved
 * @return          zero on success, error code otherwise
 */
int32_t vkil_get_parameter(const void *handle,
			   const vkil_parameter_t field,
			   void *value,
			   const vkil_command_t cmd)
{
	int32_t ret;
	const vkil_context *ilctx = handle;
	vkil_context_internal *ilpriv;
	int32_t field_size = vkil_get_struct_size(field);
	/* message size is expressed in 16 bytes unit */
	int32_t msg_size = ((field_size + 15)/16) - 1;
	host2vk_msg  message;

	VKIL_LOG(VK_LOG_DEBUG, "");
	VK_ASSERT(handle); /* sanity check */

	/* TODO: non blocking option not yet implemented */
	VK_ASSERT(cmd & VK_CMD_BLOCKING);

	ilpriv = ilctx->priv_data;
	VK_ASSERT(ilpriv);

	message.queue_id    = ilctx->context_essential.queue_id;
	message.function_id = vkil_get_function_id("get_parameter");
	message.context_id  = ilctx->context_essential.handle;
	message.size        = 0;
	message.args[0]       = field;

	ret = vkdrv_write(ilpriv->fd, &message, sizeof(message));
	if (ret < 0)
		goto fail_write;

	if (cmd & VK_CMD_BLOCKING) {
		/* we wait for the the card response */
		vk2host_msg response[msg_size + 1];

		response->queue_id    = ilctx->context_essential.queue_id;
		response->context_id  = ilctx->context_essential.handle;
		response->size        = 0;
		ret = vkil_wait_probe_msg(vkdrv_read, ilpriv->fd, &response,
						sizeof(response));
		if (ret < 0)
			goto fail_read;
		memcpy(value, &(response->arg), field_size);
	}
	return 0;

fail_write:
	/* the queue could be full (ENOFUS), so not a real error */
	VKIL_LOG(VK_LOG_DEBUG, "failure %d on writing message", ret);
	return ret;

fail_read:
	/*
	 * the response could take more time to return (ETIMEOUT),
	 * so not necessarily a real error
	 */
	VKIL_LOG(VK_LOG_DEBUG, "failure %d on reading message ", ret);
	return ret;
};

/**
 * Uploads buffers
 *
 * @param component_handle    handle to a vkil_context
 * @param host_buffer         buffer to upload
 * @param cmd                 vkil command of the dma operation
 * @return                    zero on success, error code otherwise
 */
int32_t vkil_upload_buffer(const void *component_handle,
			   const void *host_buffer,
			   const vkil_command_t cmd)
{
	const vkil_context *ilctx = component_handle;
	vkil_context_internal *ilpriv;
	host2vk_msg message;

	VKIL_LOG(VK_LOG_DEBUG, "");

	VK_ASSERT(component_handle);
	VK_ASSERT(host_buffer);
	VK_ASSERT(cmd);

	ilpriv = ilctx->priv_data;
	VK_ASSERT(ilpriv);

	/* here we need to write the dma command */
	message.queue_id    = ilctx->context_essential.queue_id;
	message.function_id = vkil_get_function_id("send_buffer");
	message.context_id  = ilctx->context_essential.handle;
	message.size        = 0;
	message.args[0]     = host_buffer; /* TODO: to clarify */
	message.args[1]     = VK_CMD_UPLOAD;
	/* return vkdrv_write(ilpriv->fd_dummy,&message,sizeof(message)); */
	return 0;
};

/**
 * Downloads buffers
 *
 * @param component_handle    handle to a vkil_context
 * @param host_buffer         buffer to download
 * @param cmd                 vkil command of the dma operation
 * @return                    zero on success, error code otherwise
 */
int32_t vkil_download_buffer(const void *component_handle,
			     void **host_buffer,
			     const vkil_command_t cmd)
{
	VKIL_LOG(VK_LOG_DEBUG, "");

	VK_ASSERT(component_handle);
	VK_ASSERT(host_buffer);
	VK_ASSERT(cmd);

	return 0;
};

/**
 * Polls the status of a buffer upload
 *
 * @param component_handle    handle to a vkil_context
 * @param host_buffer         buffer to poll on
 * @param cmd                 vkil command of the dma operation
 * @return                    the polled status on success, error code otherwise
 */
int32_t vkil_uploaded_buffer(const void *component_handle,
			     const void *host_buffer,
			     const vkil_command_t cmd)
{
	VKIL_LOG(VK_LOG_DEBUG, "");

	VK_ASSERT(component_handle);
	VK_ASSERT(host_buffer);
	VK_ASSERT(cmd);

	return 0;
};

/**
 * Polls the status of a buffer download
 *
 * @param component_handle    handle to a vkil_context
 * @param host_buffer         buffer to poll on
 * @param cmd                 vkil command of the dma operation
 * @return                    the polled status on success, error code otherwise
 */
int32_t vkil_downloaded_buffer(const void *component_handle,
			       const void *host_buffer,
			       const vkil_command_t cmd)
{
	VKIL_LOG(VK_LOG_DEBUG, "");

	VK_ASSERT(component_handle);
	VK_ASSERT(host_buffer);
	VK_ASSERT(cmd);

	return 0;
};

/**
 * Sends buffers
 *
 * @param component_handle    handle to a vkil_context
 * @param buffer_handle       buffer to send
 * @param cmd                 vkil command of the dma operation
 * @return                    number of buffers sent on success, error code
 *                            otherwise
 */
int32_t vkil_send_buffer(const void *component_handle,
			 const void *buffer_handle,
			 const vkil_command_t cmd)
{
	VKIL_LOG(VK_LOG_DEBUG, "");

	VK_ASSERT(component_handle);
	VK_ASSERT(buffer_handle);

	switch (cmd & VK_CMD_MAX) {
	/* untunnelled operations */
	case VK_CMD_UPLOAD:
		vkil_upload_buffer(component_handle, buffer_handle, cmd);

		if (cmd & VK_CMD_BLOCKING) {
			/* wait the buffer to be uploaded */
			return vkil_uploaded_buffer(component_handle,
						    buffer_handle,
						    VK_CMD_BLOCKING);
		}
		break;
	default: {
		/* tunnelled operations */
		const vkil_context *ilctx = component_handle;
		vkil_context_internal *ilpriv = ilctx->priv_data;
		host2vk_msg message;

		VK_ASSERT(ilpriv);

		message.queue_id    = ilctx->context_essential.queue_id;
		message.function_id = vkil_get_function_id("send_buffer");
		message.context_id  = ilctx->context_essential.handle;
		message.size        = 0;
		message.args[0]     = buffer_handle; /* TODO: to clarify */
		message.args[1]     = 0;

		return vkdrv_write(ilpriv->fd, &message, sizeof(message));
	}
	}
	return 0;
};

/**
 * Receives buffers
 *
 * @param component_handle    handle to a vkil_context
 * @param buffer_handle       buffer to receive
 * @param cmd                 vkil command of the dma operation
 * @return                    number of buffers received on success, error code
 *                            otherwise
 */
int32_t vkil_receive_buffer(const void *component_handle,
			    void **buffer_handle,
			    const vkil_command_t cmd)
{
	VKIL_LOG(VK_LOG_DEBUG, "");

	VK_ASSERT(component_handle);
	VK_ASSERT(buffer_handle);

	switch (cmd) {
	case VK_CMD_DOWNLOAD:
		/* untunnelled operations*/
		vkil_download_buffer(component_handle, buffer_handle, cmd);

		/* need to wait the buffer has effectively been uploaded */
		return vkil_uploaded_buffer(component_handle,
					    buffer_handle,
					    VK_CMD_BLOCKING);
	default:
		/* tunnelled operations*/
		break;
	}

	return 0;
};

/**
 * Creates and initialize a vkil_api
 *
 * @return an inited vkil_api on success, NULL otherwise
 */
void *vkil_create_api(void)
{
	vkil_api *ilapi;

	VKIL_LOG(VK_LOG_DEBUG, "");

	if (vk_mallocz((void **)&ilapi, sizeof(vkil_api)))
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

/**
 * Destroys a vkil_api
 *
 * @param ilapi    handle to a vkil_api
 * @return         zero on success, error codes otherwise
 */
int vkil_destroy_api(void **ilapi)
{
	VKIL_LOG(VK_LOG_DEBUG, "");

	vk_free(ilapi);

	return 0;
}
