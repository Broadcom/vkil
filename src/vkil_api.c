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
	void *fd_dummy;
} vkil_context_internal;

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
	int ret;

	VKIL_LOG(VK_LOG_DEBUG, "");

	if (*handle == NULL) {
		ret = vk_mallocz(handle, sizeof(vkil_context));
		if (ret)
			goto fail_malloc;
		/* no information on the component, just return the handle */
	} else {
		vkil_context *ilctx = *handle;
		/*
		 * this is defined by the vk card
		 * (expected to be the address on the valkyrie card)
		 */
		ilctx->context_essential.handle = 0;

		ilctx->context_essential.session_id = vkil_get_session_id();
		if (ilctx->context_essential.session_id < 0)
			goto fail_session;

		ilctx->context_essential.card_id = vkil_get_card_id();
		if (ilctx->context_essential.card_id < 0)
			goto fail_session;

		VKIL_LOG(VK_LOG_DEBUG, "session_id: %i\n",
			 ilctx->context_essential.session_id);
		VKIL_LOG(VK_LOG_DEBUG, "card_id: %i\n",
			 ilctx->context_essential.card_id);

		if (!ilctx->priv_data) {
			vkil_context_internal *ilpriv;
			host2vk_msg message;

			/*
			 * we know the component, but this one has not been
			 * created yet, the priv_data structure size could be
			 * component specific
			 */
			ret = vk_mallocz(&ilctx->priv_data,
					 sizeof(vkil_context_internal));
			if (ret)
				goto fail_malloc;

			ilpriv = ilctx->priv_data;
			ilpriv->fd_dummy = vkdrv_open();

			VKIL_LOG(VK_LOG_DEBUG, "ilpriv->fd_dummy: %i\n",
				 ilpriv->fd_dummy);

			message.queue_id    = ilctx->context_essential.queue_id;
			message.function_id = vkil_get_function_id("init");
			message.size        = 0;
			message.context_id  = ilctx->context_essential.handle;
			if (message.context_id == VK_NEW_CTX) {
				/*
				 * the context is not yet existing on the device
				 * the arguments carries the context essential
				 * allowing the device to create it
				 */
				memcpy(message.args, &ilctx->context_essential,
				       sizeof(vkil_context_essential));
			}

			VKIL_LOG(VK_LOG_DEBUG, "&message: %x\n",
				 &message);
			VKIL_LOG(VK_LOG_DEBUG, "message.function_id: %x\n",
				 message.function_id);
			VKIL_LOG(VK_LOG_DEBUG, "message.size: %d\n",
				 message.size);
			VKIL_LOG(VK_LOG_DEBUG, "message.context_id: %x\n",
				 message.context_id);

			vkdrv_write(ilpriv->fd_dummy, &message,
				    sizeof(message));

			/*
			 * Here, we will need to wait (poll_wait()) for the HW
			 * to create the component.
			 * vkdrv_read() to get the status of the hw
			 */

			VKIL_LOG(VK_LOG_DEBUG, "card inited %x\n",
				 ilpriv->fd_dummy);
		}
	}
	return 0;

fail_malloc:
	VKIL_LOG(VK_LOG_ERROR, "failed malloc\n");
	return VKILERROR(ret);

fail_session:
	return VKILERROR(ENOSPC);
};

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

	VK_ASSERT(handle);

	if (ilctx->priv_data) {
		vkil_context_internal *ilpriv;

		ilpriv = ilctx->priv_data;
		if (ilpriv->fd_dummy)
			vkdrv_close(ilpriv->fd_dummy);
		vk_free((void **)&ilpriv);
	}
	vk_free(handle);

	return 0;
};

/**
 * Sets a parameter of a vkil_context
 *
 * @param handle    handle to a vkil_context
 * @param field     field to set
 * @param value     value to set the field to
 * @return          zero on success, error code otherwise
 */
int32_t vkil_set_parameter(const void *handle,
			   const int32_t field,
			   const void *value)
{
	VKIL_LOG(VK_LOG_DEBUG, "");

	VK_ASSERT(handle);
	VK_ASSERT(field);
	VK_ASSERT(value);

	return 0;
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
			   const int32_t field,
			   void **value)
{
	VKIL_LOG(VK_LOG_DEBUG, "");

	VK_ASSERT(handle);
	VK_ASSERT(field);
	VK_ASSERT(value);

	return 0;
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

		return vkdrv_write(ilpriv->fd_dummy, &message, sizeof(message));
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
