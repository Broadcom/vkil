/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright(c) 2018 Broadcom
 */

#ifndef VKIL_INTERNAL_H
#define VKIL_INTERNAL_H

#include <stdint.h>
#include <pthread.h>

typedef struct _vkil_msg_id {
	int16_t used;         /**< indicte a associated intransit message */
	int16_t reserved[3];  /**< byte alignment purpose */
	int64_t user_data;    /**< associated sw data */
} vkil_msg_id;

typedef struct _vkil_devctx {
	int fd;      /**< driver */
	int32_t ref; /**< number of vkilctx instance using the device */
	int32_t id;  /**< card id */
} vkil_devctx;

typedef struct _vkil_context_internal {
	vkil_msg_id *msg_list; /**< outgoing message list */
	/**
	 * all writing operation on the list are accessed thru the mwx mutex
	 * (memory barrier)
	 */
	pthread_mutex_t mwx;
} vkil_context_internal;

ssize_t vkil_write(void *handle, host2vk_msg *message);
ssize_t vkil_wait_probe_read(void *handle, vk2host_msg *message);
int32_t vkil_init_dev(void **handle);
int32_t vkil_deinit_dev(void **handle);

#endif
