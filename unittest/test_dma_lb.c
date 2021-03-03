// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright(c) 2018-2019 Broadcom
 */
/**
 * @file
 * @brief DMA loopback test Application
 *
 * This DMA loopback test will perform a number DMA upload followed
 * by the download to/from the Valkyrie card.
 */

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "vk_logger.h"
#include "vkil_api.h"

/* log module for this file */
#define __VK_LOCAL_LOG_MOD        VK_LOG_MOD_GEN

#define LOCAL_LOG(...) vk_log(__func__, __VK_LOCAL_LOG_MOD, \
			      LOG_TYPE_INT, __VA_ARGS__)

/* macro to calculate bandwidth in kbps */
#define _CALC_BW(_bytes, _time_ns) \
	(((((uint64_t)(_bytes)) * 8) * (1000000ULL)) / ((uint64_t)_time_ns))

/* testing parameters */
typedef struct _test_dma_lb_param {
	char *dev_id; /* which card */
	uint8_t *upload_buf;
	uint8_t *download_buf;
	uint8_t v_pattern; /* just a step for increasing pattern */
	uint32_t q_no;
	uint32_t test_size;
	uint32_t dmacnt; /* number of times of buffer transaction */
} test_dma_lb_param;

typedef struct _test_dma_lb_ctx {
	vkil_api *ilapi;
	vkil_context *ilctx;
} test_dma_lb_ctx;

/**
 * @brief create vkil API context
 * @param ctx pointer to the test dma context
 * @return none
 */
static void test_dma_lb_vkil_create_api(test_dma_lb_ctx *ctx)
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
 * @brief destroy the vkil API context
 * @param ctx pointer to the test dma context
 * @return none
 */
static void test_dma_lb_vkil_destroy_api(test_dma_lb_ctx *ctx)
{
	vkil_destroy_api((void **) &ctx->ilapi);
	assert(!ctx->ilapi);
}

/**
 * @brief de-init the vkil context
 * @param ctx pointer to the test dma context
 * @return none
 */
static void test_dma_lb_vkil_deinit(test_dma_lb_ctx *ctx)
{
	ctx->ilapi->deinit((void **) &ctx->ilctx);
	assert(!ctx->ilctx);
}

/**
 * this function initializes the test scope wide variables
 * @param pointer to the parameter block
 * @return 0 on success, -1 on error
 */
int32_t test_param_init(test_dma_lb_param *param)
{
	uint32_t i;
	uint8_t val;

	/*
	 * allocate buffer with extra alignment in case the returned buffer
	 * is not aligned.
	 */
	param->upload_buf = calloc(1, param->test_size);
	if (!param->upload_buf)
		return -1;

	param->download_buf = calloc(1, param->test_size);
	if (!param->download_buf)
		return -1;

	/*
	 * initialize the buffer to a known pattern - no plan to make
	 * it fancy for now, may change later
	 */
	if (param->v_pattern) {
		uint8_t *pval = (uint8_t *)param->upload_buf;

		val = param->v_pattern;
		for (i = 0; i < param->test_size; i++) {
			*pval++ = val;
			val += param->v_pattern;
		}
	}

	/* log a message to info user */
	LOCAL_LOG(VK_LOG_INFO, "Parameters for running....");
	LOCAL_LOG(VK_LOG_INFO,
		  "Dev %s Q_tot[%d] Size 0x%x(%d) Tot %d - verify pattern 0x%x",
		  param->dev_id, param->q_no,
		  param->test_size, param->test_size,
		  param->dmacnt,
		  param->v_pattern);
	LOCAL_LOG(VK_LOG_INFO,
		  "Pat: 0x%x 0x%x 0x%x 0x%x...",
		  param->upload_buf[0], param->upload_buf[1],
		  param->upload_buf[2], param->upload_buf[3]);
	return 0;
}

/**
 * @brief local routine to calculate time between two timespec
 * @param[in] start_tm start time
 * @param[in] end_tm end time
 * @return elapsed time in ns
 */
static inline uint32_t elapsed_time(const struct timespec *start_tm,
				    const struct timespec *end_tm)
{
	uint32_t delta;
#define _NS_IN_ONE_SEC 1000000000UL

	if (start_tm->tv_nsec > end_tm->tv_nsec) {
		delta = _NS_IN_ONE_SEC + end_tm->tv_nsec - start_tm->tv_nsec;
		delta += (end_tm->tv_sec - start_tm->tv_sec - 1)
			 * _NS_IN_ONE_SEC;
	} else {
		delta = end_tm->tv_nsec - start_tm->tv_nsec;
		delta += (end_tm->tv_sec - start_tm->tv_sec) * _NS_IN_ONE_SEC;
	}
	return delta;
}

/**
 * @brief send one packet and receive the loopbacked packet
 * @param loop current loop count
 * @param ctx test dma context
 * @param param pointer to the test parameter
 * @return zero if success otherwise error message
 */
static int32_t test_dma_lb_one_packet(uint32_t loop,
				      test_dma_lb_ctx *ctx,
				      test_dma_lb_param *param,
				      uint32_t *upload_time,
				      uint32_t *download_time)
{
	vkil_api *vkilapi = ctx->ilapi;
	vkil_context *vkilctx = ctx->ilctx;
	vkil_buffer_metadata buffer_metadata;
	int32_t ret = -EINVAL;
	uint32_t i;
	struct timespec start_tm, end_tm;

	buffer_metadata.prefix.handle = 0;
	buffer_metadata.prefix.type = VKIL_BUF_META_DATA;
	buffer_metadata.data = (void *)param->upload_buf;
	buffer_metadata.size = param->test_size;
	buffer_metadata.used_size = param->test_size;
	clock_gettime(CLOCK_MONOTONIC, &start_tm);
	ret = vkilapi->transfer_buffer(
		     vkilctx, &buffer_metadata,
		     (VK_CMD_UPLOAD | VK_CMD_OPT_BLOCKING | VK_CMD_OPT_DMA_LB));

	if (ret)
		goto fail_exit;
	clock_gettime(CLOCK_MONOTONIC, &end_tm);
	*upload_time = elapsed_time(&start_tm, &end_tm);

	LOCAL_LOG(VK_LOG_INFO, "[%d]Returned Handle 0x%x, Upload time %d ns",
		  loop, buffer_metadata.prefix.handle, *upload_time);

	/* zero returned buffer before retrival */
	memset(param->download_buf, 0, param->test_size);

	buffer_metadata.data = (void *)param->download_buf;
	buffer_metadata.size = param->test_size;

	clock_gettime(CLOCK_MONOTONIC, &start_tm);
	ret = vkilapi->transfer_buffer(
		   vkilctx, &buffer_metadata,
		   (VK_CMD_DOWNLOAD | VK_CMD_OPT_BLOCKING | VK_CMD_OPT_DMA_LB));
	if (ret)
		goto fail_exit;
	clock_gettime(CLOCK_MONOTONIC, &end_tm);
	*download_time = elapsed_time(&start_tm, &end_tm);

	LOCAL_LOG(VK_LOG_INFO, "[%d]Returned 0x%x, Download time %d",
		  loop, buffer_metadata.prefix.handle, *download_time);

	/* do a verification on the pattern */
	for (i = 0; i < param->test_size; i++) {

		if (param->upload_buf[i] != param->download_buf[i]) {

			int j, idx;
			uint8_t orig_upload, orig_download;

			orig_upload = param->upload_buf[i];
			orig_download = param->download_buf[i];

			/* print out some info */
			LOCAL_LOG(
			   VK_LOG_WARNING,
			   "Pattern mismatch at location %x, src 0x%x dst 0x%x",
			   i, orig_upload, orig_download);

			/* delay a bit and see if it recovered */
			usleep(10);
			if (param->upload_buf[i] == param->download_buf[i]) {
				/* recovered */
				LOCAL_LOG(VK_LOG_WARNING,
					  "Pattern recovered at location %x, "
					  "[0x%x 0x%x] -> [0x%x 0x%x] with delay",
					  i, orig_upload, orig_download,
					  param->upload_buf[i],
					  param->download_buf[i]);
				continue;
			}

			/* print out some info */
			LOCAL_LOG(
			   VK_LOG_ERROR,
			   "Pattern mismatch at location %x, src 0x%x dst 0x%x",
			   i, param->upload_buf[i], param->download_buf[i]);

			/* use raw printf to dump out */
			for (j = -32; j < 32; j++) {
				idx = i + j;
				if ((idx >= 0) && (idx < param->test_size)) {
					if (j == 0)
						printf("--");
					printf("%x[%x]", param->upload_buf[idx],
					       param->download_buf[idx]);
				}
			}
			printf("\n");

			ret = -EIO;
			break;
		}
	}

fail_exit:
	return ret;
}

/**
 * main testing unit entry
 */
int main(int argc, char **argv)
{
	int c;
	int option_index;
	int32_t ret = -1;
	uint32_t i;
	uint32_t upload_time;
	uint64_t upload_cnt = 0;
	uint32_t download_time;
	uint64_t download_cnt = 0;
	uint64_t upload_tot_ns = 0;
	uint64_t download_tot_ns = 0;
	uint64_t bw;

	test_dma_lb_ctx ctx;
	test_dma_lb_param test_param;
	static struct option long_options[] = {
		{"dev", required_argument, 0, 'd'},
		{"verify", required_argument, 0, 'v'},
		{"bufsize", required_argument, 0, 's'},
		{"dmacnt", required_argument, 0, 'c'},
		{"qno", required_argument, 0, 'q'},
		{"poll_us", required_argument, 0, 'p'},
		{0, 0, 0, 0}
	};

	memset(&test_param, 0, sizeof(test_param));

	/*
	 * Check and get device if user input it, we support
	 *  -d <device>, --dev <device> or --dev=<device>
	 */
	test_param.dev_id = "0";
	test_param.q_no = 1;
	test_param.v_pattern = 0x0; /* default pattern */
	test_param.test_size = 0x10000;
	test_param.dmacnt = 1;	 /* only do 1 buffer DMA transfer total */

	while ((c = getopt_long(argc, argv, "c:d:q:s:v:",
				long_options, &option_index)) != -1) {
		switch (c) {
		case 'c':
			test_param.dmacnt = strtoul(optarg, NULL, 0);
			break;
		case 'd':
			/* specify the affinity if so */
			test_param.dev_id = optarg;
			break;
		case 'q':
			test_param.q_no = strtoul(optarg, NULL, 0);
			break;
		case 's':
			test_param.test_size = strtoul(optarg, NULL, 0);
			break;
		case 'v':
			test_param.v_pattern = strtoul(optarg, NULL, 0);
			break;

		default:
			printf("%c Not supported", c);
			return -1;
		}
	}

	memset(&ctx, 0, sizeof(ctx));
	/* first, create API which provides the logger here */
	test_dma_lb_vkil_create_api(&ctx);

	/* set affinity to start, this will pick which card to use */
	if (vkil_set_affinity(test_param.dev_id)) {
		LOCAL_LOG(VK_LOG_INFO, "Set affinity failure.");
		return -EINVAL;
	}

	if (test_param_init(&test_param)) {
		LOCAL_LOG(VK_LOG_INFO, "Test init failure, dev->%s.",
			  test_param.dev_id);
		ret = -1;
		goto free_and_exit;
	}

	/* init context */
	ret = ctx.ilapi->init((void **) &ctx.ilctx);
	if (ret)
		goto init_fail;

	assert(ctx.ilctx);
	ret = ctx.ilapi->init((void **) &ctx.ilctx);
	if (ret)
		goto init_fail;

	LOCAL_LOG(VK_LOG_INFO,
		  "Dev %s DMA Loopback Test started", test_param.dev_id);

	/* do a for loop for packets */
	for (i = 0; i < test_param.dmacnt; i++) {
		ret = test_dma_lb_one_packet(i, &ctx, &test_param,
					     &upload_time, &download_time);
		if (ret) {
			LOCAL_LOG(VK_LOG_INFO,
				  "Test fails at loop %d, error %d(%s)",
				  i, ret, strerror(-ret));
			break;
		}

		if (upload_time) {
			upload_tot_ns += upload_time;
			upload_cnt++;
		}
		if (download_time) {
			download_tot_ns += download_time;
			download_cnt++;
		}
	}

	test_dma_lb_vkil_deinit(&ctx);

free_and_exit:
	/* free memory */
	free(test_param.upload_buf);
	free(test_param.download_buf);

init_fail:
	LOCAL_LOG(VK_LOG_INFO, "test %s\n", (ret == 0) ?
					    "successful" : "fails");
	if (upload_cnt) {
		upload_cnt *= test_param.test_size;
		bw = _CALC_BW(upload_cnt, upload_tot_ns);

		LOCAL_LOG(VK_LOG_INFO,
			  "\t Aver Upload: total bytes %" PRIu64
			  " tot time %" PRIu64
			  " ns, %" PRIu64 " kbps",
			  upload_cnt,
			  upload_tot_ns,
			  bw);
	}
	if (download_cnt) {
		download_cnt *= test_param.test_size;
		bw = _CALC_BW(download_cnt, download_tot_ns);

		LOCAL_LOG(VK_LOG_INFO,
			  "\t Aver Download: total bytes %" PRIu64
			  " time %" PRIu64
			  " ns, %" PRIu64 " kbps",
			  download_cnt,
			  download_tot_ns,
			  bw);
	}

	test_dma_lb_vkil_destroy_api(&ctx);
	return ret;
};
