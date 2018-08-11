/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(c) 2018 Broadcom
 */

#ifndef VKIL_API_H__
#define VKIL_API_H__

/*
 * this declaration file is to be called by
 * the vkapi layer (embedded into ffmpeg)
 * or any other application
 */

#include <stdint.h>
#include "vkil_backend.h"

typedef enum _vkil_buffer_type {
	VK_BUF_UNDEF     =    0,
	VK_BUF_PACKET    =  0x8,
	VK_BUF_SURFACE   = 0x10,
	VK_BUF_MAX	 = 0xFF
} vkil_buffer_type;


/* all vkil buffer share the same prefix */
typedef struct _vkil_buffer {
	uint32_t handle; /**< handle provided by the vk card */
	uint32_t user_data_tag;
	uint32_t flags:24;
	uint32_t type:8; /**< buffer type */
} vkil_buffer;

/* flags used by vkil_buffer_packet */
#define VKIL_BUFFER_PACKET_FLAG_EOS 0x1

typedef struct _vkil_buffer_packet {
	uint32_t handle; /**< handle provided by the vk card */
	uint32_t user_data_tag;
	uint32_t flags:24;
	uint32_t type:8; /**< buffer type */
	uint32_t size;   /**< size of packet in byte */
	void     *data;   /**< Pointer to buffer start */
} vkil_buffer_packet;

/* flags used by vkil_buffer_surface */
#define VKIL_BUFFER_SURFACE_FLAG_INTERLACE 0x000001
#define VKIL_BUFFER_SURFACE_FLAG_EOS       0x010000

/* vkil_buffer_surface format type including pixel depth */
typedef enum _vkil_format_type {
	VKIL_FORMAT_UNDEF = 0,
	VKIL_FORMAT_YOL8,	 /**< hw surface 8 bits  */
	VKIL_FORMAT_YOL10,	 /**< hw surface 10 bits */
	VKIL_FORMAT_YUV420_NV12, /**< sw surface 8 bits  */
	VKIL_FORMAT_YUV420_P010, /**< sw surface 10 bits */
	VKIL_FORMAT_MAX = 0xFFFF, /**< format type is encoded on 16 bytes */
} vkil_format_type;


typedef struct _vkil_buffer_surface {
	uint32_t handle; /**< handle provided by the vk card */
	uint32_t user_data_tag;
	uint32_t flags:24;
	uint32_t type:8; /**< buffer type */
	uint16_t max_frame_width;
	uint16_t max_frame_height;
	uint16_t xoffset; /**< Luma x crop */
	uint16_t yoffset; /**< Luma y crop */
	uint16_t visible_frame_height;
	uint16_t visible_frame_width;
	uint16_t format; /** color format and pixel depth */
	uint16_t reserved0; /**< for 32 and 64 bits alignment */
	uint32_t stride[2]; /**< Stride between rows, in bytes */
	void     *plane_top[2]; /**< Y, UV top field */
	void     *plane_bot[2]; /**< bottom field (interlace only) */
} vkil_buffer_surface;


typedef struct _vkil_context {
	vkil_context_essential context_essential;
	void       *devctx;   /**< handle to the hw device */
	void       *priv_data; /**< component dependent     */
} vkil_context;

/**
 * The vkil frontend api
 * (e.g ffmpeg call these vkil functions)
 */
typedef struct _vkil_api {
	/** a new context is created if ctx_handle == VK_NEW_CTX */
	int32_t (*init)(void **ctx_handle);
	int32_t (*deinit)(void **ctx_handle);
	int32_t  (*set_parameter)(void *ctx_handle,
				  const vkil_parameter_t field,
				  const void *value,
				  const vkil_command_t cmd);
	int32_t  (*get_parameter)(void *ctx_handle,
				  const vkil_parameter_t field,
				  void *value,
				  const vkil_command_t cmd);
	int32_t (*transfer_buffer)(void *ctx_handle,
			       void *buffer_handle,
			       const vkil_command_t cmd);

	/*
	 * the below functions are intended to be deprecated,
	 * DMA operation is now expected to be trigerred by
	 * the transfer_buffer function with relevant
	 * cmd (VK_CMD_UPLOAD or VK_CMD_DOWLOAD)
	 */
	int32_t (*send_buffer)(void *ctx_handle,
			       void *buffer_handle,
			       const vkil_command_t cmd);
	int32_t (*receive_buffer)(void *ctx_handle,
			       void *buffer_handle,
			       const vkil_command_t cmd);
} vkil_api;

extern void *vkil_create_api(void);
extern int vkil_destroy_api(void **ilapi);

#endif
