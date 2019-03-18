/* SPDX-License-Identifier: GPL-2.0 OR Apache-2.0 */
/*
 * Copyright(c) 2019 Broadcom
 */
#include <stdint.h>
#include <stdio.h>

/*
 * @file
 * @brief logger subsystem API definitions
 */

#ifndef LOGGER_API_H
#define LOGGER_API_H

#include "vk_logger.h"

/* CMD buffer is 128 byte with 1 byte as header */
#define LOGGER_MAX_CMD_LEN  127

/**
 * define structure that would include the log buffers and all
 * necessary indexes + statistics
 */
typedef struct _logger_buf {

	uint32_t marker;        /**< marker to indicate if init is good */
	uint32_t cmd_off;       /**< offset of cmd buffer from start */

	/*-------------------------------------------------------------------*/
	uint32_t spool_nentries;/**< total of spool entries  */
	uint32_t spool_len;     /**< length of per spooled entry */
	uint32_t spool_off;     /**< offset of spooled buffer from beginning */
	uint32_t spool_idx;     /**< idx of the next spooled buffer */

	/*-------------------------------------------------------------------*/
	uint32_t log_nentries;  /**< total of entries in the buffer */
	uint32_t log_len;       /**< per entry length */
	uint32_t log_off;       /**< offset of the logging buffer from start */

	/* log entry house-keeping variables */
	uint32_t wr_idx;        /**< update by user when log */
	uint32_t rd_idx;        /**< update by spooler, non-protected */
	uint32_t wr_tot;        /**< total written */
	uint32_t wr_tail_drop;  /**< # of drops when wr */
	uint32_t rd_tot;        /**< rd_total */

	/*-------------------------------------------------------------------*/
	uint32_t alloc_size;    /**< size of whole logging area */
	uint32_t max_mod;       /**< maximum number of sub-modules used */
	uint32_t logger_mod;    /**< mod used by the logger to print */
	uint16_t spool_only;    /**< only spooled to buffer */
} logger_buf;

/* enqueue and log */
void logger_enq(const char *prefix, uint32_t log_mod,
		log_type ltype, log_level level, const char *fmt, ...);
void vlogger_enq(const char *prefix, uint32_t log_mod,
		log_type ltype, log_level level, const char *fmt, va_list vl);

/* logger initialization function */
int32_t logger_init(logger_ctrl *ctrl, uint32_t max_subsystem,
		    uint32_t logger_mod, logger_buf *log_buf,
		    uint32_t alloc_size,
		    void (*cmd_handler)(const char *cmd));

/* logger deinit function */
int32_t logger_deinit(void);

/* set log level */
int log_set_loglevel(const char *mod, log_level level);

/* dump log */
void log_dump(const char *num_str);

#endif /* LOGGER_API_H */
