// SPDX-License-Identifier: GPL-2.0 OR Apache-2.0
/*
 * Copyright(c) 2019 Broadcom
 */

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "logger_api.h"
#include "vk_logger.h"
#include "vk_utils.h"
#include "vksim_internal.h"
#include "vksim_utils.h"

#include <shell/shell.h>
#define VK_LOG_DEF_LEVEL VK_LOG_INFO

static logger_ctrl vk_log_ctrl[VK_LOG_MOD_MAX] = {
	[VK_LOG_MOD_GEN] = { VK_LOG_DEBUG,     ""    },
	[VK_LOG_MOD_INF] = { VK_LOG_DEF_LEVEL, "inf" },
	[VK_LOG_MOD_ENC] = { VK_LOG_DEF_LEVEL, "enc" },
	[VK_LOG_MOD_DEC] = { VK_LOG_DEF_LEVEL, "dec" },
	[VK_LOG_MOD_DMA] = { VK_LOG_DEF_LEVEL, "dma" },
	[VK_LOG_MOD_SCL] = { VK_LOG_DEF_LEVEL, "scl" },
	[VK_LOG_MOD_DRV] = { VK_LOG_DEF_LEVEL, "drv" },
	[VK_LOG_MOD_SYS] = { VK_LOG_DEF_LEVEL, "sys" },
	[VK_LOG_MOD_MVE] = { VK_LOG_DEF_LEVEL, "mve" },
};

/**
 * log message
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

	va_start(vl, fmt);
	vlogger_enq(prefix, log_mod, ltype, level, fmt, vl);
	va_end(vl);
}

void vk_vcon_cmd_handler(const char *cmd)
{
	int argc;
	char *argv[32]; /* just arbitrary and we should never exceed this */
	static char tmp_buf[LOGGER_MAX_CMD_LEN]; /* max size */
	char *p_char;
	uint32_t i, j;
	struct shell_cmd *p_cmd;

	/* reuse of the shell struct commands */
	extern struct shell_cmd vksim_shell_commands[];
	extern struct shell_cmd shell_commands[];
	static struct shell_cmd *cmd_tab_list[] = {
		vksim_shell_commands,
		shell_commands,
	};

#define _SKIP_SPACE(_p) {while (*_p == ' ') _p++; }

	/*
	 * decode cmd into argc and argv to be passed to cmd parser
	 * directly.
	 */
	strncpy(tmp_buf, cmd, sizeof(tmp_buf));
	p_char = tmp_buf;
	_SKIP_SPACE(p_char);

	argc = 0;
	argv[argc] = p_char;

	if (*p_char != '\0') {
		while (*p_char != '\0') {

			if (*p_char == ' ') {
				*p_char++ = '\0';

				_SKIP_SPACE(p_char);
				argc++;
				argv[argc] = p_char;
			} else {
				p_char++;
			}
		}
		argc++;
	}

	/* nothing needs to be done */
	if (argc == 0)
		return;

	if (strcmp(argv[0], "help") == 0) {
		for (i = 0; i < ARRAY_SIZE(cmd_tab_list); i++) {

			p_cmd = cmd_tab_list[i];
			j = 0;
			while (p_cmd[j].cmd_name != NULL) {
				vk_log("VCON:", VK_LOG_MOD_GEN, LOG_TYPE_INT,
				       VK_LOG_INFO, "%s", p_cmd[j].help);
				j++;
			}
		}
	} else {
		bool found = false;

		for (i = 0; i < ARRAY_SIZE(cmd_tab_list) && !found; i++) {

			p_cmd = cmd_tab_list[i];
			j = 0;
			while (p_cmd[j].cmd_name != NULL) {
				if (strcmp(argv[0],
					   p_cmd[j].cmd_name) == 0) {
					found = true;
					break;
				}
				j++;
			}
		}

		if (found)
			p_cmd[j].cb(argc, argv);
		else
			vk_log("VCON:", VK_LOG_MOD_GEN, LOG_TYPE_INT,
			       VK_LOG_INFO,
			       "Command not found, please use help");

	}
}

/**
 * logger init
 * @return return 0 on success, else error
 */
int32_t vk_logger_init(void)
{
	int ret;

	static logger_buf *logger;

	/*
	 * For now, allocate the memory on heap.
	 * Once the memory map for BAR2 is created, will initialize the
	 * pointer directly.
	 */
#define VK_LOGGER_HEAP_SIZE    (256 * 1024)
	ret = vksim_mallocz_high(&logger, VK_LOGGER_HEAP_SIZE);
	if (ret)
		return ret;
	/*
	 * TO FIX: the following is experimental code, and will replace
	 * the above allocation when the MAP is available.
	 *
	 * logger = (logger_buf *)0x60040000;
	 */

	return logger_init(vk_log_ctrl, VK_LOG_MOD_MAX,
			   VK_LOG_MOD_GEN, logger,  VK_LOGGER_HEAP_SIZE,
			   vk_vcon_cmd_handler);
}

/**
 * logger deinit
 * @return return 0 on success, else error
 */
int32_t vk_logger_deinit(void)
{
	/* simply call logger infra deinit function */
	return logger_deinit();
}