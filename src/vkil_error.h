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

#ifndef VKIL_ERROR_H
#define VKIL_ERROR_H

#include <string.h>
#include "vk_error.h"
#include "vk_utils.h"

#define VKILERROR(type) VKERROR_MAKE(VKIL, 0, vkil_error(__func__), type)

/**
 * Generate an index number from a function name
 * To be used to build an error code
 *
 * @param functionname function name
 * @return             function index
 */
static int32_t vkil_error(const char *functionname)
{
	static const char * const vkil_fun_list[] = {
		"undefined",
		"vkil_init",
		"vkil_deinit",
		"vkil_set_parameter",
		"vkil_get_parameter",
		"vkil_send_buffer",
		"vkil_receive_buffer",
		"vkil_upload_buffer",
		"vkil_download_buffer",
		"vkil_uploaded_buffer",
		"vkil_downloaded_buffer"};
	int32_t i;

	for (i = 1; i < VK_ARRAY_SIZE(vkil_fun_list); i++) {
		if (!strcmp(vkil_fun_list[i], functionname))
			return i;
	}
	return 0;
};

/* macros for handling error condition */
#define VKDRV_WR_ERR(_ret, _size)     ((_ret < 0) || (_ret != _size))
#define VKDRV_RD_ERR(_ret, _size)     ((_ret < 0) || (_ret != _size))

#endif
