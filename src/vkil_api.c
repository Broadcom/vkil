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

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include "vkil_api.h"
#include "vkil_backend.h"
#include "vkil_error.h"
#include "vkil_internal.h"
#include "vkil_session.h"
#include "vkil_utils.h"

/*
 * usually we wait for response message up to TIMEOUT us
 * However, in certain case, (essentially component initialization)
 * we could need to provide more time for the card to do the initialization
 * (at this time, all this is empiric)
 * TODO: implement a polling on ctrl queue (sideband like), to enquire the
 * status of the card rather than to rely on an arbitrary time out value
 */
#define TIMEOUT_MULTIPLE 100

/*
 * this refers to the maximum number of intransit message into a single context
 * with unique msg_id. The maximum size is (1<<12), since the msg has
 * a 12 bits field to encode the msg_id
 * there is 2 options to generate a unique msg_id, among intransit message:
 * 1/ use a large of digit (counter increment can then just be good enough...
 * it is not?)
 * 2/ use a reasonnably finite number of predefined id, and tag the intransit
 * message with it...this require the management of a list of id
 * we prefer the the second option, since output of the HW need to be anyway
 * paired with input (for the passing of ancillary filed, such as timestamp)
 * and so the mantenance of a list is a given
 */
#define MSG_LIST_SIZE 256

static int32_t vkil_return_msg_id(void *handle, const int32_t msg_id)
{
	vkil_context *ilctx = handle;
	vkil_context_internal *ilpriv;
	vkil_msg_id *msg_list;

	VK_ASSERT(handle);
	VK_ASSERT((msg_id >= 0) && (msg_id < MSG_LIST_SIZE));

	ilpriv = ilctx->priv_data;
	VK_ASSERT(ilpriv);

	msg_list = ilpriv->msg_list;
	VK_ASSERT(msg_list);

	VK_ASSERT(msg_list[msg_id].used);

	msg_list[msg_id].used = 0;
	return 0;
}

static int32_t vkil_get_msg_id(void *handle)
{
	int32_t ret, i;
	vkil_context *ilctx = handle;
	vkil_context_internal *ilpriv;
	vkil_msg_id *msg_list;

	VK_ASSERT(handle);

	ilpriv = ilctx->priv_data;
	VK_ASSERT(ilpriv);

	msg_list = ilpriv->msg_list;
	VK_ASSERT(msg_list);

	pthread_mutex_lock(&(ilpriv->mwx));
	for (i = 0; i < MSG_LIST_SIZE; i++) {
		if (!msg_list[i].used) {
			msg_list[i].used = 1;
			break;
		}
	}
	pthread_mutex_unlock(&(ilpriv->mwx));

	if (i >= MSG_LIST_SIZE)
		goto fail;

	return i;
fail:
	return (-ENOBUFS);
}

/**
 * prepopulate the command message with control field
 * argument, need to be prpeopulated in addition of it
 * and size changed accordingly
 *
 * @param[in,out] msg2vk to prepolulate
 * @param[in] handle to context
 * @param[in] function name to be transmitted into the messaage
 * @return    zero on succes, error code otherwise
 */
static int32_t preset_host2vk_msg(host2vk_msg *msg2vk, void *handle,
				  const char *functionname)
{
	vkil_context *ilctx = handle;
	int32_t ret;

	VK_ASSERT(handle);
	VK_ASSERT(msg2vk);

	ret = vkil_get_msg_id(handle);
	if (ret < 0)
		goto fail;

	msg2vk->msg_id = ret;
	msg2vk->queue_id = ilctx->context_essential.queue_id;
	msg2vk->context_id  = ilctx->context_essential.handle;
	msg2vk->function_id = vkil_get_function_id(functionname);
	msg2vk->size        = 0;
	return 0;

fail:
	return ret;
}

/**
 * deinitialzation to the vk card and
 * return on completion response from the card)
 *
 * @param handle    handle to a vkil_context
 * @return          zero on succes, error code otherwise
 */
static int32_t vkil_deinit_com(void *handle)
{
	int32_t ret, i;
	vkil_context *ilctx = handle;
	vkil_context_internal *ilpriv;
	host2vk_msg msg2vk;
	vk2host_msg msg2host;

	VK_ASSERT(handle);

	ilpriv = ilctx->priv_data;
	VK_ASSERT(ilpriv);

	if (ilctx->context_essential.handle < VK_START_VALID_HANDLE) {
		/* the call is allowed, but not necessarily expected */
		VKIL_LOG(VK_LOG_WARNING, "context %llx is not valid",
			 ilctx->context_essential.handle);
		return 0;
	}

	preset_host2vk_msg(&msg2vk, handle, "deinit");

	ret = vkil_write((void *)ilctx->devctx, &msg2vk);
	if (VKDRV_WR_ERR(ret, sizeof(msg2vk)))
		goto fail_write;

	memset(&msg2host, 0, sizeof(msg2host));
	/*
	 * in the deinit phase the card will need to flush some stuff we don't
	 * have visibility at vkil, but it is expected this take longer time
	 * than usual so we don't abort at the first timeout
	 */
	for (i = 0 ; i < TIMEOUT_MULTIPLE; i++) {
		ret = vkil_wait_probe_read((void *)ilctx->devctx, &msg2host);
		if (ret ==  (-EAGAIN))
			continue;
		else
			break;
	}
	if (VKDRV_RD_ERR(ret, sizeof(msg2host)))
		goto fail_read;

	vkil_return_msg_id(handle, msg2host.msg_id);

	VKIL_LOG(VK_LOG_DEBUG, "devctx = %llx, context_id=%llx",
		ilctx->devctx, ilctx->context_essential.handle);
	return 0;

fail_write:
	/* the queue could be full (ENOFUS), so not a real error */
	VKIL_LOG(VK_LOG_ERROR, "failure %d on writing message", ret);
	return ret;

fail_read:
	/*
	 * the response could take more time to return (ETIMEOUT),
	 * so not a real error
	 */
	VKIL_LOG(VK_LOG_ERROR, "failure %d on reading message ", ret);
	return ret;
}

/**
 * communicate initialzation to the vk card (return on repsonse from the card)
 *
 * @param handle    handle to a vkil_context
 * @return          zero on succes, error code otherwise
 */
static int32_t vkil_init_com(void *handle)
{
	int32_t ret, i;
	vkil_context *ilctx = handle;
	vkil_context_internal *ilpriv;
	host2vk_msg msg2vk;
	vk2host_msg msg2host;

	VK_ASSERT(handle);

	ilpriv = ilctx->priv_data;

	VK_ASSERT(ilpriv);

	preset_host2vk_msg(&msg2vk, handle, "init");
	if (msg2vk.context_id == VK_NEW_CTX) {
		/*
		 * the context is not yet existing on the device the arguments
		 * carries the context essential allowing the device to create
		 * it
		 */
		memcpy(msg2vk.args, &ilctx->context_essential,
			sizeof(vkil_context_essential));
	}

	ret = vkil_write((void *)ilctx->devctx, &msg2vk);
	if (VKDRV_WR_ERR(ret, sizeof(msg2vk)))
		goto fail_write;
	memset(&msg2host, 0, sizeof(msg2host));
	msg2host.msg_id = msg2vk.msg_id;
	/*
	 * in the init phase the card will instantiate some stuff we don't have
	 * visibility at vkil, but it is expected this take longer time than
	 * usual so we don't abort at the first timeout
	 */
	for (i = 0 ; i < TIMEOUT_MULTIPLE; i++) {
		ret = vkil_wait_probe_read((void *)ilctx->devctx, &msg2host);
		if (ret ==  (-EAGAIN))
			continue;
		else
			break;
	}
	if (VKDRV_RD_ERR(ret, sizeof(msg2host)))
		goto fail_read;

	vkil_return_msg_id(handle, msg2host.msg_id);
	if (msg2vk.context_id == VK_NEW_CTX)
		ilctx->context_essential.handle = msg2host.context_id;

	VKIL_LOG(VK_LOG_DEBUG, "card inited %d\n, with context_id=%llx",
			ilctx->devctx, ilctx->context_essential.handle);
	return 0;

fail_write:
	/* the queue could be full (ENOFUS), so not a real error */
	VKIL_LOG(VK_LOG_ERROR, "failure %d on writing message", ret);
	return ret;

fail_read:
	/*
	 * the response could take more time to return (ETIMEOUT),
	 * so not a real error
	 */
	VKIL_LOG(VK_LOG_ERROR, "failure %d on reading message ", ret);
	return ret;
}

/**
 * Initialize the device context (driver)
 *
 * @param handle    handle to a vkil_context
 * @return          zero on succes, error code otherwise
 */
static int32_t vkil_init_ctx(void *handle)
{
	int32_t ret, i;
	vkil_context *ilctx = handle;
	vkil_context_internal *ilpriv;

	/*
	 * we ensure we don't have a device context yet
	 * so we create one here
	 */
	VK_ASSERT(!ilctx->priv_data);

	ilctx->context_essential.handle = VK_NEW_CTX;

	/* we first get the session and card id*/

	ilctx->context_essential.session_id = vkil_get_session_id();
	if (ilctx->context_essential.session_id < 0)
		goto fail_session;

	/* the priv_data structure size could be component specific */
	ret = vk_mallocz(&ilctx->priv_data, sizeof(vkil_context_internal));
	if (ret)
		goto fail_malloc;

	/*
	 * we pair the device initialization with the private data one to
	 * prevent multiple device opening
	 */
	ret = vkil_init_dev(&ilctx->devctx);
	if (ret < 0)
		goto fail;

	ilctx->context_essential.card_id = ret;
	return 0;

fail:
	vk_free(&ilctx->priv_data);

fail_malloc:
	VKIL_LOG(VK_LOG_ERROR, "failed malloc\n");
	return VKILERROR(ret);

fail_session:
	return VKILERROR(ENOSPC);
}

/**
 * De-initializes a message list and associated component
 *
 * @param handle    handle to a vkil_context
 * @return          zero on succes, error code otherwise
 */
static int32_t vkil_deinit_msglist(void *handle)
{
	vkil_context *ilctx = handle;
	vkil_context_internal *ilpriv = ilctx->priv_data;
	int32_t ret = 0;

	vk_free((void **)&ilpriv->msg_list);
	ret |= pthread_mutex_destroy(&(ilpriv->mwx));

	if (ret)
		goto fail;

	return 0;

fail:
	VKIL_LOG(VK_LOG_ERROR, "failure");
	return VKILERROR(EPERM);
}

/**
 * Initializes a message list and associated component
 *
 * @param handle    handle to a vkil_context
 * @return          zero on succes, error code otherwise
 */
static int32_t vkil_init_msglist(void *handle)
{
	vkil_context *ilctx = handle;
	vkil_context_internal *ilpriv = ilctx->priv_data;
	int32_t ret;

	ret = vk_mallocz((void **)&ilpriv->msg_list,
					 sizeof(vkil_msg_id)*MSG_LIST_SIZE);
	if (ret)
		goto fail;

	pthread_mutex_init(&(ilpriv->mwx), NULL);
	return 0;

fail:
	VKIL_LOG(VK_LOG_ERROR, "failure on %x", ret);
	return ret;
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
	int32_t ret = 0;

	VKIL_LOG(VK_LOG_DEBUG, "");

	if (!ilctx) {
		VKIL_LOG(VK_LOG_ERROR, "unexpected call\n");
		return 0;
	}
	if (ilctx->priv_data) {
		vkil_context_internal *ilpriv;

		ret |= vkil_deinit_com(*handle);
		ilpriv = ilctx->priv_data;
		if (ilctx->devctx)
			vkil_deinit_dev(&ilctx->devctx);
		if (ilpriv->msg_list)
			ret |= vkil_deinit_msglist(ilctx);
		vk_free((void **)&ilpriv);
	}
	vk_free(handle);

	return ret;
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

		if (!ilctx->priv_data) {
			ret = vkil_init_ctx(*handle);
			if (ret)
				goto fail;
		}
		if (!((vkil_context_internal *)(ilctx->priv_data))->msg_list) {
			ret = vkil_init_msglist(ilctx);
			if (ret)
				goto fail;
		}
		ret = vkil_init_com(*handle);
		if (ret)
			goto fail;
	}
	return 0;

fail_malloc:
	VKIL_LOG(VK_LOG_ERROR, "failed malloc");
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
	if (field == VK_PARAM_PORT)
		return sizeof(vk_port);
	else if (field == VK_PARAM_VIDEO_ENC_CONFIG)
		return sizeof(vk_enc_cfg);
	/* this is the default value when not structure is defined */
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
int32_t vkil_set_parameter(void *handle,
			   const vkil_parameter_t field,
			   const void *value,
			   const vkil_command_t cmd)
{
	const vkil_context *ilctx = handle;
	int32_t ret;
	vkil_context_internal *ilpriv;
	int32_t field_size = vkil_get_struct_size(field);
	/* message size is expressed in 16 bytes unit */
	int32_t msg_size = field_size == sizeof(uint32_t) ?
					0 : MSG_SIZE(field_size);
	host2vk_msg message[msg_size + 1];

	VKIL_LOG(VK_LOG_DEBUG, "");
	VK_ASSERT(handle); /* sanity check */

	/* TODO: non blocking option not yet implemented */
	VK_ASSERT(cmd & VK_CMD_BLOCKING);

	ilpriv = ilctx->priv_data;
	VK_ASSERT(ilpriv);

	preset_host2vk_msg(message, handle, "set_parameter");
	/* complete message setting */
	message->size        = msg_size;
	message->args[0]     = field;

	/* align  structure copy on 16 bytes boundary */
	memcpy(&message->args[msg_size ? 2 : 1], value, field_size);
	ret = vkil_write((void *)ilctx->devctx, message);
	if (VKDRV_WR_ERR(ret, sizeof(message)))
		goto fail_write;

	if (cmd & VK_CMD_BLOCKING) {
		/* we wait for the the card response */
		vk2host_msg response;

		response.msg_id      = message->msg_id;
		response.queue_id    = ilctx->context_essential.queue_id;
		response.context_id  = ilctx->context_essential.handle;
		response.size        = 0;
		ret = vkil_wait_probe_read((void *)ilctx->devctx, &response);
		if (VKDRV_RD_ERR(ret, sizeof(response)))
			goto fail_read;

		vkil_return_msg_id(handle, response.msg_id);
	}
	return 0;

fail_write:
	/* the queue could be full (ENOFUS), so not a real error */
	VKIL_LOG(VK_LOG_ERROR, "failure %d on writing message", ret);
	return ret;

fail_read:
	/*
	 * the response could take more time to return (ETIMEOUT),
	 * so not a real error
	 */
	VKIL_LOG(VK_LOG_WARNING, "failure %d on reading message ", ret);
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
int32_t vkil_get_parameter(void *handle,
			   const vkil_parameter_t field,
			   void *value,
			   const vkil_command_t cmd)
{
	int32_t ret;
	const vkil_context *ilctx = handle;
	int32_t field_size = vkil_get_struct_size(field);
	/* message size is expressed in 16 bytes unit */
	int32_t msg_size = field_size == sizeof(uint32_t) ?
					0 : MSG_SIZE(field_size);
	host2vk_msg  message[msg_size + 1];

	VKIL_LOG(VK_LOG_DEBUG, "");
	VK_ASSERT(handle); /* sanity check */

	/* TODO: non blocking option not yet implemented */
	VK_ASSERT(cmd & VK_CMD_BLOCKING);

	preset_host2vk_msg(message, handle, "get_parameter");
	/* complete setting */
	message->size        = msg_size;
	message->args[0]       = field;
	memcpy(&message->args[msg_size ? 2 : 1], value, field_size);

	ret = vkil_write((void *)ilctx->devctx, message);
	if (VKDRV_WR_ERR(ret, sizeof(message)))
		goto fail_write;

	if (cmd & VK_CMD_BLOCKING) {
		/* we wait for the the card response */
		vk2host_msg response[msg_size + 1];

		response->msg_id      = message->msg_id;
		response->queue_id    = ilctx->context_essential.queue_id;
		response->context_id  = ilctx->context_essential.handle;
		response->size        = msg_size;
		ret = vkil_wait_probe_read((void *)ilctx->devctx, response);
		if (VKDRV_RD_ERR(ret, sizeof(response)))
			goto fail_read;

		vkil_return_msg_id(handle, response->msg_id);
		memcpy(value,
			&((&(response->arg))[msg_size ? 1 : 0]), field_size);
	}
	return 0;

fail_write:
	/* the queue could be full (ENOFUS), so not a real error */
	VKIL_LOG(VK_LOG_ERROR, "failure %d on writing message", ret);
	return ret;

fail_read:
	/*
	 * the response could take more time to return (ETIMEOUT),
	 * so not necessarily a real error
	 */
	VKIL_LOG(VK_LOG_WARNING, "failure %d on reading message ", ret);
	return ret;
};


/**
 * convert a front end surface structure into a backend one
 * (they can be different or the same).
 * @param[out] surface    handle to the backend structure
 * @param[in]  il_surface handle to a front hand packet structure
 * @return          zero on succes, error code otherwise
 */
static int32_t convert_vkil2vk_buffer_surface(vk_buffer_surface *surface,
					const vkil_buffer_surface *il_surface)
{
	/*
	 * here we assume we are on a 64 bit architecture
	 * the below is expected to work on 32 bits arch too but has not been
	 * tested
	 */
	VK_ASSERT(sizeof(void *) == sizeof(uint64_t));

	surface->handle	       = il_surface->handle;
	surface->user_data_tag = il_surface->user_data_tag;
	surface->flags = il_surface->flags;
	surface->plane_top[0] = (uint64_t)il_surface->plane_top[0];
	surface->plane_top[1] = (uint64_t)il_surface->plane_top[1];
	surface->plane_bot[0] = (uint64_t)il_surface->plane_bot[0];
	surface->plane_bot[1] = (uint64_t)il_surface->plane_bot[1];
	surface->stride[0]    = il_surface->stride[0];
	surface->stride[1]    = il_surface->stride[1];
	surface->max_frame_width  = il_surface->max_frame_width;
	surface->max_frame_height = il_surface->max_frame_height;
	surface->format           = il_surface->format;
	return 0;
}

/**
 * convert a front end packet structure into a backend one
 * (they can be different or the same).
 * @param[out] packet    handle to the backend structur
 * @param[in]  il_packet    handle to a front hand packet structuree
 * @return          zero on succes, error code otherwise
 */
static int32_t convert_vkil2vk_buffer_packet(vk_buffer_packet *packet,
					const vkil_buffer_packet *il_packet)
{
	/*
	 * here we assume we are on a 64 bit architecture
	 * the below is expected to work on 32 bits arch too but has not been
	 * tested
	 */
	VK_ASSERT(sizeof(void *) == sizeof(uint64_t));

	packet->handle	      = il_packet->handle;
	packet->user_data_tag = il_packet->user_data_tag;
	packet->flags	      = il_packet->flags;
	packet->size	      = il_packet->size;
	packet->data	      = (uint64_t)il_packet->data;
	return 0;
}

/**
 * convert a front end buffer structure into a backend one
 * (they can be different or the same).
 * @param[in]  il_packet    handle to a front hand packet structure
 * @param[out] packet    handle to the backend structure
 * @return          zero on succes, error code otherwise
 */
static int32_t convert_vkil2vk_buffer(void *buffer, const void *il_buffer)
{
	const vkil_buffer *vk_buffer = il_buffer;

	VK_ASSERT(il_buffer);
	VK_ASSERT(buffer);

	switch (vk_buffer->type) {
	case	VK_BUF_PACKET:
		return convert_vkil2vk_buffer_packet(buffer, il_buffer);
	case	VK_BUF_SURFACE:
		return convert_vkil2vk_buffer_surface(buffer, il_buffer);
	}

	return  -(EINVAL);
}

/**
 * get the size of the vk backend paired structure
 * (they can be different or the same).
 * @param[in]  il_packet    handle to a front hand packet structure
 * @param[out] packet    handle to the backend structure
 * @return          szie of the backend structure
 */
static int32_t get_vkil2vk_buffer_size(const void *il_buffer)
{
	const vkil_buffer *buffer = il_buffer;

	VK_ASSERT(il_buffer);

	switch (buffer->type) {
	case	VK_BUF_PACKET:  return sizeof(vk_buffer_packet);
	case	VK_BUF_SURFACE: return sizeof(vk_buffer_surface);
	}
	return -(EINVAL);
}

/**
 * Uploads buffers
 *
 * @param component_handle    handle to a vkil_context
 * @param host_buffer         buffer to upload
 * @param cmd                 vkil command of the dma operation
 * @return                    zero on success, error code otherwise
 */
static int32_t vkil_mem_transfer_buffer(void *component_handle,
					void *host_buffer,
					const vkil_command_t cmd)
{
	int32_t ret;
	const vkil_context *ilctx = component_handle;
	const vkil_command_t load_mode = cmd & VK_CMD_MAX;
	int32_t size = get_vkil2vk_buffer_size(host_buffer);
	vkil_context_internal *ilpriv;
	int32_t msg_size = MSG_SIZE(size);
	host2vk_msg message[msg_size + 1];

	VK_ASSERT(component_handle);
	VK_ASSERT(cmd);

	ilpriv = ilctx->priv_data;
	VK_ASSERT(ilpriv);

	/* We need to write the dma command */

	/* we could need to transfer a SGL list */
	/*
	 * at this time we assume a coherent memory addressing, so a single
	 * pointer is sufficient, in case of a SGL need to be transferred
	 * it can be done in 2 way, a pointer to a SGL structure
	 */
	preset_host2vk_msg(message, component_handle, "transfer_buffer");
	/* complete setting */
	message->size        = msg_size;
	message->args[0]     = load_mode;

	/* we convert the il frontend structure into a backend one */
	convert_vkil2vk_buffer(&message->args[2], host_buffer);

	/* then we write the command to the queue */
	ret = vkil_write((void *)ilctx->devctx, message);
	if (VKDRV_WR_ERR(ret, sizeof(host2vk_msg)*(msg_size + 1)))
		goto fail_write;

	return 0;

fail_write:
	/*
	 * the input queue could be full (ENOBUFS),
	 * so not necessarily always an real error
	 */
	VKIL_LOG(VK_LOG_ERROR, "failure %d on writing message ", ret);
	return ret;

};

/**
 * transfer buffer
 *
 * @param component_handle    handle to a vkil_context
 * @param buffer_handle       buffer to send
 * @param cmd                 vkil command of the dma operation
 * @return                    number of buffers sent on success, error code
 *                            otherwise
 */
int32_t vkil_transfer_buffer(void *component_handle,
			 void *buffer_handle,
			 const vkil_command_t cmd)
{
	const vkil_context *ilctx = component_handle;
	vkil_context_internal *ilpriv;
	vkil_buffer *buffer;
	int32_t ret;


	VKIL_LOG(VK_LOG_DEBUG, "");

	VK_ASSERT(component_handle);
	VK_ASSERT(buffer_handle);

	ilpriv = ilctx->priv_data;
	buffer = buffer_handle;

	VK_ASSERT(ilpriv);

	if (!(cmd & VK_CMD_CB)) {
		host2vk_msg message;

		switch (cmd & VK_CMD_MAX) {
		case VK_CMD_UPLOAD:
		case VK_CMD_DOWNLOAD:
			ret = vkil_mem_transfer_buffer(component_handle,
					   buffer_handle, cmd);
			break;
		default:
			/* tunnelled operations */
			preset_host2vk_msg(&message, component_handle,
					   "transfer_buffer");
			/* complete message setting */
			message.args[0]     = (cmd & VK_CMD_MAX);
			message.args[1]     = buffer->handle;

			ret = vkil_write((void *)ilctx->devctx, &message);
			if (VKDRV_WR_ERR(ret, sizeof(message)))
				goto fail_write;
		}
	}
	if ((cmd & VK_CMD_BLOCKING) || (cmd & VK_CMD_CB)) {
		/* we check for the the card response */
		vk2host_msg response;

		// response.msg_id
		response.queue_id    = ilctx->context_essential.queue_id;
		response.context_id  = ilctx->context_essential.handle;
		response.size        = 0;
		ret = vkil_wait_probe_read((void *)ilctx->devctx, &response);
		if (VKDRV_RD_ERR(ret, sizeof(response)))
			goto fail_read;

		buffer->handle = response.arg;
		vkil_return_msg_id(component_handle, response.msg_id);
	}
	return ret;

fail_write:
	/*
	 * the input queue could be full (ENOBUFS),
	 * so not necessarily always a real error
	 */
	if (ret == (-ENOBUFS))
		/*
		 * This is probably caused by a backlog on the return queue.
		 * The host should thain drain this queue first before
		 * retrying to write on the input queue
		 */
		return ret; /* request the host to drain the queue first */

	VKIL_LOG(VK_LOG_ERROR, "failure %d on writing message ", ret);
	return ret;

fail_read:
	/*
	 * the response could take more time to return (ETIMEOUT),
	 * so not necessarily always a real error
	 */
	if ((ret == (-ENOMSG)) || (ret == (-EAGAIN)))
		return (-EAGAIN); /* request the host to try again */

	VKIL_LOG(VK_LOG_WARNING, "failure %d on reading message ", ret);
	return ret;
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
		.transfer_buffer       = vkil_transfer_buffer,
		.send_buffer           = vkil_transfer_buffer,
		.receive_buffer        = vkil_transfer_buffer,
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
