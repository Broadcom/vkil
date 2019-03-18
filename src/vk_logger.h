/* SPDX-License-Identifier: GPL-2.0 OR Apache-2.0 */
/*
 * Copyright(c) 2018-2019 Broadcom
 */

#ifndef VK_LOGGER_H
#define VK_LOGGER_H

#include <stdint.h>

/**
 * logger control structure
 * This allows user to pass in its definition of control structure
 */
typedef struct _logger_ctrl {
	uint32_t log_level;
	const char *tag;
} logger_ctrl;

typedef enum _log_level {
	/** something went very wrong, and we gonna crash (assert)*/
	LOG_PANIC       = 0,
	/** something went wrong, error handled to function caller */
	LOG_ERROR       = 16,
	/** unexpected behavior, not compromising the program execution */
	LOG_WARNING     = 32,
	/** standard information */
	LOG_INFO        = 64,
	/** debugging info */
	LOG_DEBUG       = 128
} log_level;

/**
 * @brief Type of logging which specifies parameters' size
 *
 * LOG_TYPE_INT: The default one is using INT as the passing parameter.
 * LOG_TYPE_64B: 64 bit parameter
 * LOG_TYPE_UL: unsigned long
 *
 * In general, all the parameters using the logging macro has to be same size
 * as we will not decode the list at run time.	This is restriciton one, which
 * implies that for using the macro, we may need to put a cast to the type.
 *
 * The LOG_TYPE_UL is used for arch when it is running in 64bit mode, and if its
 * arg list is a mixed of %p %d etc.  In this case, eg.x86-64, pointer will
 * be 64 bits while the data is 32.  All the 32-bit data should be casted
 * to unsigned long so that the compiler will form the proper parameter list
 * on stack. This is more for future.  Note that on 32bit machine, unsigned long
 * is 32-bit and all pointers are 32bits, so using type INT would be OK.
 */
typedef enum _log_type {
	LOG_TYPE_INT      = 0,
	LOG_TYPE_ULL      = 1,
	LOG_TYPE_UL       = 2,
} log_type;

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
