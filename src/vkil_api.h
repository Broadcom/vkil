/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(c) 2018 Broadcom
 */

/**
 * @file
 * @brief frontend API for host application (e.g. ffmpeg)
 *
 * This file declares all the structures which need to be exposed to the
 * caller
 */

#ifndef VKIL_API_H__
#define VKIL_API_H__

#include <stdint.h>
#include "vkil_backend.h"

#define VKIL_MAX_AGGREGATED_BUFFERS 17

/** Buffer type descriptor */
typedef enum _vkil_buffer_type {
	VKIL_BUF_UNDEF       =    0,
	VKIL_BUF_META_DATA   =  0x4,
	VKIL_BUF_PACKET      =  0x8,
	VKIL_BUF_SURFACE     = 0x10,
	VKIL_BUF_AG_BUFFERS  = 0x20,
	VKIL_BUF_MAX         = 0xFF
} vkil_buffer_type;

/** flags used by vkil_buffer_packet */
#define VKIL_BUFFER_PACKET_FLAG_EOS 0x1
/** flags used by vkil_buffer_surface */
#define VKIL_BUFFER_SURFACE_FLAG_INTERLACE 0x000001
#define VKIL_BUFFER_SURFACE_FLAG_EOS       0x010000

/**
 * @brief generic descriptor
 *
 * The generic buffer descriptor is common to all the buffer, and describes
 * the _vkil_buffer_type, the handle to the buffer, as well as the
 * component port_id associated to the buffer.
 * @li all vkil buffers are required to have this prefix
 */
typedef struct _vkil_buffer {
	uint32_t handle; /**< handle provided by the valkyrie card */
	uint16_t flags:16;
	uint16_t port_id:8; /**< port_id for the buffer */
	uint16_t type:8;    /**< a _vkil_buffer_type */
	uint64_t user_data; /**< user defined data */
} vkil_buffer;

/**
 * @brief buffer used to store metadata (qpmap, statistic, ssim,... values)
 *
 * the type of metadata transmitted is opaque to this container
 */
typedef struct _vkil_buffer_metadata {
	vkil_buffer prefix;
	uint32_t    size;    /**< meta data size in bytes */
	void        *data;   /**< Pointer to metadata     */
} vkil_buffer_metadata;

/** @brief buffer used to store a bitstream packet */
typedef struct _vkil_buffer_packet {
	vkil_buffer prefix;
	uint32_t size;  /**< size of the packet in byte */
	void     *data; /**< Pointer to the packet start */
} vkil_buffer_packet;

/**
 * @brief 32 bits structure storing a 2D size
 */
typedef union vkil_size_ {
	struct {
		uint32_t  width:16; /**< is to be the 16 lsb */
		uint32_t height:16; /**< is to be the 16 msb */
	};
	uint32_t size;
} vkil_size;

/**
 * @brief buffer surface descriptor
 *
 * the surface descriptor describes a surface as below:
 * @image html SurfaceDescriptor.svg
 * @image latex SurfaceDescriptor.eps
 *
 * the surface can be stored in up to 4 planes, (that is chroma component
 * are always expected to be interleaved, e.g. NV12, NV21 or P010 format
 * are supported as well as Hardware format)
 */
typedef struct _vkil_buffer_surface {
	vkil_buffer prefix; /**< generic buffer descritor */
	vkil_size max_size;
	vkil_size visible_size;
	uint16_t xoffset; /**< Luma x offset */
	uint16_t yoffset; /**< Luma y offset */
	uint16_t format; /** color format and pixel depth */
	uint16_t reserved0; /**< for 32 and 64 bits alignment */
	uint32_t stride[2]; /**< Stride between rows, in bytes */
	void *plane_top[2]; /**< Y, UV top field */
	void *plane_bot[2]; /**< bottom field (interlace only) */
} vkil_buffer_surface;

/**
 * @brief aggregated surface descriptor
 *
 * handle linking to a collection of buffers
 */
typedef struct _vkil_aggregated_buffers {
	vkil_buffer prefix;
	uint32_t    nbuffers; /**< nmbers of aggregated buffers */
	uint32_t    reserved;
	vkil_buffer *buffer[VKIL_MAX_AGGREGATED_BUFFERS];
} vkil_aggregated_buffers;

/**
 * @brief The vkil software context
 *
 * all interaction with the Hardware need to apply to a specific context
 * (general HW monitoring functions need to use the VK_INFO_CTX)
 *
 * @li the sofware context shall be created by invoking _vkil_api::init with
 * the ctx_handle VK_NEW_CTX, and destroyed via _vkil_api::deinit
 * @li the _vkil_context::context_essential descrbing the context role needs to
 * be set by the user, before calling again _vkil_api::deinit
 * @li according to the _vkil_context::context_essential, an HW context will
 * be created (opaque structure)
 */
typedef struct _vkil_context {
	/** context descriptor */
	vkil_context_essential context_essential;
	void       *devctx;    /**< handle to the hw device */
	void       *priv_data; /**< private structure pointer */
} vkil_context;

/**
 * @brief The vkil frontend api (i.e. ffmpeg calls these vkil functions)
 *
 * A typical session invokes
 * @li the creation of a context (_vkil_api::init): this needs usually to
 * be invoked twice, one to allow the creation of the sw vkil context; which
 * then needs to be set by user, and a second time to create the relevant
 * hardware context, which is an opaque pointer from the host viewpoint
 * @li the setting of parameters (_vkil_api::set_parameter) allowing to
 * configure the created context
 * @li the context could require a final initialization step;
 * an additional init call will allow to perform this
 *
 * Then in loop
 * @li upload a buffer to the card (_vkil_api::transfer_buffer) if not already
 * oncard
 * @li process the buffer (_vkil_api::process_buffer)
 * @li download the processed buffer (_vkil_api::transfer_buffer)
 *
 * An example of use is illustrated below:
 * @image html  currentTranscoding_scheme2.svg
 * @image latex currentTranscoding_scheme2.eps
 *
 * @li deinit the context (_vkil_api::deinit)
 *
 * Except for the initialization, all the functions can be either blocking
 * (the VK_CMD_OPT_BLOCKING vkil_command_t is used); return only on HW message
 * response; or non blocking; return immediately after a command message has
 * been sent to the HW.
 * @li In the later case the function needs to be recalled with the
 * VK_CMD_OPT_CB vkil_command_t.
 *
 * In the context of ffmpeg this means the vkil is polling the state of the card
 */
typedef struct _vkil_api {
	/** a new context is created if ctx_handle == VK_NEW_CTX */
	int32_t (*init)(void **ctx_handle);
	/**
	 * Once a denit command is issued, no other command can be sent to the
	 * deinited context
	 */
	int32_t (*deinit)(void **ctx_handle);
	/**
	 * Set the context field with the value passed as argument
	 * (if the field refers to a structure, the value is a pointer to it)
	 */
	int32_t  (*set_parameter)(void *ctx_handle,
				  const vkil_parameter_t field,
				  const void *value,
				  const vkil_command_t cmd);
	/**
	 * get the context field values
	 * (if the field refers to a structure, the value is a pointer to it)
	 */
	int32_t  (*get_parameter)(void *ctx_handle,
				  const vkil_parameter_t field,
				  void *value,
				  const vkil_command_t cmd);
	/**
	 * This function needs to be called for all buffers to transfer to/from
	 * the Valkyrie card (vkil_command_t provides the transfer direction)
	 * @li all buffer transfers are done via DMA
	 * @li the card memory management is under the card control. Typically
	 * an upload infers a memory allocation on the card, and a download a
	 * memory freeing. the vkil sees only opaque handles to a card buffer
	 * descriptor, in no case the host can see the on card used memory
	 * addresses
	 */
	int32_t (*transfer_buffer)(void *ctx_handle,
			       void *buffer_handle,
			       const vkil_command_t cmd);
	/**
	 * the process buffer typically "consumes" the buffer provided in
	 * input; that is free the input buffer which will not be available
	 * anymore; and produces a buffer; malloc a buffer; which can be either
	 * conveyed to another processing; that is calling the same function
	 * again; or retrieved by the host, via the _vkil_api::transfer_buffer
	 * function
	 */
	int32_t (*process_buffer)(void *ctx_handle,
			       void *buffer_handle,
			       const vkil_command_t cmd);
} vkil_api;

extern void *vkil_create_api(void);
extern int vkil_destroy_api(void **ilapi);

extern int vkil_set_affinity(const char *device);
extern const char *vkil_get_affinity(void);

#endif
