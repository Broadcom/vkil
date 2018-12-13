/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright(c) 2018 Broadcom
 */

#ifndef VKIL_INTERNAL_H
#define VKIL_INTERNAL_H

#include <stdint.h>
#include <pthread.h>

/** max number of message queue used shall not be gretaer than VK_MSG_Q_NR */
#define VKIL_MSG_Q_MAX 3

typedef struct _vkil_node {
	void *data;
	struct _vkil_node *next;
} vkil_node;

typedef struct _vkil_msg_id {
	int16_t used;         /**< indicte a associated intransit message */
	int16_t reserved[3];  /**< byte alignment purpose */
	int64_t user_data;    /**< associated sw data */
} vkil_msg_id;

typedef struct _vkil_msgid_ctx {
	vkil_msg_id *msg_list; /**< outgoing message list */
	/**
	 * all writing operation on the list are accessed thru the mwx mutex
	 * (memory barrier)
	 */
	pthread_mutex_t mwx;
} vkil_msgid_ctx;

typedef struct _vkil_devctx {
	int fd;      /**< driver */
	int32_t ref; /**< number of vkilctx instance using the device */
	int32_t id;  /**< card id */
	vkil_node *vk2host[VKIL_MSG_Q_MAX]; /**< dequeued messages */
	pthread_mutex_t mwx; /** protect concurrent access to the msg queue */
	vkil_msgid_ctx msgid_ctx;
} vkil_devctx;

typedef struct _vkil_context_internal {
	int32_t reserved;
} vkil_context_internal;


ssize_t vkil_write(vkil_devctx *devctx, host2vk_msg *message);
ssize_t vkil_read(vkil_devctx  *devctx, vk2host_msg *message, int32_t wait);
int32_t vkil_init_dev(void **handle);
int32_t vkil_deinit_dev(void **handle);

int32_t vkil_get_msg_id(vkil_devctx *devctx);
int32_t vkil_return_msg_id(vkil_devctx *devctx, const int32_t msg_id);

int32_t vkil_set_msg_user_data(vkil_devctx *devctx, const int32_t msg_id,
			       const uint64_t user_data);
int32_t vkil_get_msg_user_data(vkil_devctx *devctx, const int32_t msg_id,
			       uint64_t *user_data);

#endif
