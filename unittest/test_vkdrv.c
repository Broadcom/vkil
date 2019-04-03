// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright © 2005-2019 Broadcom. All Rights Reserved.
 * The term “Broadcom” refers to Broadcom Inc. and/or its subsidiaries
 */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "vk_buffers.h"
#include "vk_logger.h"
#include "vkil_backend.h"
#include "vkil_utils.h"

/* log module for this file */
#define __VK_LOCAL_LOG_MOD        VK_LOG_MOD_GEN

#define LOCAL_LOG(...) vk_log(__func__, __VK_LOCAL_LOG_MOD, \
			      LOG_TYPE_INT, __VA_ARGS__)

#define VK_MSG_MAX_SIZE           8

/* use upper 4 nibbles as indicator of error */
#define _IS_INVALID_HANDLE(_h)    ((_h & 0xFFFF0000) == 0xFFFF0000)
/* default poll */
#define _DEF_POLL_US              50000

/* testing parameters */
typedef struct _test_vkdrv_param {
	char *upload_buf;
	char *download_buf;
	bool run_lb;
	uint8_t v_pattern; /* just a step for increasing pattern */
	uint32_t q_no;
	uint32_t test_size;
	uint32_t alloc_size;
	uint32_t dmacnt; /* number of times of buffer transaction */

	uint32_t enc_standard;
	uint32_t enc_profile;
	vk_size  enc_size;

	uint32_t dec_standard;
	uint32_t dec_profile;
	vk_size  dec_size;
} test_vkdrv_param;

/* polling value for reading the message back o*/
static uint32_t test_rsp_poll_us;

/* str version of cmpt */
static inline const char *vksim_cmpt_role2str(vkil_role_t role)
{
	const char *_strlist[VK_SCALER+1] = {
		[VK_INFO]    = "info",
		[VK_DMA]     = "dma",
		[VK_DECODER] = "decoder",
		[VK_ENCODER] = "encoder",
		[VK_SCALER]  = "scaler"
	};

	if (role < ARRAY_SIZE(_strlist))
		return _strlist[role];

	return "N/A";
}

/**
 * this function initializes the test scope wide variables
 * @param pointer to the parameter block
 * @return 0 on success, -1 on error
 */
int32_t test_param_init(test_vkdrv_param *param)
{
	uint32_t i;
	uint8_t  val;
	uint32_t adj_size;
	uint32_t *p_size;

	/* set up some encoder parameters */
	param->enc_standard = VK_V_STANDARD_H264;
	param->enc_profile = (VK_V_PROFILE_H264_MAIN << 16) |
			      VK_V_LEVEL_H264_3;
	param->enc_size.width = 160;
	param->enc_size.height = 120;

	param->dec_standard = VK_V_STANDARD_H264;
	param->dec_profile = (VK_V_PROFILE_H264_HIGH << 16) |
			      VK_V_LEVEL_H264_51;
	param->dec_size.width = 640;
	param->dec_size.height = 480;

	/*
	 * Need to scale the width/height so that the encoder buffer
	 * will be big enough for the specified test_size
	 * For the encoder surface, we use the following which
	 * corresponds to the YOL2
	 */
#define ENC_LIMIT(height, width) (((height) >> 1) * ((width) << 2))

	adj_size = ENC_LIMIT(param->enc_size.height, param->enc_size.width);
	while (adj_size < param->test_size) {
		param->enc_size.width *= 2;
		param->enc_size.height *= 2;
		adj_size = ENC_LIMIT(param->enc_size.height,
				     param->enc_size.width);

		LOCAL_LOG(VK_LOG_WARNING,
			  "Re-adj height,width to %d,%d, adjusted size 0x%x",
			  param->enc_size.height, param->enc_size.width,
			  adj_size);
	}

	/* Readjust the test size to meet the surface agreement */
	param->alloc_size = adj_size;

	/* adjust test size if needed */
	adj_size = (adj_size * 3) >> 3; /* limit of output buf */
	if (param->test_size > adj_size) {
		LOCAL_LOG(VK_LOG_WARNING, "Test_size shrink from 0x%x to 0x%x",
			  param->test_size, adj_size);
		param->test_size = adj_size;
	}

	param->upload_buf = calloc(1, param->alloc_size);
	if (!param->upload_buf)
		return -1;

	param->download_buf = calloc(1, param->alloc_size);
	if (!param->download_buf)
		return -1;

	/*
	 * initialize the buffer to a known pattern - no plan to make
	 * it fancy for now, may change later
	 */
	p_size = (uint32_t *)param->upload_buf;
	*p_size = param->test_size - sizeof(uint32_t);
	if (param->v_pattern) {
		uint8_t *pval = (uint8_t *)param->upload_buf + sizeof(uint32_t);

		val = param->v_pattern;
		for (i = 0; i < *p_size; i++) {
			*pval++ = val;
			val += param->v_pattern;
		}
	}


	/* log a message to info user */
	LOCAL_LOG(VK_LOG_INFO, "Parameters for running....");
	LOCAL_LOG(VK_LOG_INFO,
		  "Q_tot[%d] Size 0x%x(%d) Alloc 0x%x(%d) - verify pattern 0x%x loopback %s",
		  param->q_no,
		  param->test_size, param->test_size,
		  param->alloc_size, param->alloc_size,
		  param->v_pattern,
		  param->run_lb ? "TRUE" : "FALSE");
	LOCAL_LOG(VK_LOG_INFO,
		  "Enc: Standard 0x%x, profile 0x%x, width %d, height %d",
		  param->enc_standard, param->enc_profile,
		  param->enc_size.width, param->enc_size.height);
	LOCAL_LOG(VK_LOG_INFO,
		  "Dec: Standard 0x%x, profile 0x%x, width %d, height %d",
		  param->dec_standard, param->dec_profile,
		  param->dec_size.width, param->dec_size.height);
	LOCAL_LOG(VK_LOG_INFO,
		  "Pat: 0x%x 0x%x 0x%x 0x%x...",
		  param->upload_buf[4], param->upload_buf[5],
		  param->upload_buf[6], param->upload_buf[7]);
	return 0;
}

/**
 * this function send down one message to the device and wait for an expected
 * response
 * @param file handle
 * @param message to be sent down
 * @param size of message to be sent down
 * @param returned message
 * @param size of returned message allowed
 * @param expected returned function id
 * @regturn 0 if success, error otherwise
 */
int32_t test_dev_send_msg(int fd, host2vk_msg *message_in, uint32_t in_size,
			  vk2host_msg *message_out, uint32_t out_size,
			  int32_t expected_rsp_function_id)
{
	uint32_t cnt;
	ssize_t op_size;

/* defines only used locally */
#define TEST_DEV_POLL_THRESHOLD 3
#define TEST_DEV_POLL_MAX       (40 * (1000000 / _DEF_POLL_US))

	op_size = write(fd, message_in, in_size);
	if (op_size != in_size) {
		LOCAL_LOG(
		    VK_LOG_ERROR,
		    "Q[%d] func %s Writing %d bytes down but return only %d",
		    message_in->queue_id,
		    vkil_function_id_str(message_in->function_id),
		    in_size, op_size);
		return -EPERM;
	}

	usleep(test_rsp_poll_us);

	cnt = 0;
	while (((op_size = read(fd, message_out, out_size)) != out_size)
	       && (cnt < TEST_DEV_POLL_MAX)) {
		cnt++;
		usleep(test_rsp_poll_us);
	}

	if (cnt > TEST_DEV_POLL_THRESHOLD) {
		LOCAL_LOG(VK_LOG_INFO,
			  "Q[%d] func %d Long Response time, take %d us.",
			  message_in->queue_id, message_in->function_id,
			  cnt * test_rsp_poll_us);
	}

	return (expected_rsp_function_id == message_out->function_id) ?
		0 : -EPERM;
}

/**
 * Configure the encoder and check configuration parameters
 * @param device file handle
 * @param q_id to test
 * @param context_id encoder context
 * @param pointer to testing parameter struct
 * @return 0 if success, error otherwise
 */
int32_t test_vkdrv_configure_encoder(int fd, uint32_t q_id,
				     uint32_t context_id,
				     const test_vkdrv_param *param)
{
	int32_t expected_function_id;
	host2vk_msg message_in;
	vk2host_msg message_out;
	host2vk_msg message_cfg[MSG_SIZE(sizeof(vk_enc_cfg)) + 1];
	vk_enc_cfg *enc_cfg = (vk_enc_cfg *)&(message_cfg[1]);

	memset(&message_in, 0, sizeof(host2vk_msg));
	memset(&message_cfg, 0, sizeof(message_cfg));

	/*
	 * just fill up the parameters to test, as long as parameters
	 * are sensible
	 */
	enc_cfg->standard = param->enc_standard;
	enc_cfg->size.size = param->enc_size.size;
	enc_cfg->profile = (param->enc_profile >> 16) & 0xFFFF;
	enc_cfg->level =  param->enc_profile & 0xFFFF;
	enc_cfg->bitrate = 2000000;
	enc_cfg->fps = 30 << 16;
	enc_cfg->gop_size = 30; /* just use default */

	/* configure the encoder */
	message_cfg->function_id = VK_FID_SET_PARAM;
	message_cfg->size = MSG_SIZE(sizeof(vk_enc_cfg));
	message_cfg->queue_id = q_id;
	message_cfg->context_id = context_id;
	message_cfg->args[0] = VK_PARAM_VIDEO_ENC_CONFIG;
	message_cfg->args[1] = 0;

	expected_function_id = VK_FID_SET_PARAM_DONE;
	if (test_dev_send_msg(fd, message_cfg,
			      sizeof(host2vk_msg) * (message_cfg->size + 1),
			      &message_out,
			      sizeof(message_out),
			      expected_function_id) ||
	    (message_cfg->context_id != message_out.context_id))
		goto fail_send;
	return 0;

fail_send:
	return -EPERM;
}

/**
 * Configure the decoder and check configuration parameters
 * @param device file handle
 * @param q_id to test
 * @param context_id encoder context
 * @param pointer to testing parameter struct
 * @return 0 if success, error otherwise
 */
int32_t test_vkdrv_configure_decoder(int fd, uint32_t q_id,
				     uint32_t context_id,
				     const test_vkdrv_param *param)
{
	int32_t expected_function_id;
	host2vk_msg message_in;
	vk2host_msg message_out;

	memset(&message_in, 0, sizeof(host2vk_msg));

	/* configure the decoder */
	message_in.function_id = VK_FID_SET_PARAM;
	message_in.size = 0;
	message_in.queue_id = q_id;
	message_in.context_id = context_id;
	message_in.args[0] = VK_PARAM_VIDEO_CODEC;
	message_in.args[1] = param->dec_standard;

	expected_function_id = VK_FID_SET_PARAM_DONE;
	if (test_dev_send_msg(fd, &message_in,
			      sizeof(host2vk_msg) * (message_in.size + 1),
			      &message_out,
			      sizeof(message_out),
			      expected_function_id))
		goto fail_send;

	message_in.function_id = VK_FID_SET_PARAM;
	message_in.size = 0;
	message_in.context_id = context_id;
	message_in.args[0] = VK_PARAM_VIDEO_PROFILEANDLEVEL;
	message_in.args[1] = param->dec_profile;

	expected_function_id = VK_FID_SET_PARAM_DONE;
	if (test_dev_send_msg(fd, &message_in,
			      sizeof(host2vk_msg) * (message_in.size + 1),
			      &message_out,
			      sizeof(message_out),
			      expected_function_id))
		goto fail_send;

	message_in.function_id = VK_FID_SET_PARAM;
	message_in.size = 0;
	message_in.context_id = context_id;
	message_in.args[0] = VK_PARAM_VIDEO_SIZE;
	message_in.args[1] = param->dec_size.size;

	expected_function_id = VK_FID_SET_PARAM_DONE;
	if (test_dev_send_msg(fd, &message_in,
			      sizeof(host2vk_msg) * (message_in.size + 1),
			      &message_out,
			      sizeof(message_out),
			      expected_function_id))
		goto fail_send;

	return 0;

fail_send:
	return -EPERM;
}

/**
 * test on accessing low-level dev with DMA transfer
 * This will repeat simulating sending down a surface and get the
 * encoded data in return.
 * @param file descriptor
 * @param message queue to be used
 * @param context_id
 * @param pointer to testing parameters
 * @return 0 if success, error otherwise
 */
int32_t test_encoder_dma(int fd, uint32_t q_id, int32_t context_id,
			 const test_vkdrv_param *param)
{
	int32_t expected_function_id;
	int32_t ret = 0;
	uint32_t handle;
	uint32_t i, j;
	uint32_t run_cmd;
	uint64_t upload_accum = 0;
	uint32_t upload_accum_cnt = 0;
	uint64_t download_accum = 0;
	uint32_t download_accum_cnt = 0;
	uint32_t lb_fail_cnt = 0;

	host2vk_msg message_in[VK_MSG_MAX_SIZE];
	vk2host_msg message_out[VK_MSG_MAX_SIZE];
	vk_buffer_surface buffer_surface;
	vk_buffer_packet buffer_packet;

	/* macro to calculate bandwidth in kbps */
#define _CALC_BW(_bytes, _time_ns) \
	((((uint64_t)_bytes) * 8) / (((uint64_t)_time_ns) / 1000000ULL))

	memset(message_in, 0, sizeof(host2vk_msg));
	memset(message_out, 0, sizeof(vk2host_msg));
	memset(&buffer_surface, 0, sizeof(buffer_surface));
	memset(&buffer_packet, 0, sizeof(buffer_packet));

	buffer_surface.prefix.type = VK_BUF_SURFACE;
	buffer_packet.prefix.type = VK_BUF_PACKET;

	run_cmd = (param->run_lb) ? VK_CMD_VERIFY_LB : VK_CMD_RUN;

	for (i = 0; i < param->dmacnt; i++) {

		uint32_t delta;
		uint64_t bw;

		message_in->function_id = VK_FID_TRANS_BUF;
		message_in->size = MSG_SIZE(sizeof(buffer_surface));
		message_in->context_id = context_id;
		message_in->queue_id = q_id;
		/* set 4 plane to upload always */
		message_in->args[0] = VK_CMD_UPLOAD | 0x4 | VK_CMD_OPT_GET_TIME;

		/*
		 * setup the surface's buffer parameters, use only 1
		 */
		buffer_surface.planes[0].address = (uint64_t) param->upload_buf;
		buffer_surface.planes[0].size = param->alloc_size;
		buffer_surface.max_size.width = param->enc_size.width;
		buffer_surface.max_size.height = param->enc_size.height;
		buffer_surface.stride[0] = param->enc_size.width << 2;
		for (j = 1; j < VK_SURFACE_MAX_PLANES; j++) {
			buffer_surface.planes[j].address = 0x0;
			buffer_surface.planes[j].size = 0x0;
		}
		memcpy((void *) &message_in[1], &buffer_surface,
		       sizeof(buffer_surface));

		expected_function_id = VK_FID_TRANS_BUF_DONE;
		if (test_dev_send_msg(fd, message_in,
				      sizeof(host2vk_msg) *
					      (message_in->size + 1),
				      message_out,
				      sizeof(vk2host_msg) * 2,
				      expected_function_id) ||
		    (message_in->context_id != message_out->context_id))
			goto fail_send;

		handle = message_out->arg;

		LOCAL_LOG(VK_LOG_INFO,
			  "<%10d> Upload buf %p, size 0x%x, hdl 0x%x",
			  i, param->upload_buf, param->alloc_size, handle);
		delta = *((uint32_t *)&message_out[1]);
		bw = 0;
		if (delta != 0) {
			bw = _CALC_BW(param->alloc_size, delta);
			upload_accum += bw;
			upload_accum_cnt++;
		}
		LOCAL_LOG(VK_LOG_INFO, "\t\t time %d ns, bw %d kbps",
			  delta, bw);

		message_in->function_id = VK_FID_PROC_BUF;
		message_in->size = 0;
		message_in->context_id = context_id;
		message_in->queue_id = q_id;
		message_in->args[0] = run_cmd;
		message_in->args[1] = handle;

		expected_function_id = VK_FID_PROC_BUF_DONE;
		if (test_dev_send_msg(fd, message_in,
				      sizeof(host2vk_msg) *
					      (message_in->size + 1),
				      message_out,
				      sizeof(vk2host_msg),
				      expected_function_id) ||
		    (message_in->context_id != message_out->context_id))
			goto fail_send;

		LOCAL_LOG(VK_LOG_INFO,
			  "<%10d> Process buffer done in-hdl 0x%x out-hdl 0x%x",
			  i, handle, message_out->arg);

		handle = message_out->arg;

		/* if process returned error, no need to do the download */
		if (_IS_INVALID_HANDLE(handle)) {
			LOCAL_LOG(VK_LOG_INFO,
			   "\t\t error handle 0x%x returned, skipping download",
			   handle);

			lb_fail_cnt++;
			continue;
		}

		buffer_packet.prefix.handle = handle;
		buffer_packet.prefix.port_id = 0;
		buffer_packet.size = param->alloc_size;
		buffer_packet.data = (uint64_t) param->download_buf;
		memset(param->download_buf, 0, param->test_size);

		message_in->function_id = VK_FID_TRANS_BUF;
		message_in->size = MSG_SIZE(sizeof(buffer_packet));
		message_in->context_id = context_id;
		message_in->queue_id = q_id;
		/* set 1 plane to download */
		message_in->args[0] = VK_CMD_DOWNLOAD | 0x1
				      | VK_CMD_OPT_GET_TIME;
		memcpy(&message_in->args[2], &buffer_packet,
		       sizeof(buffer_packet));

		expected_function_id = VK_FID_TRANS_BUF_DONE;
		if (test_dev_send_msg(fd, message_in,
				      sizeof(host2vk_msg) *
					      (message_in->size + 1),
				      message_out,
				      sizeof(vk2host_msg) * 2,
				      expected_function_id) ||
		    (message_in->context_id != message_out->context_id))
			goto fail_send;

		LOCAL_LOG(VK_LOG_INFO,
			  "<%10d> Download buf %p, size 0x%x, hdl 0x%x",
			  i, param->download_buf, param->test_size, handle);

		delta = *((uint32_t *)&message_out[1]);
		bw = 0;
		if (delta != 0) {
			bw = _CALC_BW(param->test_size, delta);
			download_accum += bw;
			download_accum_cnt++;
		}
		LOCAL_LOG(VK_LOG_INFO, "\t\t time %d ns, bw %d kbps",
			  delta, bw);
		/*
		 * perform verification.  do expect input and output
		 * buffer match for now, just print an error message at
		 * the first mismatch
		 */
		if (param->v_pattern) {
			uint32_t j;
			uint32_t src_len;
			uint32_t dst_len;
			uint8_t *src;
			uint8_t *dst;

			src_len = *(uint32_t *)param->upload_buf;
			dst_len = *(uint32_t *)param->download_buf;
			src = (uint8_t *)param->upload_buf + sizeof(uint32_t);
			dst = (uint8_t *)param->download_buf + sizeof(uint32_t);

			if (src_len != dst_len)
				LOCAL_LOG(VK_LOG_ERROR,
					  "Src len 0x%x not match Dst len 0x%x",
					  src_len, dst_len);

			for (j = 0; j < src_len; j++) {
				if (*src != *dst) {

					LOCAL_LOG(VK_LOG_ERROR,
						  "Error occur at [0x%x(%d)] = uploaded 0x%x, downloaded 0x%x",
						  j, j, *src, *dst);
					break;
				}
				src++;
				dst++;
			}
		}

	}

	/* output accumulated statistics */
	LOCAL_LOG(VK_LOG_INFO,
		  "LB failure %d Accum upload BW %d kbps, download BW %d kbps",
		  lb_fail_cnt,
		  upload_accum_cnt ? upload_accum / upload_accum_cnt : 0,
		  download_accum_cnt ? download_accum / download_accum_cnt : 0);

	/* flush the encoder */
	message_in->function_id = VK_FID_PROC_BUF;
	message_in->size = 0;
	message_in->context_id = context_id;
	message_in->queue_id = q_id;
	message_in->args[0] = run_cmd;
	message_in->args[1] = VK_BUF_EOS;

	expected_function_id = VK_FID_PROC_BUF_DONE;
	if (test_dev_send_msg(fd, message_in,
			      sizeof(host2vk_msg) * (message_in->size + 1),
			      message_out,
			      sizeof(vk2host_msg),
			      expected_function_id) ||
	    (message_in->context_id != message_out->context_id))
		goto fail_send;


	return ret;
fail_send:

	LOCAL_LOG(VK_LOG_ERROR, "Fail...");
	LOCAL_LOG(VK_LOG_ERROR,
		  "%s exp %s rx %s context [0x%x 0x%x], cmd 0x%x outarg 0x%x",
		  vkil_function_id_str(message_in->function_id),
		  vkil_function_id_str(expected_function_id),
		  vkil_function_id_str(message_out->function_id),
		  message_in->context_id, message_out->context_id,
		  message_in->args[0], message_out->arg);
	return -1;
}

/**
 * test on accessing low-level dev
 * That will send down a bunch of messages to the device and test
 * out the responses
 * @param device fs name
 * @return 0 1if success, error otherwise
 */
int32_t test_dev(const char *dev_name, const test_vkdrv_param *param)
{
	int32_t expected_function_id;
	int32_t context_id;
	vkil_role_t curr_role;
	vkil_context_essential ctx_essential;
	host2vk_msg message_in;
	vk2host_msg message_out;
	vkil_role_t  test_role_list[] = {
		VK_ENCODER,
		/* VK_DECODER, not need to do anything yet */
	};
	uint32_t i, q_id;
	int fd;

	/* first, open the device */
	fd = open(dev_name, O_RDWR);
	if (fd < 0)
		return fd;

	for (i = 0; i < ARRAY_SIZE(test_role_list); i++) {
		for (q_id = 0; q_id < param->q_no; q_id++) {

			memset(&message_in, 0, sizeof(host2vk_msg));
			memset(&ctx_essential, 0,
			       sizeof(vkil_context_essential));

			curr_role = test_role_list[i];
			ctx_essential.component_role = curr_role;

			message_in.function_id = VK_FID_INIT;
			message_in.size = 0;
			message_in.context_id = 0;
			message_in.queue_id = q_id;
			memcpy(message_in.args, &ctx_essential,
			       sizeof(uint32_t) * 2);

			expected_function_id = VK_FID_INIT_DONE;
			if (test_dev_send_msg(fd, &message_in,
					      sizeof(message_in),
					      &message_out,
					      sizeof(message_out),
					      expected_function_id))
				goto fail_close;

			context_id = message_out.context_id;

			if (curr_role == VK_ENCODER) {

				/* encoder needs parameters setup before DMA */
				if (test_vkdrv_configure_encoder(fd,
								 q_id,
								 context_id,
								 param)) {
					LOCAL_LOG(
					    VK_LOG_INFO,
					    "Q[%d] Cmpt %s config param fails",
					    q_id,
					    vksim_cmpt_role2str(curr_role));
					goto fail_close;

				}

				/* finish the init with another init message */
				message_in.function_id = VK_FID_INIT;
				message_in.size = 0;
				message_in.context_id = context_id;
				message_in.queue_id = q_id;

				if (test_dev_send_msg(fd, &message_in,
						      sizeof(message_in),
						      &message_out,
						      sizeof(message_out),
						      expected_function_id))
					goto fail_close;

				/* now ready for stream data */
				if (test_encoder_dma(fd, q_id,
						     context_id, param)) {
					LOCAL_LOG(
					    VK_LOG_INFO,
					    "Q[%d] Cmpt %s DMA test fails",
					    q_id,
					    vksim_cmpt_role2str(curr_role));
					goto fail_close;
				}
			} else if (curr_role == VK_DECODER) {

				/* decoder set up */
				if (test_vkdrv_configure_decoder(fd,
								 q_id,
								 context_id,
								 param)) {
					LOCAL_LOG(
					    VK_LOG_INFO,
					    "Q[%d] Cmpt %s config param fails",
					    q_id,
					    vksim_cmpt_role2str(curr_role));
					goto fail_close;

				}

				/* finish the init with another init message */
				message_in.function_id = VK_FID_INIT;
				message_in.size = 0;
				message_in.context_id = context_id;
				message_in.queue_id = q_id;

				if (test_dev_send_msg(fd, &message_in,
						      sizeof(message_in),
						      &message_out,
						      sizeof(message_out),
						      expected_function_id))
					goto fail_close;
			}

			message_in.function_id = VK_FID_DEINIT;
			message_in.context_id = context_id;
			expected_function_id = VK_FID_DEINIT_DONE;

			if (test_dev_send_msg(fd, &message_in,
					      sizeof(message_in),
					      &message_out,
					      sizeof(message_out),
					      expected_function_id))
				goto fail_close;

			/* check also the context */
			if (message_in.context_id != message_out.context_id)
				goto fail_close;

			LOCAL_LOG(VK_LOG_INFO, "Q[%d] Cmpt %s test successful.",
				  q_id, vksim_cmpt_role2str(test_role_list[i]));
		}
	}

	close(fd);
	return 0;

fail_close:
	close(fd);
	return -EPERM;
}

/**
 * main testing unit entry
 */
int main(int argc, char **argv)
{
	int c;
	int option_index;
	int32_t ret = -1;
	char dev_name[30];
	bool size_ok = false;
	int i;

	test_vkdrv_param test_param;
	static struct option long_options[] = {
		{"dev", required_argument, 0, 'd'},
		{"verify", required_argument, 0, 'v'},
		{"bufsize", required_argument, 0, 's'},
		{"dmacnt", required_argument, 0, 'c'},
		{"qno", required_argument, 0, 'q'},
		{"loopback", required_argument, 0, 'l'},
		{"poll_us", required_argument, 0, 'p'},
		{0, 0, 0, 0}
	};
	static uint32_t supported_size_tab[] = {
		0x8000, 0x200000, 0x800000,
	};

	memset(&test_param, 0, sizeof(test_param));

	ret = vk_logger_init();
	if (ret) {
		printf("Error creating logger. Exit Immediately!\n");
		return -EINVAL;
	}

	/*
	 * Check and get device if user input it, we support
	 *  -d <device>, --dev <device> or --dev=<device>
	 */
	dev_name[0] = '\0';
	test_param.run_lb = true;
	test_param.q_no = 1;
	test_param.v_pattern = 0x0; /* default pattern */
	test_param.test_size = 0x8000;
	test_param.dmacnt = 1;	 /* only do 1 buffer DMA transfer total */
	test_rsp_poll_us = _DEF_POLL_US;

	while ((c = getopt_long(argc, argv, "d:v:s:c:q:l:p:",
				long_options, &option_index)) != -1) {
		switch (c) {
		case 'd':
			strncpy(dev_name, optarg, sizeof(dev_name));
			break;
		case 's':
			test_param.test_size = strtoul(optarg, NULL,
						   optarg[1] == 'x' ? 16 : 0);
			break;
		case 'v':
			test_param.v_pattern = strtoul(optarg, NULL,
						   optarg[1] == 'x' ? 16 : 0);
			break;
		case 'c':
			test_param.dmacnt = strtoul(optarg, NULL,
						optarg[1] == 'x' ? 16 : 0);
			break;
		case 'q':
			test_param.q_no = strtoul(optarg, NULL,
					      optarg[1] == 'x' ? 16 : 0);
			break;
		case 'l':
			test_param.run_lb = strcmp(optarg, "true") == 0;
			break;
		case 'p':
			test_rsp_poll_us = strtoul(optarg, NULL,
						   optarg[1] == 'x' ? 16 : 0);
			break;
		default:
			LOCAL_LOG(VK_LOG_ERROR, "%c Not supported", c);
			return -1;
		}
	}

	/* check for limited supported sizes */
	for (i = 0; i < ARRAY_SIZE(supported_size_tab); i++) {
		if (test_param.test_size == supported_size_tab[i]) {
			size_ok = true;
			break;
		}
	}
	if (!size_ok) {
		LOCAL_LOG(VK_LOG_INFO,
			  "Sizes supported....");
		for (i = 0; i < ARRAY_SIZE(supported_size_tab); i++)
			LOCAL_LOG(VK_LOG_INFO, "[%d] - 0x%x",
				  supported_size_tab[i]);
		ret = -1;
		goto deinit_exit;
	}

	if ((dev_name[0] == '\0') ||
	    test_param_init(&test_param)) {
		LOCAL_LOG(VK_LOG_INFO, "Test init failure, dev->%s.", dev_name);
		ret = -1;
		goto free_and_exit;
	}

	LOCAL_LOG(VK_LOG_INFO,
		  "Dev %s Emu Test started, Poll response time %d us",
		  dev_name, test_rsp_poll_us);
	ret = test_dev(dev_name, &test_param);

free_and_exit:
	/* free memory */
	free(test_param.upload_buf);
	free(test_param.download_buf);

deinit_exit:
	LOCAL_LOG(VK_LOG_INFO, "test %s\n", (ret == 0) ?
					     "successful" : "fails");

	vk_logger_deinit();
	return ret;
};
