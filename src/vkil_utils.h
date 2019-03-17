/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright(c) 2018 Broadcom
 */

/**
 * @file
 * @brief vkil utilities functions declaration
 */

#ifndef VKIL_UTILS_H
#define VKIL_UTILS_H

#include "vk_utils.h"

#define VKIL_LOG(...) vk_log(__func__, VK_LOG_MOD_SYS, LOG_TYPE_INT,	\
			     __VA_ARGS__)

#define VKIL_LOG_HOST2VK_MSG(loglevel, msg)				\
		VK_H2VK_LOG(VKIL_LOG, loglevel, msg)

#define VKIL_LOG_VK2HOST_MSG(loglevel, msg)				\
		VK_VK2H_LOG(VKIL_LOG, loglevel, msg)

int vkil_malloc(void **ptr, size_t size);
int vkil_mallocz(void **ptr, size_t size);
void vkil_free(void **ptr);

vkil_node *vkil_ll_append(vkil_node **head, void *data);
int32_t vkil_ll_delete(vkil_node **head, vkil_node *nd);
vkil_node *vkil_ll_search(vkil_node *head,
			int32_t (*f)(const void *data, const void *data_ref),
			const void *data_ref);
/* debug utilities */
void vkil_ll_log(const uint32_t loglevel, vkil_node *head);

#endif
