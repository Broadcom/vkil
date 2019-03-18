/* SPDX-License-Identifier: GPL-2.0 OR Apache-2.0 */
/*
 * Copyright(c) 2018 Broadcom
 */

#ifndef VK_UTILS_H
#define VK_UTILS_H

#include <assert.h>
#include <stdlib.h>
#include "vk_logger.h"

#define VK_ALIGN 16

#ifdef ZEPHYR_BUILD
#include <misc/util.h>

/** normal process termination on a zephyr __ASSERT */
#define VK_EXIT __ASSERT_POST

#else
/** normal process termination on a linux assert */
#define VK_EXIT abort()
#endif

#define VK_ASSERT(cond) do {                                               \
	if (!(cond)) {                                                     \
		vk_log(__func__, VK_LOG_MOD_SYS, LOG_TYPE_INT,             \
		       VK_LOG_PANIC,                                       \
		       " %s:%d, assert %s failed",                         \
			__FILE__, __LINE__, #cond);                        \
		VK_EXIT;                                                   \
	}                                                                  \
} while (0)

#ifndef MIN
#define MIN(a, b) (((a) < (b))?(a):(b))
#endif

#ifndef MAX
#define MAX(a, b) (((a) > (b))?(a):(b))
#endif

#ifndef ARRAY_SIZE
/* this is defined in kernel, but is not expected to be defined here */
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*(a)))
#endif

#endif
