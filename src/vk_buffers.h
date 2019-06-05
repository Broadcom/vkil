/* SPDX-License-Identifier: GPL-2.0 OR Apache-2.0 */
/*
 * Copyright(c) 2018 Broadcom
 */

#ifndef VK_BUFFERS_H
#define VK_BUFFERS_H

#include <stdint.h>
#include "vk_parameters.h"

#define VK_SURFACE_MAX_PLANES 4

#ifndef __packed
/*
 * this is not defined in x86 user space, but is defined in x86 kernel
 * and in OS zephyr
 */
#define __packed __attribute__((packed))
#endif


/** flags used by vk_buffer */
#define VK_BUFFER_FLAG_INTERLACE 0x000001
#define VK_BUFFER_FLAG_EOS       0x010000


typedef enum _vk_buffer_type {
	VK_BUF_UNDEF       =    0,
	VK_BUF_METADATA    =  0x4,
	VK_BUF_PACKET      =  0x8,
	VK_BUF_SURFACE     = 0x10,
	VK_BUF_AG_BUFFERS  = 0x20,
	VK_BUF_MAX         = 0xFF
} vk_buffer_type;

/**
 * prefix to be used for all buffer type
 * prefix size is 16 bytes
 */
typedef struct _vk_buffer {
	uint32_t handle;  /* handle to the buffer on Vallkyrie SOC */
	uint32_t flags:16;
	uint32_t port_id:8;
	uint32_t type:8;  /**< buffer type */
	uint64_t user_data_tag;
} vk_buffer;

/**
 * buffer used to store metadata (qpmap, statistic, ssim,... values)
 * the type of metadata transmitted is opaque to this container
 */
typedef struct _vk_buffer_metadata {
	vk_buffer prefix;
	uint32_t  used_size; /**< used  buffer size in bytes */
	uint32_t  size;      /**< total buffer size in bytes */
	uint64_t  data;      /**< Pointer to metadata     */
} vk_buffer_metadata;

typedef struct __packed _vk_data {
	uint32_t    size; /**< data size in bytes */
	uint64_t    address; /**< Pointer to data     */
} vk_data;

typedef struct _vk_buffer_surface {
	vk_buffer prefix;
	vk_size max_size;
	vk_size visible_size;
	uint16_t xoffset; /**< Luma x crop */
	uint16_t yoffset; /**< Luma y crop */
	uint16_t format;  /**< pixel fromat */
	uint16_t quality; /**< quality index */
	uint16_t stride[2]; /**< Stride between rows, in bytes */
	uint32_t reserved1;
	uint64_t reserved2;
	/*
	 * the below is 64 bits aligned and will fall on a 16 bytes
	 * message boundary.
	 */
	vk_data planes[VK_SURFACE_MAX_PLANES]; /* length, address */
} vk_buffer_surface;

typedef struct _vk_buffer_packet {
	vk_buffer prefix;
	uint32_t  used_size; /**< used  buffer size in bytes */
	uint32_t  size;      /**< total buffer size in bytes */
	uint64_t  data;      /**< Pointer to buffer start on Host memory*/
} vk_buffer_packet;

#endif
