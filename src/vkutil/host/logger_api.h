/* SPDX-License-Identifier: GPL-2.0 OR Apache-2.0 */
/*
 * Copyright(c) 2018-2019 Broadcom
 */

#ifndef LOGGER_API_H
#define LOGGER_API_H

#include <stdint.h>

/**
 * logger control structure
 * This allows user to pass in its definition of control structure
 */
typedef struct _logger_ctrl {
	uint32_t log_level;
	const char *tag;
} logger_ctrl;

typedef enum _log_level {
	/** something went very wrong, and we gonna crash (assert)*/
	LOG_PANIC       = 0,
	/** something went wrong, error handled to function caller */
	LOG_ERROR       = 16,
	/** unexpected behavior, not compromising the program execution */
	LOG_WARNING     = 32,
	/** standard information */
	LOG_INFO        = 64,
	/** debugging info */
	LOG_DEBUG       = 128
} log_level;

/**
 * @brief Type of logging which specifies parameters' size
 *
 * LOG_TYPE_INT: The default one is using INT as the passing parameter.
 * LOG_TYPE_64B: 64 bit parameter
 * LOG_TYPE_UL: unsigned long
 *
 * In general, all the parameters using the logging macro has to be same size
 * as we will not decode the list at run time.	This is restriciton one, which
 * implies that for using the macro, we may need to put a cast to the type.
 *
 * The LOG_TYPE_UL is used for arch when it is running in 64bit mode, and if its
 * arg list is a mixed of %p %d etc.  In this case, eg.x86-64, pointer will
 * be 64 bits while the data is 32.  All the 32-bit data should be casted
 * to unsigned long so that the compiler will form the proper parameter list
 * on stack. This is more for future.  Note that on 32bit machine, unsigned long
 * is 32-bit and all pointers are 32bits, so using type INT would be OK.
 */
typedef enum _log_type {
	LOG_TYPE_INT      = 0,
	LOG_TYPE_ULL      = 1,
	LOG_TYPE_UL       = 2,
} log_type;

/**
 * @brief set all log modules to a specific log level
 *
 * This function is tailored to vkil where only one sub-module is used
 * and so we could set them all to same level.  There is no plan to
 * distinguish individual sub-module.
 * @param level set level in ascii format
 * @return 0 on success, negative on error
 */
int32_t vk_log_set_level_all(const char *level);

#endif
