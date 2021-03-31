// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright 2018-2020 Broadcom.
 */

#include <stdint.h>
#include "vk_parameters.h"
#include "vkil_backend.h"

#ifndef ARRAY_SIZE
/* this is defined in kernel, but is not expected to be defined here */
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*(a)))
#endif

/**
 * return the str representation of a function id
 * @param function id
 * @return ASCII description of function id
 */
const char *vkil_function_id_str(const uint32_t function_id)
{

	static const char * const vkil_fun_list[VK_FID_MAX] = {
		[VK_FID_UNDEF]          = "undefined",

		[VK_FID_INIT]           = "init",
		[VK_FID_DEINIT]         = "deinit",
		[VK_FID_SET_PARAM]      = "set_parameter",
		[VK_FID_GET_PARAM]      = "get_parameter",
		[VK_FID_TRANS_BUF]      = "transfer_buffer",
		[VK_FID_PROC_BUF]       = "process_buffer",
		[VK_FID_XREF_BUF]       = "reference/dereference_buffer",
		[VK_FID_PRIVATE]        = "private",
		[VK_FID_SHUTDOWN]       = "shutdown",

		[VK_FID_INIT_DONE]      = "init_done",
		[VK_FID_DEINIT_DONE]    = "deinit_done",
		[VK_FID_SET_PARAM_DONE] = "parameter_set",
		[VK_FID_GET_PARAM_DONE] = "parameter_got",
		[VK_FID_TRANS_BUF_DONE] = "buffer_transferred",
		[VK_FID_PROC_BUF_DONE]  = "buffer_processed",
		[VK_FID_XREF_BUF_DONE]  = "buffer_referenced/dereferenced",
		[VK_FID_PRIVATE_DONE]   = "private_done",
	};

	if ((function_id < ARRAY_SIZE(vkil_fun_list)) &&
	     vkil_fun_list[function_id])
		return vkil_fun_list[function_id];
	else
		return "N/A";
};

/**
 * string version of a shutdown type
 * @param shut down type
 * @return ASCII description of shutdown type
 */
const char *vkil_shutdown_type_str(const vkil_shutdown_type type)
{
	static const char * const _shutdown_type_list[VK_SHUTDOWN_TYPE_MAX] = {
		[VK_SHUTDOWN_UNDEF]    = "undefined",
		[VK_SHUTDOWN_PID]      = "pid",
		[VK_SHUTDOWN_GRACEFUL] = "graceful",
	};

	if (type < VK_SHUTDOWN_TYPE_MAX)
		return _shutdown_type_list[type];
	else
		return "n/a";
}
