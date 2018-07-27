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

#include <unistd.h>
#include "vk_utils.h"


/**
 * try to call a function with given parameters and timeout afetr VK_TIMEOUT_MS
 * @param pointer to function
 * @param handle to the component
 * @param message to process
 * @return zero if success otherwise error message
 */
ssize_t vkil_wait_probe_msg(ssize_t (*f)(int fd, void *buf, size_t nbytes),
			int fd, void *buf, size_t nbytes)
{
	int32_t ret, i;

	for (i = 0 ; i < VK_TIMEOUT_MS ; i++) {
		ret =  f(fd, buf, nbytes);
		if (ret >= 0)
			return ret;
		usleep(1);
	}
	return (-ETIMEDOUT); /* if we are here we have timed out */
}

#define VKIL_LOG(...) vk_log(__func__, __VA_ARGS__)

#endif
