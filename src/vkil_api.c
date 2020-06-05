// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(c) 2019 Broadcom
 */

/**
 * @file
 * @brief front end vkil access functions
 *
 * This file defines all the front end functions, including the API to be
 * exposed to the caller. The front end functions
 * read, resp. write messages to the VKIL backend part (vkil_backend.c)
 */

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include "vk_buffers.h"
#include "vkil_api.h"
#include "vkil_backend.h"
#include "vkil_internal.h"
#include "vkil_utils.h"

/*
 * store global configurations that are from CLI parser
 */
static struct _vkil_cfg {
	const char *vkapi_device; /* device/affinity, which card to be used */
	uint32_t    vkapi_processing_pri; /* processing priority */
} vkil_cfg = { NULL, VKIL_DEF_PROCESSING_PRI };

/*
 * usually we wait for response message up to TIMEOUT us
 * However, in certain case, (essentially component initialization)
 * we could need to provide more time for the card to do the initialization
 * (at this time, all this is empiric)
 * TODO: implement a polling on ctrl queue (sideband like), to enquire the
 * status of the card rather than to rely on an arbitrary time out value
 */

/** this VKIL_READ_TIMEOUT factor tells vkil_read to wait "normal time"
 * for message
 */
#ifndef VKIL_READ_TIMEOUT
#define VKIL_READ_TIMEOUT 1
#endif
/** this wait factor tells vkil_read to wait "extra time" for message */
#define WAIT_INIT (VKIL_READ_TIMEOUT * 10)

/** max expected return message size, can be locally overidden */
#define VKIL_RET_MSG_MAX_SIZE 16

/** max msg size that can be sent to card */
#define VKIL_SEND_MSG_MAX_SIZE 16

/**
 * @brief instrument the write failure

 * @param[in] error error code to be processed
 * @param[out] ilctx il context emitting the write command
 * @return error
 */
static int fail_write(const int error, const void *ilctx)
{
	/*
	 * some cases are intercepted here and handled. Right now, only
	 * -EAGAIN and -EPERM, where the formal is when the h2vk queue is
	 * full, and the second case is when the drive access is off - ie,
	 * no communication.  In both cases, we could not continue.
	 * FFMPEG with VK is working in a pipelined fashion, and when one
	 * component exits, it is observed that other threads do not seem
	 * to know, so we need to do some special handling here.
	 */
	VKIL_LOG(VK_LOG_ERROR,
		 "Failure on writing mssage in ilctx %p - %s(%d)\n",
		 ilctx, strerror(-error), error);
	if ((error == -EAGAIN) || (error == -EPERM))
		kill(getpid(), SIGINT);

	return error;
}

/**
 * @brief instrument the read failure

 * @param[in] error error code to be processed
 * @param[out] ilctx il context emitting the read command
 * @return error
 */
static int fail_read(const int error, const void *ilctx)
{
	if ((error == -ENOMSG) || (error == -EAGAIN))
		return -EAGAIN; /* request the host to try again */
	else if (error)
		VKIL_LOG(error == -ETIMEDOUT ? VK_LOG_WARNING : VK_LOG_ERROR,
			 "failure %s (%d) on reading message in ilctx %p",
			 strerror(-error), error, ilctx);
	return error;
}

/**
 * @brief extract handles from the input buffer
 * that have to be processed
 * @param[in] handle input buffer to be processed
 * @param[out] nbuf number of buffers present in handle
 * @param[out] handles extracted from the input buffer
 */
static void get_buffer(void *handle, uint32_t *nbuf,
		       uint32_t *handles)
{
	vkil_buffer *buffer = handle;
	uint32_t i, nxt_msg_size_lvl;
	vkil_aggregated_buffers *ag_buf;

	if (buffer->type != VKIL_BUF_AG_BUFFERS) {
		*nbuf = 1;
		handles[0] = buffer->handle;
		return;
	}

	/* We have aggregated buffers */
	ag_buf = handle;
	for (i = 0; i < ag_buf->nbuffers; i++) {
		if (!ag_buf->buffer[i]) {
			handles[i] = 0;
		} else {
			buffer = ag_buf->buffer[i];
			handles[i] = buffer->handle;
		}
	}

	VK_ASSERT(i > 0);

	if (i > 1) {
		/* i will be rounded of to values - 5, 9, 13 & so on */
		nxt_msg_size_lvl = 5 + (((i - 2) / 4) * 4);
		while (i < nxt_msg_size_lvl)
			handles[i++] = 0;
	}

	*nbuf = i;
}

/* macros for handling error condition */
#define VKDRV_WR_ERR(_ret) ((_ret) < 0)
#define VKDRV_RD_ERR(_ret) ((_ret < 0) && (_ret != -EADV))

/**
 * @brief populate a buffer descriptor from a message
 * @param[in,out] handle buffer descriptor to be populated
 * @param[in] ref_data reference increment decrement
 */
static int32_t buffer_ref(vkil_buffer *buffer, const int ref_delta)
{
	int i;

	if (buffer->type != VKIL_BUF_AG_BUFFERS) {
		/* a single handle is returned */
		if (buffer->handle)
			buffer->ref += ref_delta;
	} else if (buffer->type == VKIL_BUF_AG_BUFFERS) {
		vkil_aggregated_buffers *ag_buf =
			(vkil_aggregated_buffers *)buffer;

		for (i = 0; i < ag_buf->nbuffers; i++) {
			if (ag_buf->buffer[i] && ag_buf->buffer[i]->handle)
				ag_buf->buffer[i]->ref += ref_delta;
		}
	}

	return 0;
}

/**
 * @brief populate a buffer descriptor from a message
 * @param[in,out] handle buffer descriptor to be populated
 * @param[in] vk2host message to read
 * @param[in] user_data user data to be used
 */
static int32_t set_buffer(void *handle, const vk2host_msg *vk2host,
			  const uint64_t user_data, const int ref_delta)
{
	vkil_buffer *buffer = handle;

	if (!vk2host->size && (buffer->type != VKIL_BUF_AG_BUFFERS)) {
		/* a singe handle is returned */
		buffer->handle = vk2host->arg;
		buffer->user_data = user_data;
		buffer->ref += ref_delta;
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

			VKIL_LOG(VK_LOG_DEBUG,
				 "i=%d buffer=%p handle=0x%" PRIx32,
				 i,
				 ag_buf->buffer[i],
				 ((uint32_t *)&(vk2host->arg))[i]);

			fflush(stdout);
			if (!ag_buf->buffer[i] &&
					((uint32_t *)&(vk2host->arg))[i])
				goto fail;
			else if (ag_buf->buffer[i]) {
				ag_buf->buffer[i]->handle =
					((uint32_t *)&(vk2host->arg))[i];
				ag_buf->buffer[i]->user_data = user_data;
				ag_buf->buffer[i]->ref += ref_delta;
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
 * @brief prepopulate the command message with control field
 * arguments
 *
 * @param[in,out] msg2vk message to prepolulate
 * @param[in] handle handle to context
 * @param[in] fid function name to be transmitted into the messaage
 * @param[in] user_data user data to be used
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
 * @brief On card context deinitialization command
 *
 * This command is blocking and return on response from the card
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
	VK_ASSERT(ilctx->devctx);

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
	if (VKDRV_WR_ERR(ret)) {
		vkil_return_msg_id(ilctx->devctx, msg2vk.msg_id);
		goto fail_write;
	}

	memset(&msg2host, 0, sizeof(msg2host));
	msg2host.msg_id = msg2vk.msg_id;
	msg2host.queue_id = msg2vk.queue_id;
	msg2host.context_id = msg2vk.context_id;

	/*
	 * in the deinit phase the card will need to flush some stuff we don't
	 * have visibility at vkil, but it is expected this take longer time
	 * than usual so we don't abort at the first timeout
	 */

	ret = vkil_read((void *)ilctx->devctx, &msg2host, WAIT_INIT);
	if (VKDRV_RD_ERR(ret))
		goto fail_read;

	vkil_return_msg_id(ilctx->devctx, msg2host.msg_id);

	VKIL_LOG(VK_LOG_DEBUG, "ilctx=%p, devctx=%p, context_id=0x%" PRIx32,
		 ilctx, ilctx->devctx, ilctx->context_essential.handle);
	return ret;

fail_write:
	return fail_write(ret, ilctx);

fail_read:
	return fail_read(ret, ilctx);
}

/**
 * @brief On card context initialization command
 *
 * This command is blocking and return on response from the card which provides
 * the context handle
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
	if (VKDRV_WR_ERR(ret)) {
		vkil_return_msg_id(ilctx->devctx, msg2vk.msg_id);
		goto fail_write;
	}

	memset(&msg2host, 0, sizeof(msg2host));
	msg2host.msg_id = msg2vk.msg_id;
	msg2host.queue_id = msg2vk.queue_id;

	/*
	 * in the init phase the card will instantiate some stuff we don't have
	 * visibility at vkil, but it is expected this take longer time than
	 * usual so we don't abort at the first timeout
	 */
	ret = vkil_read((void *)ilctx->devctx, &msg2host, WAIT_INIT);
	if (VKDRV_RD_ERR(ret))
		goto fail_read;

	vkil_return_msg_id(ilctx->devctx, msg2host.msg_id);
	if (msg2vk.context_id == VK_NEW_CTX)
		ilctx->context_essential.handle = msg2host.context_id;

	VKIL_LOG(VK_LOG_DEBUG, "ilctx=%p: card inited %p for context_id=0x%x",
		 ilctx, ilctx->devctx, ilctx->context_essential.handle);
	return ret;

fail_write:
	return fail_write(ret, ilctx);

fail_read:
	return fail_read(ret, ilctx);
}

/**
 * @brief initialize a context
 *
 * @li this function create the private data container
 * @li This function will load a device driver if required. If a device is
 * already opened, it will add a reference to it
 *
 * @param handle    handle to a vkil_context
 * @return          zero on succes, error code otherwise
 *
 * @pre @p handle must already be a _vkil_context but it's private data
 * no yet created (pointing to NULL).
 */
static int32_t vkil_init_ctx(void *handle)
{
	int32_t ret;
	vkil_context *ilctx = handle;

	VK_ASSERT(ilctx);
	VK_ASSERT(!ilctx->priv_data);

	ilctx->context_essential.handle = VK_NEW_CTX;
	ilctx->context_essential.pid = getpid();

	/* the priv_data structure size could be component specific */
	ret = vkil_mallocz(&ilctx->priv_data, sizeof(vkil_context_internal));
	if (ret)
		goto fail;

	/*
	 * we pair the device initialization with the private data one to
	 * prevent multiple device opening
	 */
	ret = vkil_init_dev(&ilctx->devctx);
	if (ret < 0)
		goto fail;

	return 0;

fail:
	vkil_free(&ilctx->priv_data);
	VKIL_LOG(VK_LOG_ERROR, "initialization failure %d for ilctx %p",
		 ret, ilctx);
	return ret;
}

/**
 * @brief de-initialize the device context
 *
 * This function will unload the device driver if not used anymore
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
 * @brief initialize a vkil_context
 *
 * This function should be called twice:
 * @li The first call creates a software only vkil_context. It is then the
 * the caller responsability to populate the
 * vkil_context::vkil_context_essential fields describing the context role
 * then call the init function again
 * @li the second call create the hw context associated to the sofware context
 * using the vkil_context::vkil_context_essential description.
 * the hw context is abstracted, and can't be accessed by the host
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
 * @brief return the size in bytes of the structure associated to a
 * vkil_parameter_t
 *
 * @param  field the field to evaluate
 * @return size of the structure aasociated to field
 */
static int32_t vkil_get_struct_size(const vkil_parameter_t field)
{
	if (field == VK_PARAM_PORT)
		return sizeof(vk_port);
	else if (field == VK_PARAM_VIDEO_ENC_CONFIG)
		return sizeof(vk_enc_cfg);
	else if (field == VK_PARAM_VIDEO_SCL_CONFIG)
		return sizeof(vk_scl_cfg);
	else if (field == VK_PARAM_FLASH_IMAGE_CONFIG)
		return sizeof(vk_flash_image_cfg);
	else if (field == VK_PARAM_POOL_SIZE_CONFIG)
		return sizeof(vk_pool_size_cfg);
	else if (field == VK_PARAM_POOL_ALLOC_BUFFER)
		return sizeof(vk_pool_alloc_buffer);
	else if (field == VK_PARAM_ERROR)
		return sizeof(vk_error);
	/* this is the default value when not structure is defined */
	return sizeof(int32_t);
}

/**
 * @brief set a vkil_context parameter
 *
 * @param handle    handle to a vkil_context
 * @param field     field to set
 * @param value     value to set the field to if the field is a uint32_t,
 *                  pointer to a structure assciated to the field otherwise
 * @param cmd       command describing if the function is blocking or not
 * @return          zero on success, error code otherwise
 */
int32_t vkil_set_parameter(void *handle,
			   const vkil_parameter_t field,
			   const void *value,
			   const vkil_command_t cmd)
{
	const vkil_context *ilctx = handle;
	int32_t ret;
	int32_t field_size = vkil_get_struct_size(field);
	/* message size is expressed in 16 bytes unit */
	int32_t msg_size = field_size == sizeof(uint32_t) ?
					0 : MSG_SIZE(field_size);
	host2vk_msg message[msg_size + 1];

	VKIL_LOG(VK_LOG_DEBUG, "");
	VK_ASSERT(handle); /* sanity check */

	/* TODO: non blocking option not yet implemented */
	VK_ASSERT(cmd & VK_CMD_OPT_BLOCKING);

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
	if (VKDRV_WR_ERR(ret)) {
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
		ret = vkil_read((void *)ilctx->devctx, &response,
				VKIL_READ_TIMEOUT);
		if (VKDRV_RD_ERR(ret))
			goto fail_read;

		vkil_return_msg_id(ilctx->devctx, response.msg_id);
	}
	return ret;

fail_write:
	return fail_write(ret, ilctx);

fail_read:
	return fail_read(ret, ilctx);
};

/**
 * @brief get a parameter from a vkil_context
 *
 * @param handle handle to a vkil_context
 * @param field  field to get
 * @param value  pointer to a structure associated to the field where to
		 write the read parameters
 * @param cmd    command describing if the function is blocking or not
 * @return	 zero on success, error code otherwise
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
	if (VKDRV_WR_ERR(ret)) {
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
		ret = vkil_read((void *)ilctx->devctx, response,
				VKIL_READ_TIMEOUT);
		if (VKDRV_RD_ERR(ret))
			goto fail_read;

		vkil_return_msg_id(ilctx->devctx, response->msg_id);
		memcpy(value,
		       &((vk2host_getargp(response))[msg_size ? 1 : 0]),
		       field_size);
	}
	return ret;

fail_write:
	return fail_write(ret, ilctx);

fail_read:
	return fail_read(ret, ilctx);
};

/**
 * @brief convert a front end buffer prefix structure into a backend one
 * (they can be different or the same).
 * @param[out] surface    handle to the backend structure
 * @param[in]  il_surface handle to a front hand buffer structure
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
 * @brief get the number of planes to transfer
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
 * @brief convert a front end surface structure into a backend one
 * (they can be different or the same).
 * @param[out] surface    handle to the backend structure
 * @param[in]  il_surface handle to a front hand packet structure
 * @pre _vkil_buffer_surface::stride are required to be a multiple of 4
 * both in luma and chroma component
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

	/*
	 * PAX DMA transfer sizes are required to be 4 byte aligned
	 * we guarantee this by enforcing the image stride to be 4 byte aligned
	 */
	VK_ASSERT(!(ilsurface->stride[0] & 0x3));
	VK_ASSERT(!(ilsurface->stride[1] & 0x3));

	convert_vkil2vk_buffer_prefix(&surface->prefix, &ilsurface->prefix);
	surface->prefix.type  = VK_BUF_SURFACE;
	surface->stride[0]    = ilsurface->stride[0];
	surface->stride[1]    = ilsurface->stride[1];

	is_interlaced = ((vkil_buffer *)ilsurface)->flags &
				VKIL_BUFFER_SURFACE_FLAG_INTERLACE;
	height = ilsurface->max_size.height;
	if (is_interlaced) /* make height a  multiple of 2 */
		height += height % 2;

	surface->format = ((vkil_buffer_surface *)ilsurface)->format;

	switch (surface->format) {
	case VK_FORMAT_YOL2:
		/*
		 * in YOL2, we use 2x2 pels block, so the height is expressed
		 * in this unit
		 */
		height >>= 1; /* each pel will take 2 bytes */
		size[1] = 0;
		break;
	case VK_FORMAT_P010:
	case VK_FORMAT_NV21:
	case VK_FORMAT_NV12:
		size[1] = (((height + 1) / 2) * ilsurface->stride[1]) >>
							     is_interlaced;
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
	surface->quality = ilsurface->quality;
	return 0;

fail:
	VKIL_LOG(VK_LOG_ERROR, "invalid format request for ilbuffer %p",
		 ilsurface);
	return -EINVAL;
}

/**
 * @brief convert a front end packet structure into a backend one
 * (they can be different or the same).
 * @param[out] packet    handle to the backend structure
 * @param[in]  il_packet handle to a front hand packet structure
 * @pre _vkil_buffer_packet::size is required to be a multiple of 4
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

	/* PAX DMA transfer sizes are required to be 4 byte aligned */
	VK_ASSERT(!(il_packet->size & 0x3));

	convert_vkil2vk_buffer_prefix(&packet->prefix, &il_packet->prefix);
	packet->prefix.type   = VK_BUF_PACKET;
	packet->used_size     = il_packet->used_size;
	packet->size          = il_packet->size;
	packet->data          = (uint64_t)il_packet->data;
	return 0;
}

/**
 * @brief convert a front end metadata structure into a backend one
 * (they can be different or the same).
 * @param[out] packet    handle to the backend structure
 * @param[in]  il_packet handle to a front hand metdata structure
 * @pre vkil_buffer_metadata::size is required to be a multiple of 4
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

	/* PAX DMA transfer sizes are required to be 4 byte aligned */
	VK_ASSERT(!(il_mdata->size & 0x3));

	convert_vkil2vk_buffer_prefix(&mdata->prefix, &il_mdata->prefix);
	mdata->prefix.type     = VK_BUF_METADATA;
	mdata->used_size       = il_mdata->used_size;
	mdata->size            = il_mdata->size;
	mdata->data            = (uint64_t)il_mdata->data;
	return 0;
}

/**
 * @brief convert a front end buffer structure into a backend one
 * (they can be different or the same).
 * @param[in]  il_packet handle to a front hand packet structure
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
 * @brief get the size of the vk backend paired structure
 * (they can be different or the same).
 * @param[in]  il_packet handle to a front hand packet structure
 * @param[out] packet    handle to the backend structure
 * @return               size of the backend structure
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
 * @brief buffer sanity check
 *
 * perform a sanity check, to ensure we provide a valid buffer to the HW
 * the sainity check consist to verify that the buffer type is a avlid one
 * @param  buffer
 * @return zero on success, error code otherwise
 */
static int32_t vkil_sanity_check_buffer(vkil_buffer *buffer)
{
	switch (buffer->type) {
	case VKIL_BUF_META_DATA:
	case VKIL_BUF_PACKET:
	case VKIL_BUF_SURFACE:
	case VKIL_BUF_AG_BUFFERS:
		return 0;
	}
	return -EINVAL;
}

/**
 * @brief transfer buffers
 *
 * This function need to be called for all buffer to transfer to/from the the
 * Valkyrie card.
 * @li all buffer transfer are done via DMA
 * @li the card memory management are under the card control, typically an
 * upload infers a memory allocation on the card, and a downlaod a memory
 * freeing. the vkil see only opaque handle  to on card buffer descriptor
 * in no case the host can see the on card used memory addresses
 *
 * @param[in] component_handle	handle to a vkil_context
 * @param[in] host_buffer	buffer to transfer
 * @param[in] cmd		transfer direction (upload/download) and mode
 *				(blocking or not)
 * @return			zero on success, error code otherwise
 * @pre the  _vkil_buffer to transfer must have a valid _vkil_buffer_type
 */
static int32_t vkil_transfer_buffer(void *component_handle,
				    void *buffer_handle,
				    const vkil_command_t cmd)
{
	int32_t ret, ret1 = 0, msg_id = 0;
	vkil_buffer *buffer = buffer_handle;
	const vkil_context *ilctx = component_handle;
	const vkil_command_t load_mode = cmd & VK_CMD_LOAD_MASK;
	int32_t size = get_vkil2vk_buffer_size(buffer);
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

	ret = vkil_sanity_check_buffer(buffer);
	if (ret)
		goto fail;

	if (!(cmd & VK_CMD_OPT_CB)) {
		/* We need to write the dma command */
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
		if (VKDRV_WR_ERR(ret)) {
			vkil_return_msg_id(ilctx->devctx, message->msg_id);
			goto fail_write;
		}
		msg_id = message->msg_id;

		if ((cmd & VK_CMD_MASK) == VK_CMD_DOWNLOAD) {
			ret = buffer_ref(buffer, -1);
			if (ret)
				goto fail_write;
		}
	}

	if ((cmd & VK_CMD_OPT_BLOCKING) || (cmd & VK_CMD_OPT_CB)) {
		/* we check for the the card response */
		vk2host_msg response;
		int32_t wait = (cmd & VK_CMD_OPT_BLOCKING) ?
				VKIL_READ_TIMEOUT : 0;

		response.function_id  = VK_FID_TRANS_BUF_DONE;
		response.msg_id      = msg_id;
		response.queue_id    = ilctx->context_essential.queue_id;
		response.context_id  = ilctx->context_essential.handle;
		response.size        = 0;
		ret = vkil_read((void *)ilctx->devctx, &response, wait);
		if (VKDRV_RD_ERR(ret))
			goto fail_read;

		ret1 = ret;
		buffer->handle = response.arg;
		ret = vkil_get_msg_user_data(ilctx->devctx, response.msg_id,
					      &buffer->user_data);
		/* we return the message no matter the error status above */
		vkil_return_msg_id(ilctx->devctx, response.msg_id);
		if (ret)
			goto fail_read;


		if ((cmd & VK_CMD_MASK) == VK_CMD_UPLOAD) {
			ret = buffer_ref(buffer, 1);
			if (ret)
				goto fail_read;
		}
	}
	return ret1;

fail_write:
	return fail_write(ret, ilctx);

fail_read:
	return fail_read(ret, ilctx);

fail:
	VKIL_LOG(VK_LOG_ERROR, "failure %d in ilctx %p", ret, ilctx);
	return ret;
};

/**
 * @brief process a buffer
 *
 * the process buffer will typically "consume" the buffer provided in input;
 * that is free the input buffer which will not available anymore; and produce
 * a buffer; malloc a buffer; which can be either conveyed to another,
 * processing, that is calling the same function again; or retrieved to the
 * host, via the vkil_transfer_buffer function.
 *
 * @param component_handle    handle to a vkil_context
 * @param buffer_handle       handle to the buffer to process
 * @param cmd                 options (blocking, call back call,...)
 * @return                    zero on success, error code otherwise
 * @pre the  _vkil_buffer to process must have a valid _vkil_buffer_type
 */
int32_t vkil_process_buffer(void *component_handle,
			    void *buffer_handle,
			    const vkil_command_t cmd)
{
	const vkil_context *ilctx = component_handle;
	vkil_context_internal *ilpriv;
	vkil_buffer *buffer;
	int32_t ret1 = 0, ret = 0;
	int32_t msg_id = 0;
	uint32_t handles[VKIL_MAX_AGGREGATED_BUFFERS];
	uint32_t nbuf, msg_size;

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

	ret = vkil_sanity_check_buffer(buffer);
	if (ret)
		goto fail;

	VK_ASSERT(ilpriv);

	if (!(cmd & VK_CMD_OPT_CB)) {
		host2vk_msg message[VKIL_SEND_MSG_MAX_SIZE];

		get_buffer(buffer_handle, &nbuf, handles);
		/* handles[0] will always be primary buffer(packet/frame) */
		msg_size = MSG_SIZE((nbuf - 1) * sizeof(uint32_t));

		ret = preset_host2vk_msg(message, component_handle,
					 VK_FID_PROC_BUF,
					 buffer->user_data);
		if (ret)
			goto fail_write;

		/* complete message setting */
		message->args[0] = cmd & VK_CMD_MASK;
		message->size    = msg_size;
		memcpy(&message->args[1], handles, nbuf * sizeof(uint32_t));

		ret = vkil_write((void *)ilctx->devctx, message);
		if (VKDRV_WR_ERR(ret)) {
			vkil_return_msg_id(ilctx->devctx,
					   message->msg_id);
			goto fail_write;
		}

		msg_id = message->msg_id;

		ret = buffer_ref(buffer, -1);
		if (ret)
			goto fail_write;
	}

	if ((cmd & VK_CMD_OPT_BLOCKING) || (cmd & VK_CMD_OPT_CB)) {
		/* we check for the the card response */
		vk2host_msg response[VKIL_RET_MSG_MAX_SIZE];
		int32_t wait = (cmd & VK_CMD_OPT_BLOCKING) ?
				VKIL_READ_TIMEOUT : 0;
		uint64_t user_data;

		response->function_id = VK_FID_PROC_BUF_DONE;
		response->msg_id      = msg_id;
		response->queue_id    = ilctx->context_essential.queue_id;
		response->context_id  = ilctx->context_essential.handle;
		response->size        = VKIL_RET_MSG_MAX_SIZE - 1;
		ret = vkil_read((void *)ilctx->devctx, response, wait);
		if (VKDRV_RD_ERR(ret))
			goto fail_read;

		ret1 = ret;

		ret = vkil_get_msg_user_data(ilctx->devctx, response->msg_id,
					      &user_data);
		/* we return the message no matter the error status above */
		vkil_return_msg_id(ilctx->devctx, response->msg_id);
		if (ret)
			goto fail_read;
		ret = set_buffer(buffer, response, user_data, 1);
		if (ret)
			goto fail_read;
	}
	return ret1;

fail_write:
	return fail_write(ret, ilctx);

fail_read:
	return fail_read(ret, ilctx);

fail:
	VKIL_LOG(VK_LOG_ERROR, "failure %d in ilctx %p", ret, ilctx);
	return ret;
};

/**
 * @brief a ref/unref buffer
 *
 * This function add or remove reference to the buffer
 *
 * @param[in] ctx_handle    handle to a vkil_context
 * @param[in] buffer_handle buffer to referecne/dereference
 * @param[in] ref_delta	    number of reference to add, if positive,
 *			    or to remove if negative
 * @param[in] options	    (blocking, call back call,...)
 * @return		    zero on success, error code otherwise
 * @pre the  _vkil_buffer to transfer must have a valid _vkil_buffer_type
 */
int32_t vkil_xref_buffer(void *ctx_handle,
			 void *buffer_handle,
			 const int32_t ref_delta,
			 const vkil_command_t cmd)
{
	int32_t ret, ret1 = 0, msg_id = 0;
	vkil_buffer *buffer = buffer_handle;
	host2vk_msg message[1];

	const vkil_context *ilctx = ctx_handle;

	VKIL_LOG(VK_LOG_DEBUG, "ilctx=%p, buffer=%p, cmd=0x%x (%s%s)",
		 ilctx,
		 buffer_handle,
		 cmd,
		 vkil_cmd_str(cmd),
		 vkil_cmd_opts_str(cmd));
	VK_ASSERT(ctx_handle);
	VK_ASSERT(cmd);

	ret = vkil_sanity_check_buffer(buffer);
	if (ret)
		goto fail;

	if (!(cmd & VK_CMD_OPT_CB)) {
		/* We need to write the dma command */
		ret = preset_host2vk_msg(message,
					 ctx_handle,
					 VK_FID_XREF_BUF,
					 buffer->user_data);
		if (ret)
			goto fail_write;

		/* complete setting */
		message->size = 0;
		message->args[0] = ref_delta;
		message->args[1] = buffer->handle;


		/* then we write the command to the queue */
		ret = vkil_write((void *)ilctx->devctx, message);
		if (VKDRV_WR_ERR(ret)) {
			vkil_return_msg_id(ilctx->devctx, message->msg_id);
			goto fail_write;
		}
		msg_id = message->msg_id;

		if (ref_delta < 0) {
			ret =  buffer_ref(buffer, ref_delta);
			if (ret)
				goto fail_write;
		}
	}

	if ((cmd & VK_CMD_OPT_BLOCKING) || (cmd & VK_CMD_OPT_CB)) {
		/* we check for the the card response */
		vk2host_msg response;
		int32_t wait = (cmd & VK_CMD_OPT_BLOCKING) ?
				VKIL_READ_TIMEOUT : 0;

		response.function_id = VK_FID_TRANS_BUF_DONE;
		response.msg_id = msg_id;
		response.queue_id = ilctx->context_essential.queue_id;
		response.context_id = ilctx->context_essential.handle;
		response.size = 0;
		ret = vkil_read((void *)ilctx->devctx, &response, wait);
		if (VKDRV_RD_ERR(ret))
			goto fail_read;

		ret1 = ret;
		buffer->handle = response.arg;
		ret = vkil_get_msg_user_data(ilctx->devctx, response.msg_id,
					      &buffer->user_data);
		/* we return the message no matter the error status above */
		vkil_return_msg_id(ilctx->devctx, response.msg_id);
		if (ret)
			goto fail_read;

		if (ref_delta > 0) {
			ret =  buffer_ref(buffer, ref_delta);
			if (ret)
				goto fail_read;
		}
	}
	return ret1;

fail_write:
	return fail_write(ret, ilctx);

fail_read:
	return fail_read(ret, ilctx);

fail:
	VKIL_LOG(VK_LOG_ERROR, "failure %d in ilctx %p", ret, ilctx);
	return ret;
};

/**
 * @brief create and initialize a vkil_api
 *
 * The vkil_api provide the intercae to the Valkyrie card.
 * @return an inited vkil_api on success, NULL otherwise
 */
void *vkil_create_api(void)
{
	vkil_api *ilapi;
	int32_t ret;

	ret = vk_logger_init();
	if (ret)
		return NULL;

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
		.xref_buffer           = vkil_xref_buffer,
	};

	return ilapi;
}

/**
 * @brief destroy a vkil_api
 *
 * @param ilapi    handle to a vkil_api
 * @return         zero on success, error code otherwise
 */
int vkil_destroy_api(void **ilapi)
{
	VKIL_LOG(VK_LOG_DEBUG, "");
	vk_logger_deinit();

	vkil_free(ilapi);

	return 0;
}

/**
 * @brief set the device to be used, configured by user CLI
 *
 * @param[in] device    id in ASCII format
 * @return              zero on success, error code otherwise
 */
int vkil_set_affinity(const char *device)
{
	char dev_name[30];

	VKIL_LOG(VK_LOG_DEBUG, "Device %s specified by user.",
		 device ? device : "NULL");

	/* check if device exists or not */
	if (device) {
		if (!snprintf(dev_name, sizeof(dev_name),
			      VKIL_DEV_DRV_NAME ".%s", device))
			return -EINVAL;

		if (access(dev_name, F_OK) != 0) {
			/* Try legacy name */
			snprintf(dev_name, sizeof(dev_name),
				 VKIL_DEV_LEGACY_DRV_NAME ".%s", device);

			if (access(dev_name, F_OK) != 0)
				return -ENODEV;
		}
	}

	vkil_cfg.vkapi_device = device;

	return 0;
}

/**
 * @brief set the processing priority, configured by user CLI
 *
 * @param[in] pri    priority in ASCII format
 * @return           zero on success, error code otherwise
 */
int vkil_set_processing_pri(const char *pri)
{
	static const char * const pri_tab[] = {"high", "med", "low"};
	uint32_t val;

	VKIL_LOG(VK_LOG_DEBUG, "Priority %s specified by user.",
		 pri ? pri : "NULL");

	if (pri) {
		for (val = 0; val < ARRAY_SIZE(pri_tab); val++)
			if (strcmp(pri, pri_tab[val]) == 0)
				break;
		if (val == ARRAY_SIZE(pri_tab))
			return -EINVAL;

		vkil_cfg.vkapi_processing_pri = val;
	}
	return 0;
}

/**
 * @brief set the log level, configured by user CLI
 *
 * @param[in] level  level in ASCII format
 * @return           zero on success, error code otherwise
 */
int vkil_set_log_level(const char *level)
{
	int ret;

	/* simply call backend to set all to same level */
	ret = vk_log_set_level_all(level);

	/*
	 * log only after setting the level, as user wants to see the
	 * below log if he/she set it to dbg
	 */
	VKIL_LOG(VK_LOG_DEBUG, "Log level %s specified by user.", level);

	return ret;
}

/**
 * @brief get the device configured and used by user CLI
 *
 * @return    device id in ASCII format
 */
const char *vkil_get_affinity(void)
{
	VKIL_LOG(VK_LOG_DEBUG, "Return %s chosen by user.",
		 vkil_cfg.vkapi_device ? vkil_cfg.vkapi_device : "NULL");
	return vkil_cfg.vkapi_device;
}

/**
 * @brief get the processing priority configured and used by user CLI
 *
 * @return  processing priority in numeric format
 */
uint32_t vkil_get_processing_pri(void)
{
	VKIL_LOG(VK_LOG_DEBUG, "Return %d chosen by user.",
		 vkil_cfg.vkapi_processing_pri);
	return vkil_cfg.vkapi_processing_pri;
}
