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

typedef enum _vk_buffer_type {
	VK_BUF_UNDEF       =    0,
	VK_BUF_METADATA    =  0x1,
	VK_BUF_PACKET      =  0x2,
	VK_BUF_SURFACE     = 0x4,
	VK_BUF_AG_BUFFERS  = 0x8,
	VK_BUF_MAX         = 0xf
} vk_buffer_type;

/**
 * prefix to be used for all buffer type
 * prefix size is 16 bytes
 */
typedef struct _vk_buffer {
	uint32_t handle;    /**< handle to the buffer on the SOC */
	uint32_t flags:16;  /**< flags */
	uint32_t type:4;    /**< buffer type */
	uint32_t reserved:4;    /**< for alignment purpose */
	uint32_t port_id:8; /**< port associated to the buffer */
	uint64_t user_data_tag; /**< associated user data */
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

/*
 * common macros
 */

/* return arg will have 24 bits for size and 8 bits for flags */
#define VK_SIZE_POS 0
#define VK_FLAG_POS 24
#define VK_SIZE_MASK 0xffffff
#define VK_FLAG_MASK 0Xff

/**
 * A SSIM result is provided for each point of a 4x4 grid (labelled "0");
 * offseted by 2x2 pels from the top left; except the rightmost column or
 * bottom most row of the grid (labelled "X"). The SSIM itself is computed
 * on a 8x8 block
 *  ------------------SURF_SZ-------------------
 *  | x x x x x x x x x x x ... x x x x x x x x|
 *  | x x x x x x x x x x x ... x x x x x x x x|
 *  | x x 0 x x x 0 x x x 0 ... 0 x x x X x x x|
 *  | x x x x x x x x x x x ... x x x x x x x x|
 *  | x x x x x x x x x x x ... x x x x x x x x|
 *  | x x x x x x x x x x x ... x x x x x x x x|
 *  | x x 0 x x x 0 x x x 0 ... 0 x x x X x x x|
 *  | x x x x x x x x x x x ... x x x x x x x x|
 *  | x x x x x x x x x x x ... x x x x x x x x|
 *  | x x x x x x x x x x x ... x x x x x x x x|
 *  | x x 0 x x x 0 x x x 0 ... 0 x x x X x x x|
 *  | ........................................ |
 *  | x x 0 x x x 0 x x x 0 ... 0 x x x x x x x|
 *  | x x x x x x x x x x x ... x x x x x x x x|
 *  | x x x x x x x x x x x ... x x x x x x x x|
 *  | x x x x x x x x x x x ... x x x x x x x x|
 *  | x x X x x x X x x x X ... X x x x X x x x|
 *  | x x x x x x x x x x x ... x x x x x x x x|
 *  | x x x x x x x x x x x ... x x x x x x x x|
 *  | x x x x x x x x x x x ... x x x x x x x x|
 *  --------------------------------------------
 * The formula below then gives the number of these point per direction
 */
#define NUM_4x4_GRID_PT(surf_sz) (((surf_sz) / 4) - 1)

#define SB_ROUNDUP(log2_sb_sz) ((1 << (log2_sb_sz)) - 1)

/*
 * then the result can be aggregated in super block, which size is expressed in
 * number of point on which the SSIM result wil be aggregated
 * (either average of the atomic result or minimum (worst) value)
 * the number of super block per direction is then expressed as below
 * where number of SB is rounded up (means it will be partial super blocks)
 */
#define NUM_SB(sz, log2_sb) ((NUM_4x4_GRID_PT(sz) + SB_ROUNDUP((log2_sb))) \
			     >> (log2_sb))

#endif
