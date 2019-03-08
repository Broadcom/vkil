// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright © 2005-2018 Broadcom. All Rights Reserved.
 * The term “Broadcom” refers to Broadcom Inc. and/or its subsidiaries
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "vkdrv_access.h"
#include "vksim_access.h"
#include "vk_utils.h"

/******************************************************************************
 * local helper functions
 ******************************************************************************/
#define VKDRV_SIM_CTX_MAX           (5 * 32)

typedef struct _vkdrv_sim_ctx {
	bool in_use;
	void *dev;
} vkdrv_sim_ctx;

static vkdrv_sim_ctx ctx[VKDRV_SIM_CTX_MAX];
static bool vkdrv_sim_ctx_init_done;
static pthread_mutex_t vkdrv_sim_mut;

__attribute__((constructor)) void vkdrv_sim_init(void)
{
	pthread_mutexattr_t attr;

	if (!vkdrv_sim_ctx_init_done) {

		memset(ctx, 0, sizeof(vkdrv_sim_ctx));
		pthread_mutexattr_init(&attr);
		pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
		pthread_mutex_init(&vkdrv_sim_mut, &attr);
		vkdrv_sim_ctx_init_done = true;
	}
}

static vkdrv_sim_ctx *vkdrv_sim_getctx(int *fd)
{
	uint32_t i;
	vkdrv_sim_ctx *p_ctx = NULL;

	VK_ASSERT(vkdrv_sim_ctx_init_done == true);

	*fd = -1;
	pthread_mutex_lock(&vkdrv_sim_mut);
	for (i = 0; i < VKDRV_SIM_CTX_MAX; i++) {
		if (!ctx[i].in_use) {
			*fd = i;
			p_ctx = &ctx[i];
			p_ctx->in_use = true;
			break;
		}
	}
	pthread_mutex_unlock(&vkdrv_sim_mut);

	return p_ctx;

}

static int vkdrv_sim_putctx(int fd)
{
	int rc = 0;

	VK_ASSERT(vkdrv_sim_ctx_init_done == true);

	pthread_mutex_lock(&vkdrv_sim_mut);
	ctx[fd].in_use = false;
	ctx[fd].dev = NULL;
	pthread_mutex_unlock(&vkdrv_sim_mut);

	return rc;
}

/******************************************************************************
 * end of local helper functions
 ******************************************************************************/

int vkdrv_sim_open(const char *dev_name, int flags)
{
	int fd;
	vkdrv_sim_ctx *p_ctx = vkdrv_sim_getctx(&fd);

	VKDRV_LOG(VK_LOG_DEBUG, "%s flags 0x%x", dev_name, flags);

	if (p_ctx)
		p_ctx->dev = vksim_init_device(NULL);
	else
		return -1;

	return (p_ctx->dev) ? fd : -1;
};

int vkdrv_sim_close(int fd)
{
	int rc = -1;

	VK_ASSERT((fd >= 0) && (fd < VKDRV_SIM_CTX_MAX));

	if (ctx[fd].in_use) {
		vksim_deinit_device(ctx[fd].dev);
		rc = vkdrv_sim_putctx(fd);
	} else
		VKDRV_LOG(VK_LOG_ERROR, "%s ctx[%d] not in use",
			  __func__, fd);

	return rc;
}

ssize_t vkdrv_sim_write(int fd, const void *buf, size_t nbytes)
{
	vksim_device *device;
	vksim_message *message = (vksim_message *) buf;
	ssize_t ret;

	VK_ASSERT((fd >= 0) && (fd < VKDRV_SIM_CTX_MAX));

	device = ctx[fd].dev;

	VK_ASSERT(ctx[fd].in_use);
	VK_ASSERT(device);
	VK_ASSERT(buf);

	/*
	 * The char driver API pass the message size as function argument,
	 * however, size is part of the message structure to enable the reader
	 * to retrieve it, we do a consistency check, for the matter to avoid
	 * a nasty warning on unused argument.
	 */
	VK_ASSERT(nbytes == (sizeof(vksim_message) * (message->size + 1)));
	VK_ASSERT(device->host2vk_queue);
	VK_ASSERT(message->queue_id < 2);

	ret = device->host2vk_queue[message->queue_id].write(
		&(device->host2vk_queue[message->queue_id]), buf);

	return ret ? ret : (ssize_t) nbytes;
}

ssize_t vkdrv_sim_read(int fd, void *buf, size_t nbytes)
{
	vksim_device *device;
	vksim_message *message = (vksim_message *) buf;
	ssize_t ret;

	VK_ASSERT((fd >= 0) && (fd < VKDRV_SIM_CTX_MAX));

	device = ctx[fd].dev;

	VK_ASSERT(ctx[fd].in_use);
	VK_ASSERT(device);
	VK_ASSERT(buf);

	/*
	 * The char driver API pass the message size as function argument,
	 * however, size is part of the message structure to enable the reader
	 * to retrieve it, we do a consistency check, for the matter to avoid
	 * a nasty warning on unused argument.
	 */
	VK_ASSERT(nbytes == (sizeof(vksim_message) * (message->size + 1)));
	VK_ASSERT(device->vk2host_queue);
	VK_ASSERT(message->queue_id < 2);

	ret = device->vk2host_queue[message->queue_id].read(
		&(device->vk2host_queue[message->queue_id]), buf);

	return ret ? ret : (ssize_t) nbytes;
}
