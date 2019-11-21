// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright(c) 2019 Broadcom
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "vkil_api.h"

#define PCIE_EYE_BUFF_SIZE_MAX	(16 * 1024)
#define REF_EYE_LANE		0xffffffff

#define MAX_EYE_X		64
#define X_START			(-31)
#define X_END			31
#define STRIPE_SIZE		MAX_EYE_X
#define MAX_EYE_Y		63
#define Y_START			31

#define NR_LIMITS		7
#define NR_CR			5

typedef struct vk_info_pcie_eye_config {
	uint32_t lane;
	uint32_t buffer_handle;
} vk_pcie_eye_config;

typedef struct vk_info_pcie_eye_ctx {
	vkil_api *ilapi;
	vkil_context *ilctx;
	uint8_t *buffer;
	char *dev_id;
} vk_pcie_eye_ctx;

struct vk_pcie_eye_info {
	vk_pcie_eye_ctx ctx;
	vk_pcie_eye_config cfg;
};

struct vk_pcie_eye_info eye_info;

static void pcie_eye_vkil_create_api(vk_pcie_eye_ctx *ctx)
{
	ctx->ilapi = vkil_create_api();
	assert(ctx->ilapi);
	assert(ctx->ilapi->init);
	assert(ctx->ilapi->deinit);
	assert(ctx->ilapi->set_parameter);
	assert(ctx->ilapi->get_parameter);
	assert(ctx->ilapi->transfer_buffer);
}

static void pcie_eye_vkil_deinit(vk_pcie_eye_ctx *ctx)
{
	ctx->ilapi->deinit((void **) &ctx->ilctx);
	assert(!ctx->ilctx);
}

static void pcie_eye_vkil_destroy_api(vk_pcie_eye_ctx *ctx)
{
	vkil_destroy_api((void **) &ctx->ilapi);
	assert(!ctx->ilapi);
}

static void print_usage(void)
{
	printf("Usage: pcie_eye -d device_no -p phy_no -l lane_no\n");
}

static void display_pcie_eye_header(void)
{
	printf("\n");
	printf(" Each character N represents approximate error rate 1e-N at that location\n");
	printf("  UI/64  : -30  -25  -20  -15  -10  -5    0    5    10   15   20   25   30\n");
	printf("         : -|----|----|----|----|----|----|----|----|----|----|----|----|-\n");
}

static void display_pcie_eye_footer(void)
{
	printf("         : -|----|----|----|----|----|----|----|----|----|----|----|----|-\n");
	printf("  UI/64  : -30  -25  -20  -15  -10  -5    0    5    10   15   20   25   30\n");
	printf("\n");
}

/*
 * Magic calculation ported from code from the PCIe Serdes team
 */
static int16_t ladder_setting_to_mV(int8_t ctrl, int range_250)
{
	uint16_t absv = abs(ctrl);
	int16_t nlmv, nlv;

	nlv = 25 * absv;
	if (absv > 22)
		nlv += (absv - 22) * 25;

	if (range_250)
		nlmv = (nlv + 2) / 4;
	else
		nlmv = (nlv * 3 + 10) / 20;
	return ((ctrl >= 0) ? nlmv : -nlmv);
}

static void display_pcie_eye_stripe(uint32_t *buf, int8_t y, int p1_select)
{
	const uint32_t limits[NR_LIMITS] = {917504, 91750, 9175, 917, 91, 9, 1};
	int16_t level;
	int8_t x, i;

	level = ladder_setting_to_mV(y, p1_select);

	printf("%6dmV : ", level);

	for (x = X_START; x <= X_END; x++) {
		for (i = 0; i < NR_LIMITS; i++) {
			if (buf[x + abs(X_START)] >= limits[i]) {
				printf("%c", '0' + i + 1);
				break;
			}
		}

		if (i == NR_LIMITS) {
			if ((x % NR_CR) == 0 && (y % NR_CR) == 0)
				printf("+");
			else if ((x % NR_CR) != 0 && (y % NR_CR) == 0)
				printf("-");
			else if ((x % NR_CR) == 0 && (y % NR_CR) != 0)
				printf(":");
			else
				printf(" ");
		}
	}
	printf("\n");
}

static void display_pcie_eye(uint32_t stripe[MAX_EYE_Y][MAX_EYE_X],
			     uint32_t size, uint32_t lane, int p1_select)
{
	int i, y;
	uint8_t *buf;

	/* Check if reference eye buffer */
	if (lane == REF_EYE_LANE) {
		buf = (uint8_t *)stripe;
		for (i = 0; i < size; i++)
			printf("%c", buf[i]);
		printf("\n");
		return;
	}

	display_pcie_eye_header();

	for (i = 0, y = Y_START; i < MAX_EYE_Y; i++, y--)
		display_pcie_eye_stripe(&stripe[i][0], y, p1_select);

	display_pcie_eye_footer();
}

int main(int argc, char *argv[])
{
	vk_pcie_eye_ctx *ctx = &eye_info.ctx;
	vk_pcie_eye_config *cfg = &eye_info.cfg;
	vkil_buffer_metadata buffer_metadata;
	vkil_api *vkilapi;
	vkil_context *vkilctx;
	uint32_t data;
	int c, ret;
	void *ptr;

	if (argc != 7) {
		printf("Invalid number of args\n");
		print_usage();
		return 0;
	}

	ctx->dev_id = "0";
	while ((c = getopt(argc, argv, "p:l:d:")) != -1) {
		switch (c) {

		case 'p':
			ret = strtoul(optarg, NULL, 0);
			cfg->lane |= (ret << 16);
			printf("PCIe eye diagram: phy_%d\n", ret);
			break;

		case 'l':
			ret = strtoul(optarg, NULL, 0);
			cfg->lane |= 0xffff & ret;
			printf("PCIe eye diagram: lane_%d\n", ret);
			break;

		case 'd':
			ctx->dev_id = optarg;
			printf("PCIe eye diagram: device_%s\n", optarg);
			break;

		default:
			print_usage();
			return 0;
		}
	}

	ret = vkil_set_affinity(ctx->dev_id);
	if (ret != 0) {
		printf("Error in setting the affinity\n");
		goto end;
	}

	pcie_eye_vkil_create_api(ctx);
	vkilapi = ctx->ilapi;
	vkilctx = ctx->ilctx;

	/*
	 * Calling init twice as the ctx initialization is done first and then
	 * the actual init of component.
	 */
	vkilapi->init((void **) &vkilctx);
	assert(vkilctx);
	vkilapi->init((void **) &vkilctx);

	data = cfg->lane;
	ret = vkilapi->get_parameter(
				vkilctx,
				VK_PARAM_PCIE_EYE_DIAGRAM,
				&data,
				VK_CMD_RUN | VK_CMD_OPT_BLOCKING);
	if (ret < 0) {
		printf("PCIe eye diagram failed:%d\n", ret);
		goto end;
	}
	buffer_metadata.prefix.handle = data;

	ret = vkilapi->get_parameter(
				vkilctx,
				VK_PARAM_PCIE_EYE_SIZE,
				&data,
				VK_CMD_RUN | VK_CMD_OPT_BLOCKING);
	if (ret < 0 || data > PCIE_EYE_BUFF_SIZE_MAX) {
		printf("PCIe get eye size failed:%d\n", ret);
		goto end;
	}

	buffer_metadata.data = malloc(data);
	buffer_metadata.prefix.type = VKIL_BUF_META_DATA;
	buffer_metadata.size = data;

	ret = vkilapi->transfer_buffer(
				vkilctx,
				&buffer_metadata,
				(VK_CMD_DOWNLOAD | VK_CMD_OPT_BLOCKING));
	if (ret < 0) {
		printf("Transfer buffer failed:%d\n", ret);
		goto end;
	}

	data = *((int *)buffer_metadata.data);
	ptr = buffer_metadata.data;

	if (cfg->lane != REF_EYE_LANE)
		ptr += sizeof(uint32_t);

	display_pcie_eye(ptr, buffer_metadata.size, cfg->lane, data);

end:
	ctx->ilctx = vkilctx;
	pcie_eye_vkil_deinit(ctx);
	pcie_eye_vkil_destroy_api(ctx);
	if (ctx->buffer)
		free(ctx->buffer);
	return 0;
}
