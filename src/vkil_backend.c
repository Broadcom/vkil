// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright(c) 2018 Broadcom
 */

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

/**
 * try to call a function with given parameters and timeout
 * after VK_TIMEOUT_MS
 * @param pointer to function
 * @param handle to the component
 * @param message to process
 * @return zero if success otherwise error message
 */
static ssize_t vkil_wait_probe_msg(ssize_t (*f)(int fd, void *buf,
			size_t nbytes), int fd, void *buf, size_t nbytes)
{
	int32_t ret, i;

	for (i = 0 ; i < VK_TIMEOUT_MS ; i++) {
		ret =  f(fd, buf, nbytes);
		if ((ret > 0) || (ret == (-EMSGSIZE)))
			return ret;
		usleep(1);
	}
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
 * read a message from the device
 * if it is the only node in the list, the list will be deleted
 * @param[in] handle to the device
 * @param[in] where to write the read message
 * @return 0 or read size if success, error code otherwise
 */
ssize_t vkil_wait_probe_read(void *handle, vk2host_msg *message)
{
	int32_t ret;
	vkil_devctx *devctx = handle;

	ret = vkil_wait_probe_msg(vkdrv_read, devctx->fd, message,
			sizeof(vk2host_msg)*(message->size + 1));
	/* TODO, handle the hw status error if any here */
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
	int32_t ret;
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
