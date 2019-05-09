/* SPDX-License-Identifier: GPL-2.0 OR Apache-2.0 */
/*
 * Copyright(c) 2018 Broadcom
 */

#ifndef VKIL_BACKEND_H
#define VKIL_BACKEND_H

/*
 * this declaration file is to be called by the valkyrie card
 * the vkil library (and possibly the vk driver)
 */

#include <stdint.h>
#include <string.h>
#include "vk_parameters.h"

#define VK_START_VALID_HANDLE (0x400)

/* msg size is expressed in multile of 16 bytes */
#define MSG_SIZE(size) ((size + 15)/16)

/**
 * message structure from host to Valkyrie card
 */
typedef struct _host2vk_msg {
	/** this refers to a function listed in vkil_fun_list */
	uint8_t function_id;
	uint8_t size;           /**< message size is 16*(1+size) bytes */
	uint16_t queue_id:4 ;   /**< this provide the input queue index */
	uint16_t msg_id:12 ;    /**< unique message identifier in the queue */
	uint32_t context_id;    /**< handle to the HW context */
	uint32_t args[2];       /**< argument list taken by the function */
} host2vk_msg;

/**
 * message structure from Valkyrie card to host
 */
typedef struct _vk2host_msg {
	/** this refers to a function listed in vkil_fun_list */
	uint8_t function_id;
	uint8_t size;           /**< message size is 16*(1+size) bytes */
	uint16_t queue_id:4 ;   /**< this match the host2vk_msg (queue_id) */
	uint16_t msg_id:12 ;    /**< this match the host2vk_msg (msg_id) */
	uint32_t context_id;    /**< handle to the HW context */
	/** return hw status. if ERROR, error_code carries in arg */
	uint32_t hw_status;
	uint32_t arg;           /**< return argument (depend on function) */
} vk2host_msg;

/**
 * enum type for the function id
 */
typedef enum _vk_function_id_t {
	/* context_id specify on which context the function apply */
	VK_FID_UNDEF,

	/* function carried by host2vk_msg */

	/*
	 * un-mutable session...
	 * Currently, there are 2 FIDs exposed and used in the driver
	 * which makes them non-mutable.
	 *     VK_FID_TRANS_BUF must be 5,
	 *     VK_FID_SHUTDOWN must be 8
	 * These are put at the beginning here.  The unused (1-4,
	 * 6-7) will be reserved for future unmutables.
	 */
	VK_FID_TRANS_BUF = 5, /**< args[0]= cmd, args[2]=host buffer desc*/
	VK_FID_SHUTDOWN  = 8, /**< shut down command                     */
	/* end of un-mutables */

	/**
	 * If the context is set to VK_NEW_CTX,
	 * a new context is created and handle returned by init_done
	 */
	VK_FID_INIT,
	VK_FID_DEINIT,
	VK_FID_SET_PARAM, /**< args[0]=field, args[1]=value              */
	VK_FID_GET_PARAM, /**< args[0]=field, args[1]=na                 */
	VK_FID_PROC_BUF,  /**< args[0]= cmd, args[1]=buffer handle       */
	VK_FID_XREF_BUF,  /**< args[0]= ref delta, args[1]=buffer handle */
	VK_FID_PRIVATE,   /**< used for internal purpose                 */

	/* function carried by vk2host_msg */
	VK_FID_INIT_DONE,
	VK_FID_DEINIT_DONE,
	VK_FID_SET_PARAM_DONE,
	VK_FID_GET_PARAM_DONE, /**< args[0]=value */
	VK_FID_TRANS_BUF_DONE, /**< if upload: args[0]=buffer handle*/
	VK_FID_PROC_BUF_DONE,  /**< args[0]=result buffer handle*/
	VK_FID_XREF_BUF_DONE,
	VK_FID_PRIVATE_DONE,   /**< used for internal purpose          */
	VK_FID_MAX
} vk_function_id_t;

/** VK shut down type */
typedef enum _vkil_shutdown_type {
	VK_SHUTDOWN_UNDEF     =    0,
	/**
	 * used by driver autonomously when a particular process is gone
	 */
	VK_SHUTDOWN_PID       =    1,
	/**
	 * user requested gracefully park the VK and reset
	 */
	VK_SHUTDOWN_GRACEFUL  =    2,
	VK_SHUTDOWN_TYPE_MAX,
} vkil_shutdown_type;

/**
 * return the str representation of a function id
 */
const char *vkil_function_id_str(uint32_t function_id);

/**
 * return description of a shutdown type
 */
const char *vkil_shutdown_type_str(const vkil_shutdown_type type);

/**
 * return the description of a command
 */
const char *vkil_cmd_str(uint32_t cmd);

/**
 * return command's option string
 */
const char *vkil_cmd_opts_str(uint32_t cmd);

#endif
