// SPDX-License-Identifier: GPL-2.0 OR Apache-2.0
/*
 * Copyright(c) 2018-2019 Broadcom
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
const char *vkil_function_id_str(uint32_t function_id)
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

/**
 * get the description of a command
 * @param command id
 * @return ASCII description of command
 */
const char *vkil_cmd_str(uint32_t cmd)
{
	static const char * const _base_cmd_list[VK_CMD_BASE_MAX] = {
		[VK_CMD_BASE_NONE]      = "none",
		[VK_CMD_BASE_IDLE]      = "idle",
		[VK_CMD_BASE_RUN]       = "run",
		[VK_CMD_BASE_FLUSH]     = "flush",
		[VK_CMD_BASE_UPLOAD]    = "upload",
		[VK_CMD_BASE_DOWNLOAD]  = "download",
		[VK_CMD_BASE_VERIFY_LB] = "process_buffer",
	};

	uint32_t base_cmd_idx = (cmd & VK_CMD_MASK) >> VK_CMD_BASE_SHIFT;

	if (base_cmd_idx < VK_CMD_BASE_MAX)
		return _base_cmd_list[base_cmd_idx];
	else
		return "n/a";
}

/**
 * get the description of options in a command
 * @param command id
 * @return ASCII description of command options
 */
const char *vkil_cmd_opts_str(uint32_t cmd)
{
	uint32_t idx;
	/*
	 * this has to match the defined first option bit location.
	 * For adding additional options, this mapping function needs
	 * to be modified to have new options.
	 */
	static const char * const opt_str[] = {
		"",        "|cb",       "|blk",       "|blk,cb",
		"|gt",     "|gt,cb",    "|gt,blk",    "|gt,blk,cb",
		"|lb",     "|lb,cb",    "|lb,blk",    "|lb,blk,cb",
		"|lb,gt",  "|lb,gt,cb", "|lb,gt,blk", "|lb,gt,blk,cb",
	};

	idx = (cmd & VK_CMD_OPTS_MASK) >> VK_CMD_OPTS_SHIFT;
	return opt_str[idx];
}
