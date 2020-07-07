/* SPDX-License-Identifier: GPL-2.0 OR Apache-2.0 */
/*
 * Copyright(c) 2020 Broadcom
 */

#ifndef VK_COMMON_H
#define VK_COMMON_H

#include <assert.h>
#include <stdlib.h>
#include "vk_logger.h"

#ifdef ZEPHYR_BUILD
/** normal process termination on a zephyr __ASSERT */
#define VK_EXIT __ASSERT_POST
#else
/** normal process termination on a linux assert */
#define VK_EXIT abort()
#endif

/* max filename length excluding null */
#define VK_ASSERT_FNAME_MAX 96

#define VK_ASSERT(cond) do {						   \
	if (!(cond)) {							   \
		char fname[128];					   \
		int val = snprintf(fname, sizeof(fname), "%s", __FILE__);  \
									   \
		if (val >= sizeof(fname))				   \
			val = sizeof(fname) - 1;			   \
		val = (val >= VK_ASSERT_FNAME_MAX) ?			   \
			      val - VK_ASSERT_FNAME_MAX : 0;		   \
		vk_log(__func__, VK_LOG_MOD_SYS, LOG_TYPE_INT,		   \
		       VK_LOG_PANIC,					   \
		       " %s:%d, assert %s failed",			   \
		       &fname[val], __LINE__, #cond);			   \
		VK_EXIT;						   \
	}								   \
} while (0)

#ifdef DEBUG
#define VK_ASSERT_DBG(cond) VK_ASSERT(cond)
#else
#define VK_ASSERT_DBG(cond)
#endif

#endif
