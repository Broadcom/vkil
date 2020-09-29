/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright 2018-2020 Broadcom.
 */

/**
 * @file
 * @brief VKIL internally used structures
 *
 * Those structures are not meant to be exposed either to the back or front end
 */

#ifndef VKIL_INTERNAL_H
#define VKIL_INTERNAL_H

#include <stdint.h>
#include <pthread.h>
#include "vkil_utils.h"

/** max number of message queues used shall not be gretaer than VK_MSG_Q_NR */
#define VKIL_MSG_Q_MAX 3

/* name of driver dev node */
#define VKIL_DEV_DRV_NAME		"/dev/bcm_vk"
#define VKIL_DEV_LEGACY_DRV_NAME	"/dev/bcm-vk"

/**
 * Each emitted message is associated to an unique message id, which can as
 * well carries user_data.
 * a command message is always paired to a response message, having then the
 * same _vkil_msg_id (hence the same user data)
 */
typedef struct _vkil_msg_id {
	int16_t used;         /**< indicte a associated intransit message */
	int16_t reserved[3];  /**< byte alignment purpose */
	int64_t user_data;    /**< associated sw data */
} vkil_msg_id;

/**
 * @brief message list context keeping track of all intransit messages
 */
typedef struct _vkil_msgid_ctx {
	vkil_msg_id *msg_list; /**< outgoing message list */
	/**
	 * all writing operation on the list are accessed thru the mwx mutex
	 * (memory barrier)
	 */
	pthread_mutex_t mwx;
} vkil_msgid_ctx;

/**
 * @brief The device context
 */
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

int32_t vkil_write(vkil_devctx * const devctx, host2vk_msg * const msg);
int32_t vkil_read(vkil_devctx * const devctx, vk2host_msg * const msg,
		  const int32_t wait);
int32_t vkil_init_dev(void **handle);
int32_t vkil_deinit_dev(void **handle);

int32_t vkil_get_msg_id(vkil_devctx *devctx);
int32_t vkil_return_msg_id(vkil_devctx *devctx, const int32_t msg_id);

int32_t vkil_set_msg_user_data(vkil_devctx *devctx, const int32_t msg_id,
			       const uint64_t user_data);
int32_t vkil_get_msg_user_data(vkil_devctx *devctx, const int32_t msg_id,
			       uint64_t *user_data);

const char *vkil_function_id_str(uint32_t function_id);
const char *vkil_cmd_str(uint32_t cmd);
const char *vkil_cmd_opts_str(uint32_t cmd);

#endif
