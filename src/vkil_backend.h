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

#define VK_START_VALID_HANDLE 0x400

/** Bit width of the msg_id field in the host2vk_msg and vk2host_msg structs */
#define MSG_ID_BIT_WIDTH 12

/**
 * message structure from host to Valkyrie card
 */
typedef struct _host2vk_msg {
	/** this refers to a function listed in vkil_fun_list */
	uint8_t function_id;
	uint8_t size;           /**< message size is 16*(1+size) bytes */
	uint16_t queue_id:4 ;   /**< this provide the input queue index */
	/** unique message identifier in the queue */
	uint16_t msg_id:MSG_ID_BIT_WIDTH;
	uint32_t context_id;    /**< handle to the HW context */
	union {
		uint32_t args[2]; /**< generic argument list taken by the function */
		/** < cmd definition */
		struct _cmd {
			uint32_t val;
			uint32_t arg;
		} cmd;
		/** < field for parameter get/set */
		struct _field {
			uint32_t idx;
			uint32_t val;
		} field;
		/** < buffer referencing structure */
		struct _ref {
			int32_t delta;
			uint32_t buf;
		} ref;
		/** < error indication */
		struct _err {
			uint32_t state;
			int32_t ret;
		} err;
	};
} host2vk_msg;
#define VKMSG_CMD(p_msg)       ((p_msg)->cmd.val)
#define VKMSG_CMD_ARG(p_msg)   ((p_msg)->cmd.arg)
#define VKMSG_FIELD(p_msg)     ((p_msg)->field.idx)
#define VKMSG_FIELD_VAL(p_msg) ((p_msg)->field.val)
#define VKMSG_REF_DELTA(p_msg) ((p_msg)->ref.delta)
#define VKMSG_REF_BUF(p_msg)   ((p_msg)->ref.buf)
#define VKMSG_ERR_IND(p_msg)   ((p_msg)->err.state)
#define VKMSG_ERR_RET(p_msg)   ((p_msg)->err.ret)

/**
 * get pointer to start of extra data in msg
 *
 * @params msg pointer to message
 * @return pointer to extra data in msg
 */
static inline void *host2vk_getdatap(host2vk_msg *msg)
{
	return (void *)(msg + 1);
}

/**
 * message structure from Valkyrie card to host
 */
typedef struct _vk2host_msg {
	/** this refers to a function listed in vkil_fun_list */
	uint8_t function_id;
	uint8_t size;           /**< message size is 16*(1+size) bytes */
	uint16_t queue_id:4 ;   /**< this match the host2vk_msg (queue_id) */
	/** this match the host2vk_msg (msg_id) */
	uint16_t msg_id:MSG_ID_BIT_WIDTH;
	uint32_t context_id;    /**< handle to the HW context */
	/** return hw status. if ERROR, error_code carries in arg */
	uint32_t hw_status;
	uint32_t arg;           /**< return argument (depend on function) */
} vk2host_msg;

/**
 * get pointer to start of extra data in msg
 *
 * @params msg pointer to message
 * @return pointer to extra data in msg
 */
static inline uint32_t *vk2host_getdatap(vk2host_msg *msg)
{
	return (uint32_t *)(msg + 1);
}

/**
 * get pointer to start of arg in msg
 *
 * @params msg pointer to message
 * @return pointer to arg in msg
 */
static inline uint32_t *vk2host_getargp(vk2host_msg *msg)
{
	return &msg->arg;
}

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
	VK_FID_TRANS_BUF = 5, /**< msg[1] = host buffer desc */
	VK_FID_SHUTDOWN  = 8, /**< shut down command */
	/* end of un-mutables */

	/**
	 * If the context is set to VK_NEW_CTX,
	 * a new context is created and handle returned by init_done
	 */
	VK_FID_INIT,
	VK_FID_DEINIT,
	VK_FID_SET_PARAM, /**< field.idx = field, field.val = set val */
	VK_FID_GET_PARAM, /**< field.idx = field, field.val = na      */
	VK_FID_PROC_BUF,  /**< cmd.val = cmd, cmd.arg = buffer handle */
	VK_FID_XREF_BUF,  /**< ref.delta = delta, ref.arg = buffer handle */
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

/* msg size is expressed in multiple of 16 bytes */
#define MSG_SIZE(size) (((size) + sizeof(host2vk_msg) - 1) \
			/ sizeof(host2vk_msg))

#endif
