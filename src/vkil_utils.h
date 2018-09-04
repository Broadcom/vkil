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

#ifndef VKIL_UTILS_H
#define VKIL_UTILS_H

#include "vk_utils.h"

#define VKIL_LOG(...) vk_log(__func__, __VA_ARGS__)
#define VKIL_LOG_HOST2VK_MSG(loglevel, msg) VKIL_LOG(loglevel,		\
	"host2vk_msg=%p: function_id=%d, size=%d, queue_id=%d, "	\
	"msg_id=0x%x, context_id=0x%x args[0]=0x%x, args[1]=0x%x",	\
	msg,								\
	((host2vk_msg *)msg)->function_id,				\
	((host2vk_msg *)msg)->size,					\
	((host2vk_msg *)msg)->queue_id,					\
	((host2vk_msg *)msg)->msg_id,					\
	((host2vk_msg *)msg)->context_id,				\
	((host2vk_msg *)msg)->args[0],					\
	((host2vk_msg *)msg)->args[1])

#define VKIL_LOG_VK2HOST_MSG(loglevel, msg) VKIL_LOG(loglevel,		\
	"vk2host_msg=%p: function_id=%d, size=%d, queue_id=%d, "	\
	"msg_id=0x%x, context_id=0x%x hw_status=%d, arg=0%x",		\
	msg,								\
	((vk2host_msg *)msg)->function_id,				\
	((vk2host_msg *)msg)->size,					\
	((vk2host_msg *)msg)->queue_id,					\
	((vk2host_msg *)msg)->msg_id,					\
	((vk2host_msg *)msg)->context_id,				\
	((vk2host_msg *)msg)->hw_status,				\
	((vk2host_msg *)msg)->arg)

vkil_node *vkil_ll_append(vkil_node **head, void *data);
int32_t vkil_ll_delete(vkil_node **head, vkil_node *nd);
vkil_node *vkil_ll_search(vkil_node *head,
			int32_t (*f)(const void *data, const void *data_ref),
			const void *data_ref);
/* debug utilities */
void vkil_ll_log(const uint32_t loglevel, vkil_node *head);

#endif
