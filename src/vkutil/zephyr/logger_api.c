// SPDX-License-Identifier: GPL-2.0 OR Apache-2.0
/*
 * Copyright(c) 2019 Broadcom
 */
#include <errno.h>
#include <kernel.h>
#include <misc/__assert.h>
#include <pthread.h>
#include <sys_clock.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zephyr.h>
#include "brcm_alloc_wrapper.h"
#include "logger_api.h"

/* local defines */

/* For Zephyr, needs to use irq_lock() */
#define _LOCK_ACCESS()     {key = irq_lock(); }
#define _UNLOCK_ACCESS()   irq_unlock(key)

/* macro to make alignment in cache */
#define _CACHE_LINE_SIZE    32
#define _ALIGN(x)           ((x + _CACHE_LINE_SIZE - 1) \
			      & ~(_CACHE_LINE_SIZE - 1))

#define LOGGER_LOC_LOG(...) logger_enq(__func__, p_log_buf->logger_mod, \
				       LOG_TYPE_INT, __VA_ARGS__)

#define LOGGER_EBUF_LEN     256
#define LOGGER_STACK_SIZE   8096

/*
 * default polling period for the logger to do log conversion
 * and we use 2 ticks' time here as the default
 */
#define LOGGER_POLL_DEF_US    (2 * (1000ul / sys_clock_ticks_per_sec) * 1000)
/* input handling default period, 100 ms should be fast enough for human */
#define LOGGER_INPUT_POLL_US  (100 * 1000)

/**
 * buffer used to store log.  A circular buffer is used to
 * store some previous logs.  Aim is to be able to retrieve
 * log even if user forgot to turn on the logging.
 * In the long-run, the buffer could be mapped to the PCIe
 * BAR memory, and if so, buffer could still be retrieved
 * in case the VK firwmare gets into a complete hunk state.
 */
#define LOG_N_ENTRIES            (1 << 9) /* number of entries */
#define LOG_IDX_MASK(_p)         (_p->log_nentries - 1)
#define SPOOL_IDX_MASK(_p)       (_p->spool_nentries - 1)
#define LOG_EBUF_LEN             256
#define LOG_BUF_MARKER_VAL       0xbeefcafe
#define LOG_BUF_FULL(_p)         \
	(((_p->wr_idx + 1) & LOG_IDX_MASK(_p)) == _p->rd_idx)\

/**
 * a single log entry, to facilate handling
 */
#define LOG_MAX_ARGS    10
typedef struct _log_entry {
	/* data structures for storing all necessary printing parameters */
	struct timespec tm;
	log_type ltype;
	log_level level;
	uint32_t  mod;
	const char *prefix;
	const char *fmt;
	char args[LOG_MAX_ARGS * sizeof(uint32_t)];
} log_entry;

typedef struct _spool_entry {
	char data[LOG_EBUF_LEN];
} spool_entry;

/*
 * Local static variables.  Use static so that only 1 copy of logger
 * exists even when multiple devices are created.
 */
static pthread_t      log_pthread;
static bool           logger_in_service; /* indication if we want to quit */
static uint32_t       log_client_cnt;
static logger_buf    *p_log_buf;
static logger_ctrl   *p_log_ctrl;
static log_entry     *entry_buf;   /* local derived value of entry buffer */
static spool_entry   *spool_buf;   /* local derived spool buffer */
static void          (*process_cmd)(const char *);
static char          *p_cmd;

/**
 * Function to invalidate cache so that data will land on DDR mem
 * @param add address to be invalidated
 * @param size size to be invalidated
 */
static inline void log_invalidate_cache(void *addr, int32_t size)
{
#if defined __DCACHE_PRESENT && (__DCACHE_PRESENT == 1U)
	SCB_CleanInvalidateDCache_by_Addr(addr, size);
#endif
}

/**
 * Function to log a single line to UART
 * @param prefix prefix of line
 * @param p_buf pointer to buffer
 * @return none
 */
static inline void log_line(const char *prefix, const char *p_buf)
{
	fprintf(stdout, "%s%s\x1B[0m\r", prefix, p_buf);
}

/**
 * Function to convert a log entry into a buffer for display
 * @param p_vl pointer to entry
 * @param p_entry buffer for storing the ascii output
 * @return none
 */
static void log_entry_to_buf(va_list *p_vl, log_entry *p_entry,
			     char buf[LOG_EBUF_LEN])
{
	char *p_buf = buf;
	char *p_curr;
	int length, max_length;
	char *color = "\x1B[0m";
	log_level level = p_entry->level;
	struct timespec *tm = &p_entry->tm;

	/*
	 * seems c-lib function will run crazy if a NULL is used as
	 * the fmt string, so do a check.
	 */
	__ASSERT(p_entry->fmt, "");

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

	/* now, log the buffer in thread's context */
	length = snprintf(p_buf, LOGGER_EBUF_LEN,
			  "\x1B[0m[%6d.%06d]%s%s:%s:",
			  (int) tm->tv_sec, (int) tm->tv_nsec / 1000,
			  color,
			  p_log_ctrl[p_entry->mod].tag,
			  p_entry->prefix);

	__ASSERT(length > 0, "");

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
	max_length = LOGGER_EBUF_LEN - length - 2;
	if (p_entry->ltype == LOG_TYPE_INT) {
		int *p_int = (int *)p_entry->args;

		length = snprintf(p_curr, max_length, p_entry->fmt,
				  p_int[0], p_int[1], p_int[2],
				  p_int[3], p_int[4], p_int[5],
				  p_int[6], p_int[7], p_int[8],
				  p_int[9]);

	} else if (p_entry->ltype == LOG_TYPE_ULL) {
		int64_t *p_int64 = (int64_t *)p_entry->args;

		length = snprintf(p_curr, max_length, p_entry->fmt,
				  p_int64[0], p_int64[1], p_int64[2],
				  p_int64[3], p_int64[4], p_int64[5],
				  p_int64[6], p_int64[7], p_int64[8],
				  p_int64[9]);

	} else if (p_entry->ltype == LOG_TYPE_UL) {
		unsigned long *p_ulong = (unsigned long *)p_entry->args;

		length = snprintf(p_curr, max_length, p_entry->fmt,
				  p_ulong[0], p_ulong[1], p_ulong[2],
				  p_ulong[3], p_ulong[4], p_ulong[5],
				  p_ulong[6], p_ulong[7], p_ulong[8],
				  p_ulong[9]);
	}

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
}

/**
 * Function to fill the parameters
 * @param vl variable parameter list
 * @param n_args number of args
 * @param p_entry pointer to log entry
 */
static inline void log_fill_args(va_list vl, uint32_t n_args,
				 log_entry *p_entry)
{
	char *p_chr = p_entry->args;
	int i;

	if (p_entry->ltype == LOG_TYPE_INT)
		for (i = 0; i < n_args; i++) {
			*(int *)p_chr = va_arg(vl, int);
			p_chr += sizeof(int);
		}
	else if (p_entry->ltype == LOG_TYPE_ULL)
		for (i = 0; i < n_args; i++) {
			*(int64_t *)p_chr = va_arg(vl, int64_t);
			p_chr += sizeof(int64_t);
		}
	else if (p_entry->ltype == LOG_TYPE_UL)
		for (i = 0; i < n_args; i++) {
			*(unsigned long *)p_chr = va_arg(vl, unsigned long);
			p_chr += sizeof(unsigned long);
		}
}

/**
 * enqueue a message to the logger
 * @param prefix message prefix
 * @param log_mod mod that does the log
 * @param ltype logging type
 * @param level logging level
 * @param fmt format of message
 * @param vl variable list of parameter
 * @return none
 */
void vlogger_enq(const char *prefix, uint32_t log_mod,
		 log_type ltype, log_level level, const char *fmt, va_list vl)
{
	log_entry *p_entry;
	uint32_t n_args = 0;
	const char *curr = fmt;
	unsigned int key;

	/*
	 * find the number of % as num of args. This means that '%' is not
	 * allowed, but there is no such usage in our code.
	 */
	while (*curr != '\0')
		if (*curr++ == '%')
			n_args++;
	__ASSERT(n_args <= LOG_MAX_ARGS, "");

	/*
	 * the mutex is essentially used to grant exclusive access to
	 * log_buffer (which is static to avoid C stack issue).
	 * however to keep the time consistently incrementing in the
	 * log, we protect the access to the clock by the mutex too.
	 *
	 */
	_LOCK_ACCESS();

	/*
	 * If we hit a full condition, it is kind of system issue,
	 * which needs to be fixed with either making the spooler
	 * thread higher priority or so.  Here we log statistics.
	 */
	if (LOG_BUF_FULL(p_log_buf)) {
		p_log_buf->wr_tail_drop++;
		_UNLOCK_ACCESS();
		return;
	}

	p_entry = &entry_buf[p_log_buf->wr_idx];
	clock_gettime(CLOCK_MONOTONIC, &p_entry->tm);
	p_entry->fmt = fmt;
	p_entry->ltype = ltype;
	p_entry->level = level;
	p_entry->mod = log_mod;
	p_entry->prefix = prefix;

	log_fill_args(vl, n_args, p_entry);

	p_log_buf->wr_idx = (p_log_buf->wr_idx + 1)
			     & LOG_IDX_MASK(p_log_buf);
	p_log_buf->wr_tot++;

	_UNLOCK_ACCESS();
}

/**
 * enqueue a message to the logger
 * @param prefix message prefix
 * @param log_mod mod that does the log
 * @param ltype logging type
 * @param level logging level
 * @param fmt format of message
 * @return none
 */
void logger_enq(const char *prefix, uint32_t log_mod,
		log_type ltype, log_level level, const char *fmt, ...)
{
	va_list vl;

	va_start(vl, fmt);
	vlogger_enq(prefix, log_mod, ltype, level, fmt, vl);
	va_end(vl);
}

/**
 * Get an converted ascii buffer to display
 * @param p_entry pointer to log entry
 * @return pointer to the buffer to be displayed
 */
static char *log_get_ascii_output(log_entry *p_entry)
{
	char *p_buf;
	uint32_t next_idx = (p_log_buf->spool_idx + 1)
			     & SPOOL_IDX_MASK(p_log_buf);

	/*
	 * we don't need to do a flush for now as the zephyr has been
	 * using write-through policy.  If that is to be changed to
	 * write-back only, then, the cache invalidate function should
	 * be called.
	 */
	p_buf = (char *)&spool_buf[p_log_buf->spool_idx];
	log_entry_to_buf(NULL, p_entry, p_buf);
	p_log_buf->spool_idx = next_idx;

	return p_buf;
}

/**
 * log dump routine - routine to logs in circular buffer
 * @param num_str number of entries from latest in ascii format
 * @return none
 */
void log_dump(const char *num_str)
{
	uint32_t idx, i, num;
	const char *p_buf;
	log_entry *p_entry;

	/*
	 * Note: this routine is run while the spooler is running
	 *	 and therefore, is intended to be used at static state, ie,
	 *	 post decode session.
	 */
	if (num_str && strcmp(num_str, "clr") == 0) {
		/* clear all */
		memset(entry_buf, 0,
		       sizeof(log_entry) * p_log_buf->log_nentries);
		p_log_buf->rd_idx = p_log_buf->wr_idx = 0;
		LOGGER_LOC_LOG(LOG_INFO, "All logs cleared!");
		return;
	}

	/* user wants to display past logs */
	if ((num_str == NULL) || (strcmp(num_str, "all") == 0))
		num = p_log_buf->log_nentries;
	else
		num = atoi(num_str);

	if (num > p_log_buf->log_nentries)
		num = p_log_buf->log_nentries;

	/* start from oldest index */
	idx = (p_log_buf->wr_idx + (p_log_buf->log_nentries - num))
		     & LOG_IDX_MASK(p_log_buf);
	for (i = 0; i < num; i++) {
		p_entry = &entry_buf[idx];

		/* use the time stamp != 0 as a valid entry indicator */
		if ((p_entry->tm.tv_sec != 0) || (p_entry->tm.tv_nsec != 0)) {
			char prefix[20];

			p_buf = log_get_ascii_output(p_entry);
			snprintf(prefix, sizeof(prefix), "[%3d]", idx);
			if (!p_log_buf->spool_only)
				log_line(prefix, p_buf);
		}
		idx = (idx + 1) & LOG_IDX_MASK(p_log_buf);
	}
	/* log statistics */
	LOGGER_LOC_LOG(LOG_INFO,
		  "[Wr Rd] = [%d %d], Wr-tail-drop[%d].",
		  p_log_buf->wr_tot,
		  p_log_buf->rd_tot,
		  p_log_buf->wr_tail_drop);
}

/**
 * set a sub mod log to a specific level
 * @param mod logging sub mod
 * @param level logging level
 * @return 0 on success, and -EINVAL if no match and nothing is done
 */
int log_set_loglevel(const char *mod, log_level level)
{
	uint32_t curr;
	bool set_all = (strcmp(mod, "all") == 0);
	int ret = -EINVAL;

	/*
	 * use the tag matching as this function is supposed to be
	 * hooked to CLI which is ascii based.
	 */
	for (curr = 0; curr < p_log_buf->max_mod; curr++)
		if ((set_all ||
		     (strcmp(mod, p_log_ctrl[curr].tag) == 0)) &&
		    (curr != p_log_buf->logger_mod)) {
			ret = 0;
			p_log_ctrl[curr].log_level = level;
		}

	/* Dump the new settings if something is set */
	if (ret == 0)
		for (curr = 0; curr < p_log_buf->max_mod; curr++)
			LOGGER_LOC_LOG(LOG_INFO,
				  "%s: -> level %d", p_log_ctrl[curr].tag,
				  p_log_ctrl[curr].log_level);

	return ret;
}

/**
 * check and process command
 */
void handle_input_cmd(void)
{
	static char _loc_buf[LOGGER_MAX_CMD_LEN];
	char *p_dst;
	char *p_src;
	static uint32_t poll_cnt;

	/* check to see if it is necessary to go on */
	if (++poll_cnt < (LOGGER_INPUT_POLL_US / LOGGER_POLL_DEF_US))
		return;
	poll_cnt = 0;

	log_invalidate_cache(p_cmd, sizeof(_loc_buf));

	/* check if there is anything */
	if (*p_cmd == 0)
		return;

	/* cp to local buffer */
	p_src = p_cmd + 1;
	p_dst = _loc_buf;
	strncpy(p_dst, p_src, sizeof(_loc_buf));

	/* mark cmd location to be free */
	*p_cmd = 0;
	log_invalidate_cache(p_cmd, 1);

	/*
	 * 2 special commands are handled inline where the rest will be
	 * processed by the callback
	 */
	if (strcmp(_loc_buf, "enable") == 0) {
		p_log_buf->spool_only = 1;
		logger_enq(__func__, p_log_buf->logger_mod, LOG_TYPE_INT,
			   LOG_INFO, "VCON: enabled - UART disabled");
	} else if (strcmp(_loc_buf, "disable") == 0) {
		p_log_buf->spool_only = 0;
		logger_enq(__func__, p_log_buf->logger_mod, LOG_TYPE_INT,
			   LOG_INFO, "VCON: disabled - UART re-enabled");
	} else {
		process_cmd(_loc_buf);
	}
}

/**
 * log io function, run in continuous mode
 * @param arg thread arg, passed in pointer to log buffer
 */
static void *log_io_thread(void *arg)
{
	uint16_t rd_idx;
	logger_buf *log = arg;
	log_entry *p_entry;
	char *p_buf;
	/*
	 * For first cut, we use a fixed number for the sleep.  In the long run,
	 * this sleep would be self-adjusted.  We use 2 ticks in the zephyr.
	 * Also, we limit the number of print per loop since co-operative
	 * scheduling is used.  This will limit the time spent in this routine,
	 * which cause tail drop in the client calling vk_log().
	 */
	uint32_t sleep_duration_us = LOGGER_POLL_DEF_US;
	uint32_t output_total;

#define LOG_MAX_LINE_PER_LOOP    10

	__ASSERT(log, "");
	while (logger_in_service) {

		/* handle input */
		handle_input_cmd();

		/* handle output */
		rd_idx = log->rd_idx;
		p_entry = &entry_buf[rd_idx];

		output_total = 0;
		while (rd_idx != log->wr_idx) {

			if (p_entry->level <=
				    p_log_ctrl[p_entry->mod].log_level) {

				p_buf = log_get_ascii_output(p_entry);

				/*
				 * only limit when output to UART, if not
				 * finish them until all done
				 */
				if (!p_log_buf->spool_only) {
					log_line("", p_buf);
					output_total++;
				}
			}

			log->rd_tot++;
			rd_idx = (rd_idx + 1) & LOG_IDX_MASK(log);
			log->rd_idx = rd_idx;
			p_entry = &entry_buf[rd_idx];

			if (output_total > LOG_MAX_LINE_PER_LOOP)
				break;
		}
		usleep(sleep_duration_us);
	}

	LOGGER_LOC_LOG(LOG_INFO, "Flushing remaining logs before exit.");

	/* termination of thread, need to write out remaining */
	rd_idx = log->rd_idx;
	while (rd_idx != log->wr_idx) {

		p_entry = &entry_buf[rd_idx];
		p_buf = log_get_ascii_output(p_entry);
		if (!p_log_buf->spool_only)
			log_line("", p_buf);
		rd_idx = (rd_idx + 1) & LOG_IDX_MASK(log);
	}
	return NULL;
}

/**
 * function to init logger
 * @param ctrl pointer to logger control structure, allocated by user
 * @param max_subsystem the max number of sub-system user wants
 * @param logger_mod mod that the logger should be using when log
 * @param log_buf pointer to the logger buffering location
 * @param alloc_size total size allocated for the logging subsystem
 * @param cmd_handler callback to provided by user of cmd channel
 * @return 0 on success, negative number in error
 */
int32_t logger_init(logger_ctrl *ctrl, uint32_t max_subsystem,
		    uint32_t logger_mod, logger_buf *log_buf,
		    uint32_t alloc_size,
		    void (*cmd_handler)(const char *cmd))
{
	int ret;
	pthread_attr_t attr;
	unsigned int key;
	void *stack;
	size_t stacksize;

	/*
	 * Return if it has been setup.  This is only needed for the simulation
	 * environment where multiple devices maybe created.
	 * For real HW/FPGA/QEMU, only 1 device will be created and so it won't
	 * be initialized multiple times.
	 */
	_LOCK_ACCESS();
	log_client_cnt++;
	if (logger_in_service) {
		_UNLOCK_ACCESS();
		return 0;
	}
	logger_in_service = true;
	_UNLOCK_ACCESS();

	/*
	 * set up some variables in case the pointer is relocated to
	 * some pre-init memory to be safe
	 */
	memset(log_buf, 0, alloc_size);

	process_cmd = cmd_handler;
	p_log_ctrl = ctrl;
	p_log_buf = log_buf;

	p_log_buf->log_nentries = LOG_N_ENTRIES;
	p_log_buf->log_len = sizeof(log_entry);
	p_log_buf->logger_mod = logger_mod;
	p_log_buf->max_mod = max_subsystem;
	p_log_buf->alloc_size = alloc_size;

	/* right after the structure */
	p_log_buf->log_off = _ALIGN(sizeof(*p_log_buf));
	/* derive other locations */
	entry_buf = (log_entry *)(((char *)p_log_buf) + p_log_buf->log_off);

	p_log_buf->spool_nentries = LOG_N_ENTRIES;
	p_log_buf->spool_len = sizeof(spool_entry);

	p_log_buf->spool_off =
		_ALIGN(p_log_buf->log_off +
		       sizeof(log_entry) * p_log_buf->log_nentries);

	spool_buf = (spool_entry *)(((char *)p_log_buf)
				    + p_log_buf->spool_off);

	p_log_buf->cmd_off =
		_ALIGN(p_log_buf->spool_off +
		       sizeof(spool_entry) * p_log_buf->spool_nentries);

	p_cmd = ((char *)p_log_buf) + p_log_buf->cmd_off;

	__ASSERT(p_log_buf->cmd_off + LOGGER_MAX_CMD_LEN < alloc_size, "");

	/* log the info for debugging, these lines will appear once at start */
	fprintf(stdout, "\n\r[%p] entry_buf %p spool_buf %p cmd_off 0x%x\n",
		p_log_buf, entry_buf, spool_buf, p_log_buf->cmd_off);
	fprintf(stdout, "logger_mod %d, max_mod %d, alloc_size 0x%x\n",
		p_log_buf->logger_mod, p_log_buf->max_mod,
		p_log_buf->alloc_size);
	fprintf(stdout,	"spool: nentries %d, spool_len %d, spool_off 0x%x\n",
		p_log_buf->spool_nentries,
		p_log_buf->spool_len, p_log_buf->spool_off);
	fprintf(stdout, "log  : nentries %d, log_len %d, log_off 0x%x\n",
		p_log_buf->log_nentries, p_log_buf->log_len,
		p_log_buf->log_off);

	ret = pthread_attr_init(&attr);
	if (ret)
		goto fail_pthread;

	stacksize = LOGGER_STACK_SIZE;
	stack = brcm_mem_alloc_32(stacksize);
	if (!stack) {
		log_line(__func__, "alloc_pthread_stack failed");
		goto fail_pthread;
	}
	memset(stack, 0, stacksize);
	pthread_attr_setstack(&attr, stack, stacksize);

	ret = pthread_create(&log_pthread, &attr, log_io_thread, p_log_buf);
	if (ret)
		goto fail_pthread;

	p_log_buf->marker = LOG_BUF_MARKER_VAL;
	return 0;

fail_pthread:
	logger_in_service = false;
	log_line(__func__, "Logger creation fails!");
	return -EINVAL;
}

/**
 * function to deinit logger
 * @return 0 on success, negative number in error
 */
int32_t logger_deinit(void)
{
	int32_t ret;
	unsigned int key;

	/* nothing needs to be done if someone has put it out of service */
	_LOCK_ACCESS();
	log_client_cnt--;
	if ((logger_in_service == false) || (log_client_cnt != 0)) {
		_UNLOCK_ACCESS();
		return 0;
	}
	logger_in_service = false;
	_UNLOCK_ACCESS();

	ret = pthread_join(log_pthread, NULL);
	return ret;
}
