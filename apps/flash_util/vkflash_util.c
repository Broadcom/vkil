// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright(c) 2018-2019 Broadcom
 */
/**
 * @file
 * @brief Flash Util Application
 *
 * The flash util application handles the writing of flash data into
 * the flash device in Valkyrie board. The application uses the VKIL
 * and INFO component for transferring the flash data from host to
 * the Valkyrie card.
 *
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "vkil_api.h"

#define MAX_FILE_NAME           256
#define MAX_FLASH_NAME          16
#define FLASH_UTIL_APP_NAME     "vkflash_util"
#define FLASH_MAX_BLOCK_SIZE    4096

enum {
	SUCCESS = 0,
	INVARGS = 100,
	INVFILEOPS,
	INVMEMOPS
};

typedef struct _vk_flash_util_ctx {
	vkil_api *ilapi;
	vkil_context *ilctx;
	uint8_t *buffer;
	uint32_t file_size;
	uint32_t start_offset;
	char *dev_id;
} vk_flash_util_ctx;

typedef struct _vk_flash_util_info {
	vk_flash_util_ctx ctx;
	vk_flash_image_cfg cfg;
} vk_flash_util_info_t;

vk_flash_util_info_t vk_flash_util_info;

/**
 * @brief create vkil API context
 * @param ctx pointer to the flash util context
 * @return none
 */
static void flash_util_vkil_create_api(vk_flash_util_ctx *ctx)
{
	ctx->ilapi = vkil_create_api();
	assert(ctx->ilapi);
	assert(ctx->ilapi->init);
	assert(ctx->ilapi->deinit);
	assert(ctx->ilapi->set_parameter);
	assert(ctx->ilapi->get_parameter);
	assert(ctx->ilapi->transfer_buffer);
}

/**
 * @brief de-init the vkil context
 * @param ctx pointer to the flash util context
 * @return none
 */
static void flash_util_vkil_deinit(vk_flash_util_ctx *ctx)
{
	ctx->ilapi->deinit((void **) &ctx->ilctx);
	assert(!ctx->ilctx);
}

/**
 * @brief destroy the vkil API context
 * @param ctx pointer to the flash util context
 * @return none
 */
static void flash_util_vkil_destroy_api(vk_flash_util_ctx *ctx)
{
	vkil_destroy_api((void **) &ctx->ilapi);
	assert(!ctx->ilapi);
}

/**
 * @brief write the data to flash
 * @param util_info pointer to the flash util info
 * @param offset write offset of the flash device
 * @param buffer pointer to the data buffer
 * @param size size of the data to be written
 * @return zero if success otherwise error message
 */
static int32_t flash_write_block(vk_flash_util_info_t *vk_flash_util_info,
				 uint32_t write_offset, uint8_t *buffer,
				 uint32_t block_size)
{
	int32_t ret;
	vk_flash_util_ctx *ctx = &vk_flash_util_info->ctx;
	vk_flash_image_cfg *cfg = &vk_flash_util_info->cfg;
	vkil_api *vkilapi = ctx->ilapi;
	vkil_context *vkilctx = ctx->ilctx;
	vkil_buffer_metadata buffer_metadata;

	buffer_metadata.prefix.type = VKIL_BUF_META_DATA;
	buffer_metadata.data = buffer;
	buffer_metadata.size = block_size;

	printf("Initiate Transfer buffer operation:%p, %d\n", buffer,
	       block_size);
	ret = vkilapi->transfer_buffer(
				vkilctx,
				&buffer_metadata,
				(VK_CMD_UPLOAD | VK_CMD_OPT_BLOCKING));
	if (ret < 0) {
		printf("Transfer buffer failed:%d\n", ret);
		return ret;
	}

	/* Update the Valkyrie Buffer handle to the flash config */
	cfg->buffer_handle = buffer_metadata.prefix.handle;
	cfg->write_offset = write_offset;
	cfg->image_size = block_size;

	/* Set the parameters of flash util and write the data 2 flash */
	ret = vkilapi->set_parameter(
				vkilctx,
				VK_PARAM_FLASH_IMAGE_CONFIG,
				cfg,
				VK_CMD_RUN | VK_CMD_OPT_BLOCKING);
	if (ret < 0) {
		printf("Set parameter failed:%d\n", ret);
		return ret;
	}

	return ret;
}

/**
 * @brief Print the application and parameter usage
 * @return none
 */
static void print_usage(void)
{
	printf("\n Usage:\n");
	printf("%s bin_filename [optional args]\n", FLASH_UTIL_APP_NAME);
	printf("[Optional args]\n");
	printf("-d <device id associated with M.2 card> default is 0\n");
	printf("-o <flash write offset> default is 0\n");
	printf("-t <flash type string> qspi/nand, default is qspi\n");
	printf("-h help/usage\n");
}

/**
 * @brief Application Main
 * @param argc argument count
 * @param argv pointer to the array of argument
 * @return zero if success otherwise error message
 */
int main(int argc, char *argv[])
{
	int32_t ret = SUCCESS;
	vk_flash_util_ctx *ctx = &vk_flash_util_info.ctx;
	vk_flash_image_cfg *cfg = &vk_flash_util_info.cfg;
	char bin_filename[MAX_FILE_NAME];
	char flash_type[MAX_FLASH_NAME];
	int c;
	FILE *bin_filefd = 0;
	uint32_t last_block_size = 0;
	int num_of_blocks;
	int loop;

	if (argc < 2) {
		printf("%s: too few parameters\n", FLASH_UTIL_APP_NAME);
		print_usage();
		ret = -INVARGS;
		goto end;
	}

	strncpy(bin_filename, argv[1], MAX_FILE_NAME);
	bin_filename[MAX_FILE_NAME-1] = '\0';

	/* default values for image type and write offset */
	cfg->image_type = VK_INFO_FLASH_TYPE_QSPI;
	ctx->start_offset = 0;
	ctx->dev_id = "0";

	while ((c = getopt(argc, argv, "d:o:h:t:")) != -1) {
		switch (c) {

		case 'o':
			ret = sscanf(optarg, "%x", &ctx->start_offset);
			if (ret == EOF) {
				ret = -INVARGS;
				goto end;
			}
			printf("flash write offset:%x\n", ctx->start_offset);
			break;

		case 'd':
			/* specify the affinity */
			ctx->dev_id = optarg;
			break;

		case 't':
			if (argc < 3) {
				printf("too few parameters!\n");
				print_usage();
				ret = -INVARGS;
				goto end;
			}

			strncpy(flash_type, optarg, MAX_FLASH_NAME);
			flash_type[MAX_FLASH_NAME-1] = '\0';
			if (strcmp(flash_type, "qspi") == 0)
				cfg->image_type = VK_INFO_FLASH_TYPE_QSPI;
			else if (strcmp(flash_type, "nand") == 0)
				cfg->image_type = VK_INFO_FLASH_TYPE_NAND;
			else {
				printf("Invalid flash type:%s\n", flash_type);
				cfg->image_type = VK_INFO_FLASH_TYPE_INVALID;
			}
			break;

		case 'h':
			print_usage();
			break;
		default:
			ret = -INVARGS;
			goto end;
		}
	}

	bin_filefd = fopen(bin_filename, "rb");
	if (bin_filefd == 0) {
		printf("Error in opening the bin file:%s,%p\n",
		       bin_filename,
		       bin_filefd);
		ret = -INVFILEOPS;
		goto end;
	}

	if (fseek(bin_filefd, 0, SEEK_END) != 0) {
		printf("Error in fseek on bin file:%s,%p,%d\n", bin_filename,
		       bin_filefd,
		       ferror(bin_filefd));
		ret = -INVFILEOPS;
		goto end;
	}

	ctx->file_size = ftell(bin_filefd);

	if (fseek(bin_filefd, 0, SEEK_SET) != 0) {
		printf("Error in fseek on bin file:%s,%p,%d\n", bin_filename,
		       bin_filefd,
		       ferror(bin_filefd));
		ret = -INVFILEOPS;
		goto end;
	}

	if (ctx->file_size == 0) {
		printf("Invalid bin file size for file:%s\n", bin_filename);
		ret = -INVFILEOPS;
		goto end;
	}

	ctx->buffer = malloc(ctx->file_size);
	if (!ctx->buffer) {
		printf("Error in allocating buffer for file\n");
		ret = -INVMEMOPS;
		goto end;
	}

	ret = fread(ctx->buffer, ctx->file_size, 1, bin_filefd);

	if (ret < 1) {
		printf("Error in reading data from file:%s,%d\n",
		       bin_filename, ret);
		ret = -INVFILEOPS;
		goto end;
	}

	printf("Starting the flasher test...\n");
	printf("flash type:%d, write_offset:%d, image_size:%d\n",
	       cfg->image_type, ctx->start_offset, ctx->file_size);

	ret = vkil_set_affinity(ctx->dev_id);
	if (ret != 0) {
		printf("Error in setting the affinity %d\n", ret);
		goto end;
	}

	flash_util_vkil_create_api(ctx);

	/*
	 * Calling init twice as the ctx initialization is done first and then
	 * the actual init of component.
	 */
	ctx->ilapi->init((void **) &ctx->ilctx);
	assert(ctx->ilctx);
	ctx->ilapi->init((void **) &ctx->ilctx);

	/*
	 * Break the transfer into small size blocks to minimize larger memory
	 * allocation in the Valkyrie
	 */
	num_of_blocks = ctx->file_size / FLASH_MAX_BLOCK_SIZE;
	last_block_size = ctx->file_size % FLASH_MAX_BLOCK_SIZE;
	if (last_block_size)
		num_of_blocks++;

	for (loop = 0; loop < num_of_blocks; loop++) {
		uint8_t *buffer = (ctx->buffer + (loop * FLASH_MAX_BLOCK_SIZE));
		uint32_t block_size = FLASH_MAX_BLOCK_SIZE;
		uint32_t write_offset = ctx->start_offset + (loop * block_size);

		if ((loop + 1) == num_of_blocks)
			block_size = last_block_size;
		if (block_size > 0) {
			ret = flash_write_block(&vk_flash_util_info,
						write_offset, buffer,
						block_size);
			if (ret < 0) {
				printf("Transfer/set buffer failed:%d\n", ret);
				goto end;
			}
		}
	}

	flash_util_vkil_deinit(ctx);
	flash_util_vkil_destroy_api(ctx);
	printf("Flash Update complete\n");

	if (ctx->buffer)
		free(ctx->buffer);

	ret = 0;
end:
	if (bin_filefd)
		fclose(bin_filefd);

	return ret;
}
