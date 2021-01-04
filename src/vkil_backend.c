// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright 2018-2020 Broadcom.
 */

/**
 * @file
 * @brief backend vkil functions
 *
 * This file defines all the functions interfacing with the kernel driver;
 * including the opening and closing of it.
 *
 * It also implements all functions required for proper message handling from
 * the backend viewpoint, that is providing a unique message id, and handling
 * of the message read from the driver into an intermediate linked list
 * the driver read message queue act as a FIFO, but the host need to read
 * messages in "random" order.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>
#include "vkil_api.h"
#include "vkil_backend.h"
#include "vkil_internal.h"
#include "vkil_utils.h"

#ifdef VKDRV_USERMODEL
#include "../drv_model/vkdrv_access.h"
#endif
#define BIG_MSG_SIZE_INC   2
/**
 * we should wait long enough to allow for non real time transcoding scheme
 * short enough to bail-out quickly on unresponsive card
 * if VKIL_TIMEOUT_MS is set to zero, wait can then be infinite.
 */
#define VKIL_TIMEOUT_MS  (50 * 1000)
/** in the ffmpeg context ms order to magnitude is OK */
#define VKIL_PROBE_INTERVAL_MS 1

/*
 * this refers to the maximum number of intransit message into a single context
 * with unique msg_id. The maximum size is (1<<12), since the msg has
 * a 12 bits field to encode the msg_id
 * there is 2 options to generate a unique msg_id, among intransit message:
 * 1/ use a large of digit (counter increment can then just be good enough...
 * it is not?)
 * 2/ use a reasonnably finite number of predefined id, and tag the intransit
 * message with it...this requires the management of a list of id
 * we prefer the the second option, since output of the HW need to be anyway
 * paired with input (for the passing of ancillary field, such as timestamp)
 * and so the mantenance of a list is a given
 */
#define MSG_LIST_SIZE 256

/**
 * @brief set user data for the msg_id
 * (including the HW, with the assigned msg_id)
 *
 * @param[in]  handle to a vkil_devctx
 * @param[in]  msg_id to set
 * @param[in]  user data
 * @return zero if success, error code otherwise
 */
int32_t vkil_set_msg_user_data(vkil_devctx *devctx,
			       const int32_t msg_id,
			       const uint64_t user_data)
{
	vkil_msg_id *msg_list = devctx->msgid_ctx.msg_list;

	VK_ASSERT((msg_id >= 0) && (msg_id < MSG_LIST_SIZE));
	VK_ASSERT(msg_list[msg_id].used);

	msg_list[msg_id].user_data = user_data;
	return 0;
}

/**
 * @brief get user data froma message id
 * (including the HW, with the assigned msg_id
 *
 * @param[in]  devctx handle to a device context
 * @param[in]  msg_id id of the message to retrieve
 * @param[in,out] retrieved user data
 * @return zero if success, error code otherwise
 */
int32_t vkil_get_msg_user_data(vkil_devctx *devctx,
			       const int32_t msg_id,
			       uint64_t *user_data)
{
	vkil_msg_id *msg_list = devctx->msgid_ctx.msg_list;

	VK_ASSERT((msg_id >= 0) && (msg_id < MSG_LIST_SIZE));
	VK_ASSERT(msg_list[msg_id].used);

	*user_data = msg_list[msg_id].user_data;

	return 0;
}

/**
 * @brief Recycle a message id, indicate there is no more message in the
 * system; including the HW; with the assigned msg_id
 *
 * @param  devctx device context
 * @param  msg_id id to recycle
 * @return zero if success, error code otherwise
 */
int32_t vkil_return_msg_id(vkil_devctx *devctx, const int32_t msg_id)
{
	vkil_msg_id *msg_list = devctx->msgid_ctx.msg_list;

	VK_ASSERT((msg_id >= 0) && (msg_id < MSG_LIST_SIZE));

	VK_ASSERT(msg_list[msg_id].used);

	msg_list[msg_id].used = 0;
	return 0;
}

/**
 * @brief Get a unique message id
 *
 * @param  devctx device context
 * @return an unique msg_id if positive, error code otherwise
 */
int32_t vkil_get_msg_id(vkil_devctx *devctx)
{
	int32_t i, ret;
	vkil_msg_id *msg_list;

	VK_ASSERT(devctx && devctx->msgid_ctx.msg_list);

	msg_list = devctx->msgid_ctx.msg_list;
	ret = pthread_mutex_lock(&(devctx->msgid_ctx.mwx));
	if (ret)
		goto fail;

	/* msg_id zero is reserved */
	for (i = 1; i < MSG_LIST_SIZE; i++) {
		if (!msg_list[i].used) {
			msg_list[i].used = 1;
			break;
		}
	}
	ret = pthread_mutex_unlock(&(devctx->msgid_ctx.mwx));
	if (ret)
		goto fail;

	if (i >= MSG_LIST_SIZE) {
		ret = ENOBUFS;
		goto fail;
	}

	return i;

fail:
	VKIL_ERR(ret, "in devctx %p", devctx);
	return -ret; /* we always return negative error code if error */
}

/**
 * @brief De-initialize a message list
 *
 * @param  devctx device context
 * @return zero on succes, error code otherwise
 */
static int32_t vkil_deinit_msglist(vkil_devctx *devctx)
{
	int32_t ret;

	vkil_free((void **)&devctx->msgid_ctx.msg_list);
	ret = pthread_mutex_destroy(&(devctx->msgid_ctx.mwx));
	if (ret)
		goto fail;

	return 0;

fail:
	VKIL_ERR(ret, "");
	return -EPERM;
}

/**
 * @brief initializes a message list
 *
 * @param  devctx device context
 * @return zero on succes, error code otherwise
 */
static int32_t vkil_init_msglist(vkil_devctx *devctx)
{
	int32_t ret;

	ret = vkil_mallocz((void **)&devctx->msgid_ctx.msg_list,
			   sizeof(vkil_msg_id) * MSG_LIST_SIZE);
	if (ret)
		goto fail;

	ret = pthread_mutex_init(&(devctx->msgid_ctx.mwx), NULL);
	if (ret)
		goto fail;

	return 0;

fail:
	vkil_deinit_msglist(devctx);
	VKIL_ERR(ret, "");
	return -abs(ret);
}

static int64_t vkil_get_time_us(void)
{
	struct timespec time_i;
	int64_t sec;
	int64_t res;
	int overflow;

	clock_gettime(CLOCK_MONOTONIC, &time_i);
	/* CERT-C INT32-C compliance */
	overflow = __builtin_smull_overflow(time_i.tv_sec, 1000000, &sec);
	VK_ASSERT(!overflow);
	/* CERT-C INT32-C compliance */
	overflow = __builtin_saddl_overflow(sec, time_i.tv_nsec / 1000, &res);
	VK_ASSERT(!overflow);
	return res;
}

/**
 * @brief probe a driver for message up to VKIL_TIMEOUT_MS * wait_x
 * @param[in] driver to poll
 * @param[in|out] message returned on success
 * @param[in] max wait factor (=default_wait*wait_x, zero means no wait)
 * @return positive if success otherwise error message
 */
static ssize_t vkil_wait_probe_msg(int fd,
				   vk2host_msg *msg,
				   const int32_t wait_x)
{
	int32_t ret, nbytes;
	struct pollfd fds;
	int64_t start_us, time_us, end_us, wait;
	int overflow;

	VK_ASSERT(msg);
	VK_ASSERT(msg->size < UINT8_MAX);
	VK_ASSERT(__errno_location());

	nbytes = sizeof(*msg) * (msg->size + 1);

	fds.fd = fd;
	fds.events = POLLIN | POLLRDNORM;
	fds.revents = 0;

	start_us = vkil_get_time_us();
	time_us = start_us;

	/* CERT-C INT32-C compliance */
	overflow = __builtin_smull_overflow(wait_x, VKIL_TIMEOUT_MS * 1000, &wait);
	VK_ASSERT(!overflow);

	/* CERT-C INT32-C compliance */
	overflow = __builtin_saddl_overflow(start_us, wait, &end_us);
	VK_ASSERT(!overflow);

	while (1) {
		if (wait_x) {
#if VKIL_TIMEOUT_MS
			int64_t del_time_us;
			int64_t time_ms;

			overflow = __builtin_ssubl_overflow(end_us, time_us, &del_time_us);
			VK_ASSERT(!overflow);

			time_ms = del_time_us / 1000;
			if (time_ms <= 0)
				break;
			ret = poll(&fds, 1, time_ms);
#else
			/* Infinite wait */
			ret = poll(&fds, 1, 0);
#endif
		} else {
			ret = 1;
		}

		if (ret) {
			ret = read(fd, msg, nbytes);
			if (ret > 0) {
				/*
				 * If ret < nbytes, we have only partially read the message
				 * and there is no way for calling code to recover.
				 */
				VK_ASSERT(ret == nbytes);
				return ret;
			}

#ifdef VKDRV_USERMODEL
			/* in sw simulation only we don't use system errno */
			if (ret == -EMSGSIZE)
#else
			if ((ret < 0) && (errno == EMSGSIZE))
#endif
				return -EMSGSIZE;
			if (!wait_x)
				return -ENOMSG;
		}

		time_us = vkil_get_time_us();
	}

	VKIL_LOG(VK_LOG_WARNING, "Hit timeout %d ms", wait_x * VKIL_TIMEOUT_MS);
	return -ETIMEDOUT; /* if we are here we have timed out */
}

/**
 * @brief write a message to the device
 * @param devctx device context
 * @param message to write
 * @return 0 on success, error code otherwise
 */
int32_t vkil_write(vkil_devctx * const devctx, host2vk_msg * const msg)
{
	ssize_t ret;

	VK_ASSERT(__errno_location());

	errno = 0; /* statement to comply with CERT-C:2012 rule ERR30-C */
	ret = write(devctx->fd, msg, sizeof(*msg) * (msg->size + 1));
	/* on error, driver returns -1 and the error is stored in errno */
	if (ret < 0)
		return -errno;

	return 0;
}

/**
 * @brief compare vk2host_msg::msg_id
 * @param first message to compare
 * @param second message to compare
 * @return 0 if matching, error code otherwise
 */
static int32_t cmp_msg_id(const void *data, const void *data_ref)
{
	const vk2host_msg *msg = data;
	const vk2host_msg *msg_ref = data_ref;

	if (msg->msg_id == msg_ref->msg_id)
		return 0;

	return -EINVAL;
}

/**
 * @brief compare vk2host_msg::function_id
 * @param first message to compare
 * @param second message to compare
 * @return 0 if matching, error code otherwise
 */
static int32_t cmp_function(const void *data, const void *data_ref)
{
	const vk2host_msg *msg = data;
	const vk2host_msg *msg_ref = data_ref;

	/*
	 * checking order doesn't matter but for efficiency we check from the
	 * least probable to the most
	 */
	if (msg->function_id == msg_ref->function_id)
		/* The message need to belong to the same context */
		if (msg->context_id == msg_ref->context_id)
			return 0;

	return -EINVAL;
}

/**
 * @brief retrieve a message from a linked list
 *
 * if it belongs to the only node in the list, the list will be deleted
 * @param[in|out] handle to the linked list
 * @param[in] where to write the read message,
 *	@li if a vk2host_msg::msg_id is provided, will extract only a message
 *	    matching the provided vk2host_msg::msg_id
 *	@li otherwise return message matching the provided
 *	    vk2host_msg::function_id
 * @return 0 if success, error code otherwise
 */
static int32_t retrieve_message(vkil_node **pvk2host_ll, vk2host_msg *message)
{
	size_t msglen;
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
		node = vkil_ll_search(vk2host_ll, cmp_msg_id, message);
	else /* no msg_id set, search by function all msg non tagged blocking */
		node = vkil_ll_search(vk2host_ll, cmp_function, message);

	if (!node) {
		ret = -EAGAIN; /* message is not there yet */
		goto out;
	}

	msg = node->data;
	if (message->size >= msg->size) {
		msglen = sizeof(*msg) * (msg->size + 1);
		memcpy(message, msg, msglen);
		vkil_ll_delete(pvk2host_ll, node);
		vkil_free((void **)&msg);
	} else {
		/* message too long to be copied */
		message->size = msg->size; /* requested size */
		ret = -EMSGSIZE;
		goto out;
	}

	if (message->hw_status == VK_STATE_ERROR) {
		/* memcpy to avoid undefined behavior involved by cast */
		if (message->arg)
			memcpy(&ret, &message->arg, sizeof(ret));
		else
			/* default error to prevent to log success */
			ret = -EADV;
		VKIL_ERR(ret, "VK_STATE_ERROR");
		VKIL_LOG_VK2HOST_MSG(VK_LOG_DEBUG, message);
		ret = -EADV;
	}

out:
	return ret;
}

/**
 * @brief flush the driver reading queue into a SW linked list
 * @param[in] devctx device context
 * @param[in] message carrying q_id, and field to retrieve (msg_id,...)
 * @param[in] wait for incoming message carrying specific field  (msg_id,...)
 * @return (-ETIMEDOUT) or (-ENOMSG) on flushing completion
 */
static int32_t vkil_flush_read(vkil_devctx *devctx,
			       vk2host_msg *message,
			       int32_t wait)
{
	int32_t ret, q_id;
	vk2host_msg *msg;
	vkil_node *node;
	uint8_t size;

	/*
	 * this function need to be called with
	 * pthread_mutex_lock(&devctx->mwx);
	 *
	 * the caller function is here expected to lock/unlock the mutex
	 */

	q_id = message->queue_id;
	if (q_id >= VKIL_MSG_Q_MAX) {
		VKIL_LOG(VK_LOG_ERROR, "q_id %d > MAX %d in devctx %p",
			 q_id, VKIL_MSG_Q_MAX, devctx);
		ret = -EINVAL;
		goto fail;
	}

	do {
		/* first exhaust the hw pipe */
		size = 0;
		msg = NULL;
		do {
			if (msg)
				vkil_free((void **)&msg);

			ret = vkil_mallocz((void **)&msg,
					   sizeof(*msg) * (size + 1));
			if (ret)
				goto fail_malloc;
			msg->size = size;
			msg->queue_id = q_id;
			ret = vkil_wait_probe_msg(devctx->fd, msg, wait);
			if (ret == -ETIMEDOUT)
				goto fail;

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
			if (msg->queue_id >= VKIL_MSG_Q_MAX) {
				VKIL_LOG(VK_LOG_ERROR,
					 "Received message with q_id %d > MAX %d in devctx %p",
					 msg->queue_id, VKIL_MSG_Q_MAX, devctx);
				ret = -EINVAL;
				goto fail;
			}
			node = vkil_ll_append(&devctx->vk2host[msg->queue_id], msg);
			if (!node) {
				ret = -ENOMEM;
				goto fail;
			}
			/*
			 * if no message id specified or message is unpaired,
			 * compare based on function id
			 */
			if (!message->msg_id ||
			    message->msg_id == VK_UNPAIRED_MSG_ID) {
				int32_t still_wait = cmp_function(msg, message);

				wait = still_wait ? wait : 0;
			} else if (message->msg_id == msg->msg_id) {
				wait = 0;
			}
		} else
			vkil_free((void **)&msg);
	} while (ret >= 0);

	/*
	 * here the return will be negative -ENOMSG, since that is the expected
	 * result, we return 0 as success
	 */
	return 0;

fail:
	vkil_free((void **)&msg);
fail_malloc:
	return ret;
}

/**
 * @brief read a message from the device
 *
 * the function will first look if the message has been transferred from the
 * driver to the a SW linked list, if not, then it will flush the driver queue
 * into the vkil backend linkd list; then poll the SW linked list again
 * @param[in] devctx device context
 * @param[in|out] returned message
 * @return 0 on success, -EADV if read message report an error, other errors
 *           code otherwise
 */
int32_t vkil_read(vkil_devctx * const devctx, vk2host_msg * const msg,
		  const int32_t wait)
{
	int32_t ret, retm;

	/* sanity check */
	VK_ASSERT(msg);
	VK_ASSERT(devctx);
	VK_ASSERT(msg->queue_id < VKIL_MSG_Q_MAX);

	/*
	 * Thought it is expected concurrent driver access are handled
	 * by the driver itself, we still need to prevent concurrent access
	 * to the linked list manipulation
	 */
	retm = pthread_mutex_lock(&devctx->mwx);
	if (retm) {
		VKIL_ERR(retm, "mutex lock error in devctx %p", devctx);
		return -retm; /* force negative error */
	}

	ret = retrieve_message(&devctx->vk2host[msg->queue_id], msg);

	if (ret != -EAGAIN) {
		/*
		 * either message has been retrieved, or some other problem
		 * has occcured, such has message size bigger than expected
		 */
		VKIL_LOG_VK2HOST_MSG(VK_LOG_DEBUG, msg);
		retm = pthread_mutex_unlock(&(devctx->mwx));
		if (retm) {
			/* mutex error takes precedence on other error */
			VKIL_ERR(retm,
				 "mutex unlock error in devctx %p", devctx);
			ret = -retm;
		}
		return ret;
	}

	ret = vkil_flush_read(devctx, msg, wait);
	if (ret)
		goto out;

	ret = retrieve_message(&devctx->vk2host[msg->queue_id], msg);

	if (ret != -EAGAIN)
		VKIL_LOG_VK2HOST_MSG(VK_LOG_DEBUG, msg);
	else
		VKIL_LOG(VK_LOG_DEBUG, "message not retrieved yet");

out:
	retm = pthread_mutex_unlock(&devctx->mwx);
	if (retm) {
		/* mutex error takes precedence on other error */
		VKIL_ERR(retm, "mutex unlock error in devctx %p", devctx);
		ret = -retm;
	}
	return ret;
}

void vkil_deinit_node_list(vkil_node *ptr)
{
	vkil_node *nxt;

	while (ptr) {
		nxt = ptr->next;
		if (ptr->data)
			vkil_free((void **)&ptr->data);
		vkil_free((void **)&ptr);
		ptr = nxt;
	}
}

/**
 * @brief denit the device
 *
 * If the caller is the only user, the device is closed
 * @param[in,out] handle handle to the device
 * @return device id if success, error code otherwise
 */
int32_t vkil_deinit_dev(void **handle)
{
	int i;
	VKIL_LOG(VK_LOG_DEBUG, "");

	if (*handle) {
		vkil_devctx *devctx = *handle;

		VK_ASSERT(devctx->ref);

		devctx->ref--;
		if (!devctx->ref) {
			VKIL_LOG(VK_LOG_DEBUG, "close driver");
			vkil_deinit_msglist(devctx);
			close(devctx->fd);
			pthread_mutex_destroy(&devctx->mwx);
			for (i = 0; i < VKIL_MSG_Q_MAX; i++) {
				vkil_deinit_node_list(devctx->vk2host[i]);
				devctx->vk2host[i] = NULL;
			}
			vkil_free(handle);
		}
	}
	return 0;
}

/**
 * @brief init the device
 *
 * open a device if not yet done, otherwise add a reference to
 * existing device
 * @param[in,out] handle handle to the device
 * @return device id if positive, error code otherwise
 */
int32_t vkil_init_dev(void **handle)
{
	vkil_devctx *devctx;
	int32_t ret;
	char dev_name[30]; /* format: /dev/bcm-vk.x */
	const char *p_aff_dev;

	if (!(*handle)) {
		VKIL_LOG(VK_LOG_DEBUG, "init a new device");

		ret = vkil_mallocz(handle, sizeof(*devctx));
		if (ret)
			goto fail;

		devctx = *handle;
		devctx->ref++;
		ret = -ENODEV; /* value to be used for below fails */

		p_aff_dev = vkil_get_affinity();
		devctx->id = p_aff_dev ? atoi(p_aff_dev) : 0;
		if (devctx->id < 0)
			goto fail;

		if (!snprintf(dev_name, sizeof(dev_name),
			      VKIL_DEV_DRV_NAME ".%d", devctx->id))
			goto fail;

		devctx->fd = open(dev_name, O_RDWR);
		if (devctx->fd < 0) {
			/* Try legacy name */
			snprintf(dev_name, sizeof(dev_name),
				 VKIL_DEV_LEGACY_DRV_NAME ".%d", devctx->id);

			devctx->fd = open(dev_name, O_RDWR);
			if (devctx->fd < 0)
				goto fail;
		}

		ret = vkil_init_msglist(devctx);
		if (ret)
			goto fail;

		ret = pthread_mutex_init(&devctx->mwx, NULL);
		if (ret) {
			ret = -ret; /* error are forced to be negative */
			goto fail;
		}
	} else {
		devctx = *handle;
		devctx->ref++;
	}

	VKIL_LOG(VK_LOG_DEBUG, "devctx->fd: %i\n devctx->ref = %i",
				devctx->fd, devctx->ref);
	return devctx->id;

fail:
	vkil_deinit_dev(handle);
	VKIL_ERR(ret, "device initialization failure");
	return ret;
}
