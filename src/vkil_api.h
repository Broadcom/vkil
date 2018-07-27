/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(c) 2018 Broadcom
 */

#ifndef VKIL_API_H__
#define VKIL_API_H__

/*
 * this declaration file is to be called by
 * the vkapi layer (embedded into ffmpeg)
 */

#include <stdint.h>
#include "vkil_backend.h"

/* vkil structures are for the time being identical to the vk structures */
typedef struct  _vk_buffer_packet  vkil_buffer_packet;
typedef struct  _vk_buffer_surface vkil_buffer_surface;

typedef struct _vkil_context {
	vkil_context_essential context_essential;
	void       *priv_data; /**< component dependent */
} vkil_context;

/**
 * The vkil frontend api
 * (e.g ffmpeg call these vkil functions)
 */
typedef struct _vkil_api {
	/** a new context is created if ctx_handle == VK_NEW_CTX */
	int32_t (*init)(void **ctx_handle);
	int32_t (*deinit)(void **ctx_handle);
	int32_t  (*set_parameter)(const void *ctx_handle, const int32_t field,
						      const void *value,
						      const vkil_command_t cmd);
	int32_t  (*get_parameter)(const void *ctx_handle, const int32_t field,
						      void **value,
						      const vkil_command_t cmd);

	int32_t (*send_buffer)(const void *ctx_handle,
			       const void *buffer_handle,
			       const vkil_command_t cmd);
	int32_t (*receive_buffer)(const void *ctx_handle,
				  void **buffer_handle,
				  const vkil_command_t cmd);

	/*
	 * the below functions are intended to be deprecated,
	 * DMA operation is now expected to be trigerred by
	 * the send_buffer and receive_buffer with relevant
	 * cmd (VK_CMD_UPLOAD or VK_CMD_DOWLOAD)
	 */

	/* start dma operation */
	int32_t (*upload_buffer)(const void *ctx_handle,
				 const void *host_buffer,
				 const vkil_command_t cmd);
	int32_t (*download_buffer)(const void *ctx_handle,
				   void **host_buffer,
				   const vkil_command_t cmd);

	/* poll dma operation status */
	int32_t (*uploaded_buffer)(const void *ctx_handle,
				   const void *host_buffer,
				   const vkil_command_t cmd);
	int32_t (*downloaded_buffer)(const void *ctx_handle,
				     const void *host_buffer,
				     const vkil_command_t cmd);
} vkil_api;

extern void *vkil_create_api(void);
extern int vkil_destroy_api(void **ilapi);

#endif
