// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright(c) 2018 Broadcom
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "vkdrv_access.h"
#include "vkil_backend.h"
#include "vkil_error.h"
#include "vkil_internal.h"
#include "vkil_session.h"
#include "vkil_utils.h"

#define BIG_MSG_SIZE_INC   2
/**
 * we allow up to 1 second,
 * long enough to allow for non real time transcoding scheme
 * short enough to bail-out quickly on unresponsive card
 */
#define VKIL_TIMEOUT_US  (1000 * 1000)
/** in the ffmpeg context ms order to magnitude is OK */
#define VKIL_PROBE_INTERVAL_US 1000

/**
 * try to call a function with given parameters and timeout
 * after VK_TIMEOUT_MS
 * @param pointer to function
 * @param handle to the component
 * @param message to process
 * @return zero if success otherwise error message
 */
static ssize_t vkil_wait_probe_msg(int fd, void *buf, const uint32_t wait_x)
{
	int32_t ret, i = 0;
	vk2host_msg *msg = (vk2host_msg *)buf;
	int32_t nbytes = sizeof(vk2host_msg)*(msg->size + 1);

	do {
		ret = vkdrv_read(fd, buf, nbytes);
		if (ret > 0)
			return ret;
#if (VKDRV_KERNEL)
		if ((ret < 0) && (errno == EMSGSIZE))
#else
		/* in sw simulation only we don't use system errno */
		if (ret == (-EMSGSIZE))
#endif
			return (-EMSGSIZE);
		if (!wait_x)
			return (-ENOMSG);
		usleep(VKIL_PROBE_INTERVAL_US);
		i++;
	} while (i < (wait_x * VKIL_TIMEOUT_US) / VKIL_PROBE_INTERVAL_US);
	return (-ETIMEDOUT); /* if we are here we have timed out */
}

/**
 * Write a message to the device
 * if it is the only node in the list, the list will be deleted
 * @param[in] handle to the device
 * @param[in] message to write
 * @return 0 or written size if success, error code otherwise
 */
ssize_t vkil_write(void *handle, host2vk_msg *message)
{
	vkil_devctx *devctx = handle;

	return vkdrv_write(devctx->fd, message,
			sizeof(host2vk_msg)*(message->size + 1));
}

/**
 * Search a message via msg_id matching
 * @param[in] message to look
 * @param[in] reference message
 * @return 0 if matching, error code otherwise
 */
static int32_t search_msg_id(const void *data, const void *data_ref)
{
	const vk2host_msg *msg = data;
	const vk2host_msg *msg_ref = data_ref;

	if (msg->msg_id == msg_ref->msg_id)
		return 0;

	return (-EINVAL);
}

/**
 * Search a message via context_id matching
 * @param[in] message to look
 * @param[in] reference message
 * @return 0 if matching, error code otherwise
 */
static int32_t search_context(const void *data, const void *data_ref)
{
	const vk2host_msg *msg = data;
	const vk2host_msg *msg_ref = data_ref;

	if (msg->context_id == msg_ref->context_id)
		return 0;

	return (-EINVAL);
}

/**
 * extract a message from a linked list
 * if it belong to the only node in the list, the list will be deleted
 * @param[in|out] handle to the linked list
 * @param[in] where to write the read message, field indicate search method
 * @return 0 or read size if success, error code otherwise
 */
static int32_t retrieve_message(vkil_node **pvk2host_ll, vk2host_msg *message)
{
	int32_t ret = 0;
	vk2host_msg *msg;
	vkil_node *node = NULL;
	vkil_node *vk2host_ll = *pvk2host_ll;

	/*
	 * this function need to be called with
	 * pthread_mutex_lock(&devctx->mwx);
	 *
	 * the caller function is here expected to lock/unlock the mutex
	 */

	if (!vk2host_ll) {
		/* the list is empty, no message try later */
		ret = -EAGAIN; /* message is not there yet */
		goto out;
	}

	if (message->msg_id) /* search msg_id */
		node = vkil_ll_search(vk2host_ll, search_msg_id,
				      message);
	else /* no msg_id set, search by context all msg non tagged blocking */
		node = vkil_ll_search(vk2host_ll, search_context,
				      message);

	if (!node) {
		ret = -EAGAIN; /* message is not there yet */
		goto out;
	}

	msg = node->data;
	if (message->size >= msg->size) {
		memcpy(message, msg, sizeof(vk2host_msg) * (msg->size + 1));
		ret = sizeof(vk2host_msg) * (msg->size + 1);
		vkil_ll_delete(pvk2host_ll, node);
		vk_free((void **)&msg);
	} else {
		/* message too long to be copied */
		message->size = msg->size; /* requested size */
		ret = -EMSGSIZE;
		goto out;
	}

out:
	return ret;
}

/**
 * flush the driver reading queue into a SW linked list
 * if it is the only node in the list, the list will be deleted
 * @param[in] handle to the context
 * @param[in] message carrying q_id, and field to retrieve (msg_id,...)
 * @param[in] wait for incoming message carrying specific field  (msg_id,...)
 * @return (-ETIMEDOUT) or (-ENOMSG) on flushing completion
 */
static int32_t vkil_flush_read(void *handle, vk2host_msg *message, int32_t wait)
{
	int32_t ret, q_id;
	vkil_devctx *devctx = handle;
	vk2host_msg *msg;
	vkil_node *node;
	int32_t size;

	/*
	 * this function need to be called with
	 * pthread_mutex_lock(&devctx->mwx);
	 *
	 * the caller function is here expected to lock/unlock the mutex
	 */

	q_id = message->queue_id;
	do {
		/* first exhaust the hw pipe */
		size = 0;
		msg = NULL;
		do {
			if (msg)
				vk_free((void **)&msg);
			ret = vk_mallocz((void **)&msg,
					sizeof(vk2host_msg)*(size + 1));
			if (ret)
				goto fail;
			msg->size = size;
			msg->queue_id = q_id;
			ret = vkil_wait_probe_msg(devctx->fd, msg, wait);

			/* if the message size is too small, the driver
			 * should return the required size in
			 * msg->size field so we run only twice in this loop
			 */
			if (msg->size)
				size =  msg->size;
			else
				/* otherwise increase arbitraily the size */
				size += BIG_MSG_SIZE_INC;
		} while (ret == -EMSGSIZE);

		if (ret >= 0) {
			node = vkil_ll_append(&(devctx->vk2host[q_id]), msg);
			if (!node) {
				vk_free((void **)&msg);
				ret = -ENOMEM;
				goto fail;
			}
			/*
			 * if no message id specified or message id specified
			 * has been retrieved no need to wait any longer
			 */
			if (!message->msg_id ||
			    (message->msg_id == msg->msg_id))
				wait = 0;
		} else
			vk_free((void **)&msg);
	} while (ret >= 0);

	/*
	 * here the return will be negative either -ETIMEOUT or
	 * -ENOMSG, since that is the expected result, we return 0 as success
	 */
	return 0;

fail:
	return ret;
}

/**
 * read a message from the device
 * if it is the only node in the list, the list will be deleted
 * @param[in] handle to the device
 * @param[in] where to write the read message
 * @return 0 or read size if success, error code otherwise
 */
ssize_t vkil_read(void *handle, vk2host_msg *message, int32_t wait)
{
	int32_t ret;
	vkil_devctx *devctx = handle;

	/* sanity check */
	VK_ASSERT(message);
	VK_ASSERT(handle);

	/*
	 * Thought it is expected concurrent driver access are handled
	 * by the driver itself, we still need to prevent concurrent access
	 * to the linked list manipulation
	 */
	pthread_mutex_lock(&devctx->mwx);

	ret = retrieve_message(&devctx->vk2host[message->queue_id], message);

	if (ret != -EAGAIN) {
		/*
		 * either message has been retrieved, or some other problem
		 * has occcured, such has message size bigger than expected
		 */
		pthread_mutex_unlock(&(devctx->mwx));
		return ret;
	}

	ret = vkil_flush_read(handle, message, wait);
	if (ret)
		goto out;

	ret = retrieve_message(&devctx->vk2host[message->queue_id], message);

out:
	pthread_mutex_unlock(&devctx->mwx);
	return ret;
}

/**
 * Denit the device
 * If the caller is the only user, the device is closed
 * @param[in,out] handle to the device
 * @return device id if success, error code otherwise
 */
int32_t vkil_deinit_dev(void **handle)
{
	VKIL_LOG(VK_LOG_DEBUG, "");

	if (*handle) {
		vkil_devctx *devctx = *handle;

		VK_ASSERT(devctx->ref);

		devctx->ref--;
		if (!devctx->ref) {
			vkdrv_close(devctx->fd);
			pthread_mutex_destroy(&devctx->mwx);
			vk_free(handle);
		}
	}
	return 0;
}

/**
 * Init the device
 * open a device if not yet done, otherwise add a reference to
 * existing device
 * @param[in,out] handle to the device
 * @return device id if success, error code otherwise
 */
int32_t vkil_init_dev(void **handle)
{
	vkil_devctx *devctx;
	int32_t ret = -ENODEV;
	char dev_name[30]; /* format: /dev/bcm-vk.x */

	if (!(*handle)) {
		VKIL_LOG(VK_LOG_DEBUG, "init a new device");

		ret = vk_mallocz(handle, sizeof(vkil_devctx));
		if (ret)
			goto fail_malloc;
		devctx = *handle;

		devctx->id = vkil_get_card_id();
		if (devctx->id < 0)
			goto fail;

		if (!snprintf(dev_name, sizeof(dev_name), "/dev/bcm-vk.%d",
			devctx->id))
			goto fail;

		devctx->fd = vkdrv_open(dev_name, O_RDWR);
		if (devctx->fd < 0)
			goto fail;

		pthread_mutex_init(&devctx->mwx, NULL);
	}
	devctx = *handle;
	devctx->ref++;

	VKIL_LOG(VK_LOG_DEBUG, "devctx->fd: %i\n devctx->ref = %i",
				devctx->fd, devctx->ref);
	return devctx->id;

fail:
	vk_free(handle);
fail_malloc:
	VKIL_LOG(VK_LOG_DEBUG, "device context creation failure");
	return ret;
}
