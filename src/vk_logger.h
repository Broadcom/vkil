/* SPDX-License-Identifier: GPL-2.0 OR Apache-2.0 */
/*
 * Copyright(c) 2018-2019 Broadcom
 */

#ifndef VK_LOGGER_H
#define VK_LOGGER_H

#include <stdint.h>
#include "logger_api.h"

typedef enum _vk_log_mod {
	VK_LOG_MOD_GEN = 0,  /** generic, non-mutable */
	VK_LOG_MOD_INF,      /** info */
	VK_LOG_MOD_ENC,      /** encoder */
	VK_LOG_MOD_DEC,      /** decoder */
	VK_LOG_MOD_DMA,      /** dma */
	VK_LOG_MOD_SCL,      /** scaler */
	VK_LOG_MOD_DRV,      /** driver */
	VK_LOG_MOD_SYS,      /** infra structures, mem, queue etc */
	VK_LOG_MOD_MVE,      /** mali firmware */
	VK_LOG_MOD_MAX,
} vk_log_mod;

/* 1-to-1 mapping of log levels */
typedef enum _vk_log_level {
	VK_LOG_PANIC   = LOG_PANIC,
	VK_LOG_ERROR   = LOG_ERROR,
	VK_LOG_WARNING = LOG_WARNING,
	VK_LOG_INFO    = LOG_INFO,
	VK_LOG_DEBUG   = LOG_DEBUG,
} vk_log_level;

/* logger related APIs */
int32_t vk_logger_init(void);
int32_t vk_logger_deinit(void);
void vk_log(const char *prefix, vk_log_mod log_mod, log_type ltype,
	    log_level level,
	    const char *fmt, ...);

#define VK_H2VK_LOG(logger, loglevel, msg) logger(loglevel,                \
	"host2vk_msg=%x: function_id=%d(%s), size=%d, queue_id=%d, "       \
	"msg_id=%x, context_id=%x args[0]=%x, args[1]=%x",                 \
	msg,                                                               \
	((host2vk_msg *)msg)->function_id,                                 \
	vkil_function_id_str(((host2vk_msg *)msg)->function_id),           \
	((host2vk_msg *)msg)->size,                                        \
	((host2vk_msg *)msg)->queue_id,                                    \
	((host2vk_msg *)msg)->msg_id,                                      \
	((host2vk_msg *)msg)->context_id,                                  \
	((host2vk_msg *)msg)->args[0],                                     \
	((host2vk_msg *)msg)->args[1])

#define VK_VK2H_LOG(logger, loglevel, msg) logger(loglevel,                \
	"vk2host_msg=%x: function_id=%d(%s), size=%d, queue_id=%d, "       \
	"msg_id=%x, context_id=%x hw_status=%d, arg=%x",                   \
	msg,                                                               \
	((vk2host_msg *)msg)->function_id,                                 \
	vkil_function_id_str(((vk2host_msg *)msg)->function_id),           \
	((vk2host_msg *)msg)->size,                                        \
	((vk2host_msg *)msg)->queue_id,                                    \
	((vk2host_msg *)msg)->msg_id,                                      \
	((vk2host_msg *)msg)->context_id,                                  \
	((vk2host_msg *)msg)->hw_status,                                   \
	((vk2host_msg *)msg)->arg)

#endif
