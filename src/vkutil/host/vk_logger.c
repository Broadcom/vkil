// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright 2018-2020 Broadcom.
 */

/**
 * @file
 * @brief valkyrie host logger functions
 *
 * This is an inline logger that is used under the host
 * environment, FFMPEG using vkil or pure simulation mode.
 * Since all these modes are x86, it bears to have lots of
 * time for printing and it is OK to do it inline.
 */

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "vk_logger.h"
#include "vkil_utils.h"

#define VK_LOG_DEF_LEVEL VK_LOG_INFO

static logger_ctrl vk_log_ctrl[VK_LOG_MOD_MAX] = {
	[VK_LOG_MOD_GEN] = { VK_LOG_DEBUG,     ""    },
	[VK_LOG_MOD_INF] = { VK_LOG_DEF_LEVEL, "inf" },
	[VK_LOG_MOD_ENC] = { VK_LOG_DEF_LEVEL, "enc" },
	[VK_LOG_MOD_DEC] = { VK_LOG_DEF_LEVEL, "dec" },
	[VK_LOG_MOD_DMA] = { VK_LOG_DEF_LEVEL, "dma" },
	[VK_LOG_MOD_SCL] = { VK_LOG_DEF_LEVEL, "scl" },
	[VK_LOG_MOD_MPS] = { VK_LOG_DEF_LEVEL, "mps" },
	[VK_LOG_MOD_DRV] = { VK_LOG_DEF_LEVEL, "drv" },
	[VK_LOG_MOD_SYS] = { VK_LOG_DEF_LEVEL, "sys" },
	[VK_LOG_MOD_MVE] = { VK_LOG_DEF_LEVEL, "mve" },
	[VK_LOG_MOD_FWE] = { VK_LOG_DEF_LEVEL, "fwe" },
};

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief set all log modules to a specific log level
 *
 * @param level set level in ascii format
 * @return 0 on success, negative on error
 */
int32_t vk_log_set_level_all(const char *level)
{
	uint32_t i, j;
	/* levels supported */
	static const struct _log_lev_tab {
		char  *tag;
		vk_log_level lev;
	} _tab[] = {
		{"panic", VK_LOG_PANIC   },
		{"err",   VK_LOG_ERROR   },
		{"warn",  VK_LOG_WARNING },
		{"info",  VK_LOG_INFO    },
		{"dbg",   VK_LOG_DEBUG   }
	};

	for (i = 0; i < ARRAY_SIZE(_tab); i++) {
		if (strcmp(_tab[i].tag, level) == 0) {

			for (j = 0; j < ARRAY_SIZE(vk_log_ctrl); j++)
				vk_log_ctrl[j].log_level = _tab[i].lev;
			return 0;
		}
	}

	return -EINVAL;
}

/**
 * @brief log message
 *
 * @param message prefix
 * @param log mod
 * @param logging level
 * @param message to print
 * @param variable list of parameter
 * @return none
 */
void vk_log(const char *prefix, vk_log_mod log_mod,
	    log_type ltype, log_level level, const char *fmt, ...)
{
	va_list vl;
	static char _loc_buf[256];
	char *p_curr, *p_buf;
	const char *color = "\x1B[0m";
	struct timespec tm;
	int length, max_length;

	if (level > vk_log_ctrl[log_mod].log_level)
		return;

	pthread_mutex_lock(&log_mutex);

	clock_gettime(CLOCK_MONOTONIC, &tm);

	/* determine color first */
	if (level < LOG_ERROR)
		/* panic error message in red */
		color = "\x1B[1m\x1B[31mPANIC:";
	else if (level < LOG_WARNING)
		/* critical error message in red */
		color = "\x1B[31mERROR:";
	else if (level < LOG_INFO)
		/* warning message in yellow */
		color = "\x1B[33mWARNING:";
	else if (level > LOG_INFO)
		/* debug level message in green */
		color = "\x1B[32m";

	p_buf = _loc_buf;

	/* now, log the buffer in thread's context */
	length = snprintf(p_buf, sizeof(_loc_buf),
			  "\x1B[0m[%6d.%06d]%s%s:%s:",
			  (int) tm.tv_sec, (int) tm.tv_nsec / 1000,
			  color,
			  vk_log_ctrl[log_mod].tag,
			  prefix);

	VK_ASSERT(length > 0);

	/* get the rest of buffer */
	p_curr = p_buf + length;

	/*
	 * The max length is the buffer size - 2 formatting parameters:
	 * '\0' to terminate the  string
	 * '\n' in case this one is not included
	 *
	 *
	 * we have to put in all the parameters to the snprintf, where the
	 * later will based on the fmt string to decode.  If the number
	 * parameters is smaller than the max, it will simply not be touched.
	 * Note: if the MAX number of parameters is changed, then, it has
	 *	 to be added at the end.
	 */
	max_length = sizeof(_loc_buf) - length - 2;
	va_start(vl, fmt);
	length = vsnprintf(p_curr, max_length, fmt, vl);
	va_end(vl);

	/*
	 * if truncation happens
	 */
	if (length > max_length)
		length = max_length;
	/*
	 * since an assert has been checked, + the length is limited, append
	 * terminator
	 */
	p_curr[length++] = '\n';
	p_curr[length] = '\0';

	/* print the buffer */

	/*
	 * in the QEMU world, it creates 2 processes that handle the simulation,
	 * and it seems that there is some internal re-piping of output, and in
	 * some cases, the fprintf may get it stuck, use printf instead.
	 */
	printf("%s\x1B[0m\r", p_buf);
	pthread_mutex_unlock(&log_mutex);
}

/**
 * @brief logger init
 *
 * @return always 0 for non zephyr
 */
int32_t vk_logger_init(void)
{
	return 0;
}

/**
 * @brief logger deinit
 *
 * @return always 0 for non zephyr
 */
int32_t vk_logger_deinit(void)
{
	return 0;
}
