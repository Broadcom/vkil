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
#include <string.h>
#include "vk_error.h"
#include "vkil_api.h"
#include "vkil_backend.h"
#include "vkil_internal.h"
#include "vkil_session.h"
#include "vkil_utils.h"

/*
 * store global configurations that are from CLI parser
 */
static struct _vkil_cfg {
	const char *vkapi_device; /* device/affinity, which card to be used */
} vkil_cfg;

/*
 * usually we wait for response message up to TIMEOUT us
 * However, in certain case, (essentially component initialization)
 * we could need to provide more time for the card to do the initialization
 * (at this time, all this is empiric)
 * TODO: implement a polling on ctrl queue (sideband like), to enquire the
 * status of the card rather than to rely on an arbitrary time out value
 */

/** this wait factor tells vkil_read to wait "normal time" for message */
#define WAIT 1
/** this wait factor tells vkil_read to wait "extra time" for message */
#define WAIT_INIT (WAIT * 10)

/** max expected return message size, can be locally overidden */
#define VKIL_RET_MSG_MAX_SIZE 8

#define VKILERROR(type) VKERROR_MAKE(VKIL, 0, vkil_error(__func__), type)

/**
 * Generate an index number from a function name
 * To be used to build an error code
 *
 * @param functionname function name
 * @return             function index
 */
static int32_t vkil_error(const char *functionname)
{
	static const char * const vkil_fun_list[] = {
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
		"vkil_downloaded_buffer"};
	int32_t i;

	for (i = 1; i < VK_ARRAY_SIZE(vkil_fun_list); i++) {
		if (!strcmp(vkil_fun_list[i], functionname))
			return i;
	}
	return 0;
};

/* macros for handling error condition */
#define VKDRV_WR_ERR(_ret, _size)     ((_ret < 0) || (_ret != _size))
#define VKDRV_RD_ERR(_ret, _size)     ((_ret < 0) || (_ret != _size))

static int32_t set_buffer(void *handle, const vk2host_msg *vk2host,
			  const uint64_t user_data)
{
	vkil_buffer *buffer = handle;

	if (!vk2host->size) {
		/* a singe handle is returned */
		buffer->handle = vk2host->arg;
		buffer->user_data = user_data;
	} else if (buffer->type == VKIL_BUF_AG_BUFFERS) {
		uint32_t nhandles, i;
		vkil_aggregated_buffers *ag_buf = handle;

		ag_buf->nbuffers = 0; /* default: no buffer are written */

		/*
		 * a more complete answer is returned, the default
		 * is a collection of handles, so that supposes that
		 * the buffer is a n aggregation of buffers
		 */

		if (1 + (vk2host->size * 4) > VKIL_MAX_AGGREGATED_BUFFERS)
			goto fail;

		/* max number of returned handle */
		nhandles = MIN(1 + (vk2host->size * 4),
			       VKIL_MAX_AGGREGATED_BUFFERS);

		for (i = 0; i < nhandles ; i++) {
			/*
			 * if we have an handle without a matching buffer to
			 * write it, we fail, since we could loose track of
			 * the handle otherwise
			 * A NULL handle is however accepted to not have a
			 * corrsponding buffer
			 */
			if (!ag_buf->buffer[i] &&
					((uint32_t *)&(vk2host->arg))[i])
				goto fail;
			else if (ag_buf->buffer[i]) {
				ag_buf->buffer[i]->handle =
					((uint32_t *)&(vk2host->arg))[i];
				ag_buf->buffer[i]->user_data = user_data;
			}
			/* else no aggregatwd buffer but handle is null */
		}
		ag_buf->nbuffers = i;
		ag_buf->prefix.user_data = user_data;
	}
	return 0;

fail:
	return -EOVERFLOW;
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
static int32_t preset_host2vk_msg(host2vk_msg *msg2vk, const void *handle,
				  const vk_function_id_t fid,
				  const int64_t user_data)
{
	const vkil_context *ilctx = handle;

	int32_t ret, msg_id;

	VK_ASSERT(handle);
	VK_ASSERT(msg2vk);

	msg_id = vkil_get_msg_id(ilctx->devctx);
	if (msg_id < 0) {
		ret = -ENOBUFS;
		/* unable to get an id, too much message in transit */
		goto fail;
	}

	ret = vkil_set_msg_user_data(ilctx->devctx, msg_id, user_data);
	if (ret < 0)
		/* unable to set the user data */
		goto fail;

	msg2vk->msg_id = msg_id;
	msg2vk->queue_id = ilctx->context_essential.queue_id;
	msg2vk->context_id  = ilctx->context_essential.handle;
	msg2vk->function_id = fid;
	msg2vk->size        = 0;
	return 0;

fail:
	VKIL_LOG(VK_LOG_ERROR, "error %d on preset msg %p in ilctx %p",
		 ret, msg2vk, ilctx);
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
	int32_t ret;
	vkil_context *ilctx = handle;
	vkil_context_internal *ilpriv;
	host2vk_msg msg2vk;
	vk2host_msg msg2host;

	VK_ASSERT(handle);

	ilpriv = ilctx->priv_data;
	VK_ASSERT(ilpriv);

	if (ilctx->context_essential.handle < VK_START_VALID_HANDLE) {
		/* the call is allowed, but not necessarily expected */
		VKIL_LOG(VK_LOG_WARNING,
			 "in ilctx=%p, context 0x%x is not valid",
			 ilctx, ilctx->context_essential.handle);
		return 0;
	}

	ret = preset_host2vk_msg(&msg2vk, handle, VK_FID_DEINIT, 0);
	if (ret)
		goto fail_write;

	ret = vkil_write((void *)ilctx->devctx, &msg2vk);
	if (VKDRV_WR_ERR(ret, sizeof(msg2vk))) {
		vkil_return_msg_id(ilctx->devctx, msg2vk.msg_id);
		goto fail_write;
	}

	memset(&msg2host, 0, sizeof(msg2host));
	msg2host.msg_id = msg2vk.msg_id;
	msg2host.context_id = msg2vk.context_id;

	/*
	 * in the deinit phase the card will need to flush some stuff we don't
	 * have visibility at vkil, but it is expected this take longer time
	 * than usual so we don't abort at the first timeout
	 */

	ret = vkil_read((void *)ilctx->devctx, &msg2host, WAIT_INIT);
	if (VKDRV_RD_ERR(ret, sizeof(msg2host)))
		goto fail_read;

	vkil_return_msg_id(ilctx->devctx, msg2host.msg_id);

	VKIL_LOG(VK_LOG_DEBUG, "ilctx=%p, devctx=%p, context_id=0x%lx",
		 ilctx, ilctx->devctx, ilctx->context_essential.handle);
	return 0;

fail_write:
	/* the queue could be full (ENOFUS), so not a real error */
	VKIL_LOG(VK_LOG_ERROR, "failure %d on writing message in ilctx %p",
		 ret, ilctx);
	return ret;

fail_read:
	/*
	 * the response could take more time to return (ETIMEOUT),
	 * so not a real error
	 */
	VKIL_LOG(VK_LOG_ERROR, "failure %d on reading message in ilctx %p",
		 ret, ilctx);
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
	int32_t ret;
	vkil_context *ilctx = handle;
	vkil_context_internal *ilpriv;
	host2vk_msg msg2vk;
	vk2host_msg msg2host;

	VK_ASSERT(handle);

	ilpriv = ilctx->priv_data;

	VK_ASSERT(ilpriv);

	ret = preset_host2vk_msg(&msg2vk, handle, VK_FID_INIT, 0);
	if (ret)
		goto fail_write;
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
	if (VKDRV_WR_ERR(ret, sizeof(msg2vk))) {
		vkil_return_msg_id(ilctx->devctx, msg2vk.msg_id);
		goto fail_write;
	}

	memset(&msg2host, 0, sizeof(msg2host));
	msg2host.msg_id = msg2vk.msg_id;
	/*
	 * in the init phase the card will instantiate some stuff we don't have
	 * visibility at vkil, but it is expected this take longer time than
	 * usual so we don't abort at the first timeout
	 */
	ret = vkil_read((void *)ilctx->devctx, &msg2host, WAIT_INIT);
	if (VKDRV_RD_ERR(ret, sizeof(msg2host)))
		goto fail_read;

	vkil_return_msg_id(ilctx->devctx, msg2host.msg_id);
	if (msg2vk.context_id == VK_NEW_CTX)
		ilctx->context_essential.handle = msg2host.context_id;

	VKIL_LOG(VK_LOG_DEBUG, "ilctx=%p: card inited %p for context_id=0x%x",
		 ilctx, ilctx->devctx, ilctx->context_essential.handle);
	return 0;

fail_write:
	/* the queue could be full (ENOFUS), so not a real error */
	VKIL_LOG(VK_LOG_ERROR, "failure %d on writing message in ilctx %p",
		 ret, ilctx);
	return ret;

fail_read:
	/*
	 * the response could take more time to return (ETIMEOUT),
	 * so not a real error
	 */
	VKIL_LOG(VK_LOG_ERROR, "failure %d on reading message in ilctx %p",
		 ret, ilctx);
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
	int32_t ret;
	vkil_context *ilctx = handle;

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
	ret = vkil_mallocz(&ilctx->priv_data, sizeof(vkil_context_internal));
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
	vkil_free(&ilctx->priv_data);

fail_malloc:
	VKIL_LOG(VK_LOG_ERROR, "failed malloc");
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
		vkil_free((void **)&ilpriv);
	}
	vkil_free(handle);

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
		ret = vkil_mallocz(handle, sizeof(vkil_context));
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
		ret = vkil_init_com(*handle);
		if (ret)
			goto fail;
	}
	return 0;

fail_malloc:
	VKIL_LOG(VK_LOG_ERROR, "failed malloc");
	return ret;

fail:
	vkil_deinit(handle);
	return ret;
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
	else if (field == VK_PARAM_VIDEO_SCL_CONFIG)
		return sizeof(vk_scl_cfg);
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
	VK_ASSERT(cmd & VK_CMD_OPT_BLOCKING);

	ilpriv = ilctx->priv_data;
	VK_ASSERT(ilpriv);

	ret = preset_host2vk_msg(message, handle, VK_FID_SET_PARAM, 0);
	if (ret)
		goto fail_write;
	/* complete message setting */
	message->size        = msg_size;
	message->args[0]     = field;

	/* align  structure copy on 16 bytes boundary */
	memcpy(msg_size ? (uint32_t *) &message[1] : &message->args[1],
	       value, field_size);
	ret = vkil_write((void *)ilctx->devctx, message);
	if (VKDRV_WR_ERR(ret, sizeof(message))) {
		vkil_return_msg_id(ilctx->devctx, message->msg_id);
		goto fail_write;
	}

	if (cmd & VK_CMD_OPT_BLOCKING) {
		/* we wait for the the card response */
		vk2host_msg response;

		response.msg_id      = message->msg_id;
		response.queue_id    = ilctx->context_essential.queue_id;
		response.context_id  = ilctx->context_essential.handle;
		response.size        = 0;
		ret = vkil_read((void *)ilctx->devctx, &response, WAIT);
		if (VKDRV_RD_ERR(ret, sizeof(response)))
			goto fail_read;

		vkil_return_msg_id(ilctx->devctx, response.msg_id);
	}
	return 0;

fail_write:
	/* the queue could be full (ENOFUS), so not a real error */
	VKIL_LOG(VK_LOG_ERROR, "failure %d on writing message in ilctx %p",
		 ret, ilctx);
	return ret;

fail_read:
	/*
	 * the response could take more time to return (ETIMEOUT),
	 * so not a real error
	 */
	VKIL_LOG(VK_LOG_WARNING, "failure %d on reading message in ilctx %p",
		 ret, ilctx);
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
	VK_ASSERT(cmd & VK_CMD_OPT_BLOCKING);

	ret = preset_host2vk_msg(message, handle, VK_FID_GET_PARAM, 0);
	if (ret)
		goto fail_write;
	/* complete setting */
	message->size        = msg_size;
	message->args[0]       = field;
	memcpy(msg_size ? (uint32_t *) &message[1] : &message->args[1],
	       value, field_size);

	ret = vkil_write((void *)ilctx->devctx, message);
	if (VKDRV_WR_ERR(ret, sizeof(message))) {
		vkil_return_msg_id(ilctx->devctx, message->msg_id);
		goto fail_write;
	}

	if (cmd & VK_CMD_OPT_BLOCKING) {
		/* we wait for the the card response */
		vk2host_msg response[msg_size + 1];

		response->msg_id      = message->msg_id;
		response->queue_id    = ilctx->context_essential.queue_id;
		response->context_id  = ilctx->context_essential.handle;
		response->size        = msg_size;
		ret = vkil_read((void *)ilctx->devctx, response, WAIT);
		if (VKDRV_RD_ERR(ret, sizeof(response)))
			goto fail_read;

		vkil_return_msg_id(ilctx->devctx, response->msg_id);
		memcpy(value,
			&((&(response->arg))[msg_size ? 1 : 0]), field_size);
	}
	return 0;

fail_write:
	/* the queue could be full (ENOFUS), so not a real error */
	VKIL_LOG(VK_LOG_ERROR, "failure %d on writing message in ilctx %p",
		 ret, ilctx);
	return ret;

fail_read:
	/*
	 * the response could take more time to return (ETIMEOUT),
	 * so not necessarily a real error
	 */
	VKIL_LOG(VK_LOG_WARNING, "failure %d on reading message in ilctx %p",
		 ret, ilctx);
	return ret;
};

/**
 * convert a front end buffer prefix structure into a backend one
 * (they can be different or the same).
 * @param[out] surface    handle to the backend structure
 * @param[in]  il_surface handle to a front hand packet structure
 * @return          zero on succes, error code otherwise
 */
static int32_t convert_vkil2vk_buffer_prefix(vk_buffer *dst,
					     const vkil_buffer *org)
{
	/*
	 * here we assume we are on a 64 bit architecture
	 * the below is expected to work on 32 bits arch too but has not been
	 * tested
	 */
	VK_ASSERT(sizeof(void *) == sizeof(uint64_t));

	dst->handle        = org->handle;
	dst->user_data_tag = org->user_data;
	dst->flags         = org->flags;
	dst->port_id       = org->port_id;
	return 0;
}

/**
 * get the number of planes to transfer
 * @param[in]  il_packet    handle to a front hand packet structure
 * @return                  number of pointer to convey to the card
 */
static int32_t get_vkil_nplanes(const void *il_buffer)
{
	int32_t ret;

	VK_ASSERT(il_buffer);

	switch (((vkil_buffer *)il_buffer)->type) {
	case VKIL_BUF_PACKET:
	case VKIL_BUF_META_DATA:
		ret = 1;
		break;
	case VKIL_BUF_SURFACE:
		ret = 4;
		break;
	default:
		goto fail;
	}
	return ret;

fail:
	VKIL_LOG(VK_LOG_ERROR, "error in ilbuffer=%p", il_buffer);
	return -EINVAL;
}

/**
 * convert a front end surface structure into a backend one
 * (they can be different or the same).
 * @param[out] surface    handle to the backend structure
 * @param[in]  il_surface handle to a front hand packet structure
 * @return          zero on succes, error code otherwise
 */
static int32_t convert_vkil2vk_buffer_surface(vk_buffer_surface *surface,
					const vkil_buffer_surface *ilsurface)
{
	int32_t is_interlaced, height, size[2];

	/*
	 * here we assume we are on a 64 bit architecture
	 * the below is expected to work on 32 bits arch too but has not been
	 * tested
	 */
	VK_ASSERT(sizeof(void *) == sizeof(uint64_t));

	convert_vkil2vk_buffer_prefix(&surface->prefix, &ilsurface->prefix);
	surface->prefix.type  = VK_BUF_SURFACE;
	surface->stride[0]    = ilsurface->stride[0];
	surface->stride[1]    = ilsurface->stride[1];

	is_interlaced = ((vkil_buffer *)ilsurface)->flags &
				VKIL_BUFFER_SURFACE_FLAG_INTERLACE;
	height = ilsurface->max_size.height;
	if (is_interlaced) /* make height a  multiple of 2 */
		height += height % 2;

	switch (((vkil_buffer_surface *)ilsurface)->format) {
	case VKIL_FORMAT_YOL8: /* intentional fall thru */
	case VKIL_FORMAT_YOL10:
		/*
		 * in YOL, we use 2x2 pels block, so the height is expressed
		 * in this unit
		 */
		height >>= 1; /* each pel will take 2 bytes */
		size[1] = 0;
		surface->format = VK_FORMAT_YOL2;
		break;
	case VKIL_FORMAT_YUV420_P010:
		size[1] = (((height + 1) / 2) * ilsurface->stride[1]) >>
							     is_interlaced;
		surface->format = VK_FORMAT_P010;
		break;
	case VKIL_FORMAT_YUV420_NV12:
		size[1] = (((height + 1) / 2) * ilsurface->stride[1]) >>
							     is_interlaced;
		surface->format = VK_FORMAT_NV12;
		break;
	default:
		goto fail;
	}

	size[0] = (height * ilsurface->stride[0]) >> is_interlaced;

	surface->max_size.width   = ilsurface->max_size.width;
	surface->max_size.height  = ilsurface->max_size.height;

	surface->planes[0].address = (uint64_t)ilsurface->plane_top[0];
	surface->planes[0].size    = size[0];
	surface->planes[1].address = (uint64_t)ilsurface->plane_top[1];
	surface->planes[1].size    = size[1];

	if (is_interlaced) {
		surface->planes[2].address = (uint64_t)ilsurface->plane_bot[0];
		surface->planes[2].size    = size[0];
		surface->planes[3].address = (uint64_t)ilsurface->plane_top[1];
		surface->planes[3].size    = size[1];
	} else {
		surface->planes[2].address = 0;
		surface->planes[2].size = 0;
		surface->planes[3].address = 0;
		surface->planes[3].size = 0;
	}
	return 0;

fail:
	VKIL_LOG(VK_LOG_ERROR, "error in ilbuffer=%p", ilsurface);
	return -EINVAL;
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

	convert_vkil2vk_buffer_prefix(&packet->prefix, &il_packet->prefix);
	packet->prefix.type   = VK_BUF_PACKET;
	packet->size          = il_packet->size;
	packet->data          = (uint64_t)il_packet->data;
	return 0;
}

/**
 * convert a front end metadata structure into a backend one
 * (they can be different or the same).
 * @param[out] packet    handle to the backend structur
 * @param[in]  il_packet    handle to a front hand packet structuree
 * @return          zero on succes, error code otherwise
 */
static int32_t convert_vkil2vk_buffer_metadata(vk_buffer_metadata *mdata,
					const vkil_buffer_metadata *il_mdata)
{
	/*
	 * here we assume we are on a 64 bit architecture
	 * the below is expected to work on 32 bits arch too but has not been
	 * tested
	 */
	VK_ASSERT(sizeof(void *) == sizeof(uint64_t));

	convert_vkil2vk_buffer_prefix(&mdata->prefix, &il_mdata->prefix);
	mdata->prefix.type     = VK_BUF_METADATA;
	mdata->size            = il_mdata->size;
	mdata->data            = (uint64_t)il_mdata->data;
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
	VK_ASSERT(il_buffer);
	VK_ASSERT(buffer);

	switch (((vkil_buffer *)il_buffer)->type) {
	case	VKIL_BUF_PACKET:
		return convert_vkil2vk_buffer_packet(buffer, il_buffer);
	case	VKIL_BUF_SURFACE:
		return convert_vkil2vk_buffer_surface(buffer, il_buffer);
	case	VKIL_BUF_META_DATA:
		return convert_vkil2vk_buffer_metadata(buffer, il_buffer);
	}
	return -EINVAL;
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
	VK_ASSERT(il_buffer);

	switch (((vkil_buffer *)il_buffer)->type) {
	case	VKIL_BUF_PACKET:  return sizeof(vk_buffer_packet);
	case	VKIL_BUF_SURFACE: return sizeof(vk_buffer_surface);
	case	VKIL_BUF_META_DATA: return sizeof(vk_buffer_metadata);
	}

	return -EINVAL;
}

/**
 * transfer buffers
 * @param[in] component_handle handle to a vkil_context
 * @param[in] host_buffer      buffer to transfer
 * @param[in] cmd              transfer direction (upload/download) and mode
 * @return                     zero on success, error code otherwise
 */
static int32_t vkil_transfer_buffer(void *component_handle,
				    void *buffer_handle,
				    const vkil_command_t cmd)
{
	int32_t ret, msg_id = 0;
	vkil_buffer *buffer = buffer_handle;
	const vkil_context *ilctx = component_handle;
	const vkil_command_t load_mode = cmd & VK_CMD_MASK;
	int32_t size = get_vkil2vk_buffer_size(buffer);
	vkil_context_internal *ilpriv;
	int32_t msg_size = MSG_SIZE(size);
	host2vk_msg message[msg_size + 1];

	VKIL_LOG(VK_LOG_DEBUG, "ilctx=%p, buffer=%p, cmd=0x%x (%s%s)",
		 ilctx,
		 buffer_handle,
		 cmd,
		 vkil_cmd_str(cmd),
		 vkil_cmd_opts_str(cmd));
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
	if (!(cmd & VK_CMD_OPT_CB)) {
		ret = preset_host2vk_msg(message,
					 component_handle,
					 VK_FID_TRANS_BUF,
					 buffer->user_data);
		if (ret)
			goto fail_write;

		/* complete setting */
		message->size        = msg_size;
		message->args[0]     = load_mode;

		ret = get_vkil_nplanes(buffer_handle);
		if (ret < 0) {
			VKIL_LOG(VK_LOG_WARNING, "");
			goto fail_write;
		}
		message->args[0]     |= ret;

		/* we convert the il frontend structure into a backend one */
		convert_vkil2vk_buffer(&message->args[2], buffer);

		/* then we write the command to the queue */
		ret = vkil_write((void *)ilctx->devctx, message);
		if (VKDRV_WR_ERR(ret, sizeof(host2vk_msg)*(msg_size + 1))) {
			vkil_return_msg_id(ilctx->devctx, message->msg_id);
			goto fail_write;
		}
		msg_id = message->msg_id;
	}

	if ((cmd & VK_CMD_OPT_BLOCKING) || (cmd & VK_CMD_OPT_CB)) {
		/* we check for the the card response */
		vk2host_msg response;
		int32_t wait = (cmd & VK_CMD_OPT_BLOCKING) ? WAIT : 0;

		response.function_id  = VK_FID_TRANS_BUF_DONE;
		response.msg_id      = msg_id;
		response.queue_id    = ilctx->context_essential.queue_id;
		response.context_id  = ilctx->context_essential.handle;
		response.size        = 0;
		ret = vkil_read((void *)ilctx->devctx, &response, wait);
		if (VKDRV_RD_ERR(ret, sizeof(response)))
			goto fail_read;

		buffer->handle = response.arg;
		ret = vkil_get_msg_user_data(ilctx->devctx, response.msg_id,
					     &buffer->user_data);
		/* we return the message no matter the error status above */
		vkil_return_msg_id(ilctx->devctx, response.msg_id);
		if (ret)
			goto fail_read;
	}
	return 0;

fail_write:
	/*
	 * the input queue could be full (ENOBUFS),
	 * so not necessarily always an real error
	 */
	VKIL_LOG(VK_LOG_ERROR,
		 "failure %d on writing message in ilctx %p ",
		 ret,
		 ilctx);
	return ret;

fail_read:
	/*
	 * the response could take more time to return (ETIMEOUT),
	 * so not necessarily always a real error
	 */
	if ((ret == -ENOMSG) || (ret == -EAGAIN))
		return -EAGAIN; /* request the host to try again */

	VKIL_LOG(VK_LOG_WARNING,
		 "failure %d on reading message in ilctx %p",
		 ret,
		 ilctx);
	return ret;

};

/**
 * process buffer
 *
 * @param component_handle    handle to a vkil_context
 * @param buffer_handle       buffer to process
 * @param cmd                 options (blocking, call back call,...)
 * @return                    zero on success, error code otherwise
 */
int32_t vkil_process_buffer(void *component_handle,
			    void *buffer_handle,
			    const vkil_command_t cmd)
{
	const vkil_context *ilctx = component_handle;
	vkil_context_internal *ilpriv;
	vkil_buffer *buffer;
	int32_t ret;
	int32_t msg_id = 0;

	VKIL_LOG(VK_LOG_DEBUG, "ilctx=%p, buffer=%p, cmd=0x%x (%s%s)",
		 ilctx,
		 buffer_handle,
		 cmd,
		 vkil_cmd_str(cmd),
		 vkil_cmd_opts_str(cmd));

	VK_ASSERT(component_handle);
	VK_ASSERT(buffer_handle);

	ilpriv = ilctx->priv_data;
	buffer = buffer_handle;

	VK_ASSERT(ilpriv);

	if (!(cmd & VK_CMD_OPT_CB)) {
		host2vk_msg message;

		ret = preset_host2vk_msg(&message, component_handle,
					 VK_FID_PROC_BUF,
					 buffer->user_data);
		if (ret)
			goto fail_write;

		/* complete message setting */
		message.args[0]     = cmd & VK_CMD_MASK;
		message.args[1]     = buffer->handle;

		ret = vkil_write((void *)ilctx->devctx, &message);
		if (VKDRV_WR_ERR(ret, sizeof(message))) {
			vkil_return_msg_id(ilctx->devctx,
					   message.msg_id);
			goto fail_write;
		}

		msg_id = message.msg_id;
	}

	if ((cmd & VK_CMD_OPT_BLOCKING) || (cmd & VK_CMD_OPT_CB)) {
		/* we check for the the card response */
		vk2host_msg response[VKIL_RET_MSG_MAX_SIZE];
		int32_t wait = (cmd & VK_CMD_OPT_BLOCKING) ? WAIT : 0;
		uint64_t user_data;

		response->function_id = VK_FID_PROC_BUF_DONE;
		response->msg_id      = msg_id;
		response->queue_id    = ilctx->context_essential.queue_id;
		response->context_id  = ilctx->context_essential.handle;
		response->size        = VKIL_RET_MSG_MAX_SIZE - 1;
		ret = vkil_read((void *)ilctx->devctx, response, wait);
		if (VKDRV_RD_ERR(ret,
				 sizeof(vk2host_msg) * (response->size + 1)))
			goto fail_read;

		ret = vkil_get_msg_user_data(ilctx->devctx, response->msg_id,
					     &user_data);
		/* we return the message no matter the error status above */
		vkil_return_msg_id(ilctx->devctx, response->msg_id);
		if (ret)
			goto fail_read;
		ret = set_buffer(buffer, response, user_data);
		if (ret)
			goto fail_read;
	}
	return ret;

fail_write:
	/*
	 * the input queue could be full (ENOBUFS),
	 * so not necessarily always a real error
	 */
	if (ret == -ENOBUFS)
		/*
		 * This is probably caused by a backlog on the return queue.
		 * The host should thain drain this queue first before
		 * retrying to write on the input queue
		 */
		return ret; /* request the host to drain the queue first */

	VKIL_LOG(VK_LOG_ERROR,
		 "failure %d on writing message in ilctx %p",
		 ret,
		 ilctx);
	return ret;

fail_read:
	/*
	 * the response could take more time to return (ETIMEOUT),
	 * so not necessarily always a real error
	 */
	if ((ret == -ENOMSG) || (ret == -EAGAIN))
		return -EAGAIN; /* request the host to try again */

	VKIL_LOG(VK_LOG_WARNING,
		 "failure %d on reading message in ilctx %p",
		 ret,
		 ilctx);
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

	if (vkil_mallocz((void **)&ilapi, sizeof(vkil_api)))
		return NULL;

	*ilapi = (vkil_api) {
		.init                  = vkil_init,
		.deinit                = vkil_deinit,
		.set_parameter         = vkil_set_parameter,
		.get_parameter         = vkil_get_parameter,
		.transfer_buffer       = vkil_transfer_buffer,
		.process_buffer        = vkil_process_buffer,
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

	vkil_free(ilapi);

	return 0;
}

/**
 * Set device to be used, configured by user CLI
 * @param device id in ASCII format
 */
void vkil_set_affinity(const char *device)
{
	VKIL_LOG(VK_LOG_DEBUG, "Device %s specified by user.",
		 device ? device : "NULL");
	vkil_cfg.vkapi_device = device;
}

/**
 * Get device configured and used by user CLI
 * @param device id in ASCII format
 */
const char *vkil_get_affinity(void)
{
	VKIL_LOG(VK_LOG_DEBUG, "Return %s chosen by user.",
		 vkil_cfg.vkapi_device ? vkil_cfg.vkapi_device : "NULL");
	return vkil_cfg.vkapi_device;
}
