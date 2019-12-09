// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright(c) 2019 Broadcom
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

#define MAX_BER_WAIT_MS		1000
#define BER_SIGNATURE		0x42455253
#define BER_MAX_SAMPLES		64
#define BER_NR_MODES		4

#define HI_CONFIDENCE_ERR_CNT	100
#define HI_CONFIDENCE_MIN_ERR_CNT	20
#define MAX_CLIPPED_ERR_CNT	8355840
#define ARTIFICIAL_BER		0.5
#define ARTIFICIAL_MARGIN_V	500
#define ARTIFICIAL_MARGIN_H	1
#define MIN_BER_TO_REPORT	-24
#define MIN_BER_FOR_FIT		-8.0

#define PCIE_G3_SPEED		8000000000

/* diag BER mode settings */
enum srds_diag_ber_scan_mode_enum {
	DIAG_BER_POS  = 0,
	DIAG_BER_NEG  = 1,
	DIAG_BER_VERT = 0,
	DIAG_BER_HORZ = 1 << 1,
	DIAG_BER_P1_NARROW = 1 << 3,
};

/* BER confidence scale */
static const double ber_conf_scale[104] = {
	2.9957, 5.5717, 3.6123, 2.9224, 2.5604,
	2.3337, 2.1765, 2.0604, 1.9704, 1.8983,
	1.8391, 1.7893, 1.7468, 1.7100, 1.6778,
	1.6494, 1.6239, 1.6011, 1.5804, 1.5616,
	1.5444, 1.5286, 1.5140, 1.5005, 1.4879,
	1.4762, 1.4652, 1.4550, 1.4453, 1.4362,
	1.4276, 1.4194, 1.4117, 1.4044, 1.3974,
	1.3908, 1.3844, 1.3784, 1.3726, 1.3670,
	1.3617, 1.3566, 1.3517, 1.3470, 1.3425,
	1.3381, 1.3339, 1.3298, 1.3259, 1.3221,
	1.3184, 1.3148, 1.3114, 1.3080, 1.3048,
	1.3016, 1.2986, 1.2956, 1.2927, 1.2899,
	1.2872, 1.2845, 1.2820, 1.2794, 1.2770,
	1.2746, 1.2722, 1.2700, 1.2677, 1.2656,
	1.2634, 1.2614, 1.2593, 1.2573, 1.2554,
	1.2535, 1.2516, 1.2498, 1.2481, 1.2463,
	1.2446, 1.2429, 1.2413, 1.2397, 1.2381,
	1.2365, 1.2350, 1.2335, 1.2320, 1.2306,
	1.2292, 1.2278, 1.2264, 1.2251, 1.2238,
	1.2225, 1.2212, 1.2199, 1.2187, 1.2175,
	1.2163, /* starts in index: 100 for #errors: 100,200,300,400 */
	1.1486, /* 200 */
	1.1198, /* 300 */
	1.1030
};

uint32_t ber_mode[BER_NR_MODES];
uint32_t ber_time[BER_NR_MODES][BER_MAX_SAMPLES];
uint32_t ber_err[BER_NR_MODES][BER_MAX_SAMPLES];

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
	printf("Usage: pcie_eye -d dev_no -p phy_no -l lane_no [-b ber_en]\n");
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

static int ber_read_data(uint32_t *fp, const int size)
{
	int i, mode_idx = -1;
	int fetch_mode = 0;
	uint32_t val;
	unsigned int sample_cnt = 0;

	for (i = 0; i < size / 4; i++) {
		val = fp[i];

		/* each signature match marks a start of a new set of data */
		if (val == BER_SIGNATURE) {
			mode_idx++;
			if (mode_idx >= BER_NR_MODES) {
				printf("Invalid BER mode index %d\n",
						mode_idx);
				return -1;
			}

			/* clear memories */
			memset(ber_time[mode_idx], 0,
					sizeof(uint32_t) * BER_MAX_SAMPLES);
			memset(ber_err[mode_idx], 0,
					sizeof(uint32_t) * BER_MAX_SAMPLES);

			/* next data should be BER mode */
			fetch_mode = 1;
			sample_cnt = 0;
			continue;
		}

		if (fetch_mode == 1) {
			ber_mode[mode_idx] = val;
			fetch_mode = 0;
			continue;
		}

		/* fetch data */
		if (sample_cnt > BER_MAX_SAMPLES * 2) {
			printf("Invalid BER sample count %u\n", sample_cnt);
			return -1;
		}

		/*
		 * First set of samples are time, and second set of
		 * samples are BER
		 */
		if (sample_cnt < BER_MAX_SAMPLES)
			ber_time[mode_idx][sample_cnt] = val;
		else
			ber_err[mode_idx][sample_cnt - BER_MAX_SAMPLES] = val;

		sample_cnt++;
	};

	return 0;
}

static double ber_extrapolate_data(double rate, uint8_t ber_scan_mode,
				   uint32_t *total_errs, uint32_t *total_time,
				   uint8_t max_offset)
{
	double lbers[BER_MAX_SAMPLES];
	double margins[BER_MAX_SAMPLES];
	double bers[BER_MAX_SAMPLES];
	uint32_t i;
	int8_t offset[BER_MAX_SAMPLES];
	int8_t mono_flags[BER_MAX_SAMPLES];
	int8_t direction, heye, delta_n;
	double Exy = 0.0, Eyy = 0.0, Exx = 0.0;
	double Ey = 0.0, Ex = 0.0;
	double alpha = 0.0, beta = 0.0;
	double proj_margin_12 = 0.0;
	uint8_t start_n, stop_n, idx;
	uint8_t n_mono = 0;
	uint8_t eye_cnt = 1;
	uint8_t hi_confidence_cnt = 0;
	int8_t first_good_ber_idx = -1;
	int8_t first_small_errcnt_idx = -1;
	int8_t first_non_clipped_errcnt_idx = -1;
	uint8_t range250;
	int artificial_margin;
	double artificial_lber;

	for (i = 0; i < BER_MAX_SAMPLES; i++) {
		bers[i] = 0;
		mono_flags[i] = 0;
	}

	/* decode mode/direction/etc */
	heye = (ber_scan_mode & DIAG_BER_HORZ) >> 1;
	direction = (ber_scan_mode & DIAG_BER_NEG) ? -1 : 1;
	range250 = (ber_scan_mode & DIAG_BER_P1_NARROW) ? 0 : 1;

	/* prepare artificial points in case they are needed */
	if (heye == 1)
		artificial_margin = direction * ARTIFICIAL_MARGIN_H;
	else
		artificial_margin = direction * ARTIFICIAL_MARGIN_V;
	artificial_lber = (double)sqrt(-log10(ARTIFICIAL_BER));

	/*
	 * Generate margins[]
	 * Generate ber[]
	 * Find first and last points for linear fit
	 */
	i = 0;
	do {
		if (heye == 1) {
			offset[i] = (int8_t)(max_offset - i);
			/* Horizontal scale is (1/64) of the Unit Interval */
			margins[i] = direction * offset[i] * 1000.0 / 64.0;
		} else {
			offset[i] = (int8_t)(max_offset - i);
			margins[i] = direction *
				ladder_setting_to_mV(offset[i], range250);
		}

		if (total_errs[i] == 0)
			bers[i] = 1.0 / (((double)total_time[i]) *
					 0.00001 * rate);
		else
			bers[i] = ((double)total_errs[i] /
				   (((double)total_time[i]) * 0.00001 * rate));

		/*
		 * Find the first data point with good BER (i.e.,
		 * BER <= 10^MIN_BER_FOR_FIT)
		 *
		 * NOTE: no need for lower bound on BER, since correction
		 * factors will be applied for all total_errs >= 0
		 */
		if ((log10(bers[i]) <= MIN_BER_FOR_FIT) &&
				(first_good_ber_idx == -1))
			first_good_ber_idx = (int8_t)i;

		/* determine high-confidence iterations */
		if (total_errs[i] >= HI_CONFIDENCE_ERR_CNT)
			hi_confidence_cnt++;
		else if ((total_errs[i] < HI_CONFIDENCE_MIN_ERR_CNT) &&
				(first_small_errcnt_idx == -1))
			first_small_errcnt_idx = (int8_t)i;

		/*
		 * Determine first non-clipped error count
		 *
		 * NOTE: Originally this limit was created for post processing
		 * of micro-generated data; however, this could be used for
		 * PC-generated data as well
		 */
		if ((total_errs[i] < MAX_CLIPPED_ERR_CNT) &&
		    (first_non_clipped_errcnt_idx == -1))
			first_non_clipped_errcnt_idx = (int8_t)i;

		i++;
	} while (((total_errs[i] != 0) || (total_time[i] != 0)) &&
		 (i <= max_offset));

	eye_cnt = (int8_t)i;

	/*
	 * Setting up stop_n variable
	 *
	 * Check if:
	 *  - There is only one point in measurement vector (i.e. eye_cnt = 1)
	 *  - The very last point's measurement time was "too short"
	 */
	i = eye_cnt - 1;
	if (i >= 1) {
		if ((total_time[i] >= 0.5 * total_time[i - 1]) ||
		    (total_errs[i] >= HI_CONFIDENCE_MIN_ERR_CNT)) {
			stop_n = eye_cnt; /* include last point */
		} else {
			stop_n = eye_cnt - 1; /* discard last point */
		}
	} else
		stop_n = 1; /* there is ONLY one measurement */

	/*
	 * Correcting *all* BER values using confidence factors in
	 * 'ber_conf_scale' vector
	 *
	 * This step is done for extrapolation purposes
	 */
	for (idx = 0; idx < eye_cnt; idx++) {
		if (total_errs[idx] <= 100)
			bers[idx] = ber_conf_scale[total_errs[idx]] * bers[idx];
		else if (total_errs[idx] > 100 && total_errs[idx] < 200)
			bers[idx] = ber_conf_scale[100] * bers[idx];
		else if (total_errs[idx] >= 200 && total_errs[idx] < 300)
			bers[idx] = ber_conf_scale[101] * bers[idx];
		else if (total_errs[idx] >= 300 && total_errs[idx] < 400)
			bers[idx] = ber_conf_scale[102] * bers[idx];
		else if (total_errs[idx] >= 400)
			bers[idx] = ber_conf_scale[103] * bers[idx];
	}

	/* compute the "linearised" ber vector */
	for (idx = 0; idx < eye_cnt; idx++)
		lbers[idx] = (double)sqrt(-log10(bers[idx]));

	/* assign highest data point to use */
	if (first_good_ber_idx == -1)
		start_n = stop_n;
	else
		start_n = first_good_ber_idx;

	/*
	 * EXTRAPOLATION (START)
	 *
	 * Different data set profiles can be received by this code.
	 * Each case is processed accordingly here (IF-ELSE IF cases)
	 */

	/* errors encountered all the way to sampling point */
	if (start_n >= eye_cnt)
		return proj_margin_12;

	/*
	 * Only ONE measured point. Typically when the eye is wide
	 * open. Artificial points will be used to make
	 * extrapolation possible
	 */
	if (stop_n == 1) {
		delta_n = 1;

		/*
		 * Compute covariances and means
		 *
		 * But only for two points: artificial and the single
		 * measured point
		 */
		Exy = ((margins[0] * lbers[0] +
		       artificial_margin * artificial_lber) / 2.0);
		Eyy = ((lbers[0] * lbers[0] +
		       artificial_lber * artificial_lber) / 2.0);
		Exx = ((margins[0] * margins[0] +
		       artificial_margin * artificial_margin) / 2.0);
		Ey  = ((lbers[0] + artificial_lber) / 2.0);
		Ex  = ((margins[0] + artificial_margin) / 2.0);

		goto end;
	}

	/* normal case when there are more than 1 measurement */
	/* Detect and record nonmonotonic data points */
	for (idx = 0; idx < stop_n; idx++) {
		if ((idx > start_n) &&
				(log10(bers[idx]) > log10(bers[idx - 1]))) {
			mono_flags[idx] = 1;
			if (first_good_ber_idx != -1)
				n_mono++;
		}
	}
	/* derive number of MEASURED points available for extrapolation */
	delta_n = (stop_n - start_n - n_mono);

	/* high confidence case */
	if (delta_n >= 2) {
		/* Compute covariances and means */
		for (idx = start_n; idx < stop_n; idx++) {
			if (mono_flags[idx] != 0)
				continue;

			Exy += (margins[idx] * lbers[idx] / (double)delta_n);
			Eyy += (lbers[idx] * lbers[idx] / (double)delta_n);
			Exx += (margins[idx] * margins[idx] / (double)delta_n);
			Ey  += (lbers[idx] / (double)delta_n);
			Ex  += (margins[idx] / (double)delta_n);
		}
	} else { /* low confidence case */
		if ((first_non_clipped_errcnt_idx >= 0) &&
				(first_non_clipped_errcnt_idx < start_n)) {
			/*
			 * Compute covariances and means...
			 *
			 * But only for two points: first and last
			 */
			Exy = ((margins[stop_n - 1] * lbers[stop_n - 1] +
			       margins[first_non_clipped_errcnt_idx] *
			       lbers[first_non_clipped_errcnt_idx]) / 2.0);
			Eyy = ((lbers[stop_n - 1] * lbers[stop_n - 1] +
			       lbers[first_non_clipped_errcnt_idx] *
			       lbers[first_non_clipped_errcnt_idx]) / 2.0);
			Exx = ((margins[stop_n - 1] * margins[stop_n - 1] +
			       margins[first_non_clipped_errcnt_idx] *
			       margins[first_non_clipped_errcnt_idx]) / 2.0);
			Ey  = ((lbers[stop_n - 1] +
			       lbers[first_non_clipped_errcnt_idx]) / 2.0);
			Ex  = ((margins[stop_n - 1] +
			       margins[first_non_clipped_errcnt_idx]) / 2.0);
		} else {
			/*
			 * Compute covariances and means...
			 *
			 * But only for two points:
			 * artificial and the single measured point
			 */
			Exy = (artificial_margin * artificial_lber) / 2.0;
			Eyy = (artificial_lber * artificial_lber) / 2.0;
			Exx = (artificial_margin * artificial_margin) / 2.0;
			Ey  = (artificial_lber) / 2.0;
			Ex  = (artificial_margin) / 2.0;

			for (idx = start_n; idx < stop_n; idx++) {
				if (mono_flags[idx] != 0)
					continue;

				Exy += (margins[idx] * lbers[idx] / 2.0);
				Eyy += (lbers[idx] * lbers[idx] / 2.0);
				Exx += (margins[idx] * margins[idx] / 2.0);
				Ey  += (lbers[idx] / 2.0);
				Ex  += (margins[idx] / 2.0);
			}
		}
	}

end:
	/* compute fit slope and offset: BER = alpha * margin + beta */
	alpha = (Exy - Ey * Ex) / (Exx - Ex * Ex);
	beta = Ey - Ex * alpha;

	proj_margin_12 = direction * (sqrt(-log10(1e-12)) - beta) /
		alpha;

	return abs(proj_margin_12);
}

static void display_pcie_ber(void)
{
	int16_t level;
	int i, j, y;
	double margin[BER_NR_MODES];
	double hor_test_result = 0;
	double ver_test_result = 0;

	j = (BER_MAX_SAMPLES / 2) - 1;

	for (i = 0; i < BER_NR_MODES; i++) {
		for (y = j; y >= -j; y--) {
			if (ber_mode[i] & DIAG_BER_HORZ)
				level = ladder_setting_to_mV(y, 1);
			else
				level = y;

			if (level == 0) {
				margin[i] = ber_extrapolate_data(PCIE_G3_SPEED,
						ber_mode[i],
						ber_err[i],
						ber_time[i],
						j);
				break;
			}
		}
	}

	hor_test_result = (margin[0] + margin[1]) / 1000;
	ver_test_result = (margin[2] + margin[3]);

	printf("===========================================================\n");
	printf("Extrapolation for BER at 1e-12 is completed\n\n");
	printf("<Test Result>:");

	if ((hor_test_result > 0) && (ver_test_result > 0))
		printf(" Both Eye Width and Height margins are " \
		       "greater than 0%%, test PASSED\n");
	else if ((hor_test_result > 0) && (ver_test_result <= 0))
		printf(" Eye Height margin is not greater than 0%%, " \
		       "test FAILED\n");
	else if ((hor_test_result <= 0) && (ver_test_result > 0))
		printf(" Eye Width margin is not greater than 0%%, " \
		       "test FAILED\n");
	else
		printf("Both Eye Width and Height margins are not " \
		       "greater than 0%%, test FAILED\n");
	printf("\n");

	printf("<Margins>:\n");
	printf("Eye Width margin at le-12 is %0.3f UI\n", hor_test_result);
	printf("Eye Height margin at le-12 is %0.2f mV\n", ver_test_result);
	printf("===========================================================\n");
}

int main(int argc, char *argv[])
{
	vk_pcie_eye_ctx *ctx = &eye_info.ctx;
	vk_pcie_eye_config *cfg = &eye_info.cfg;
	vkil_buffer_metadata buffer_metadata;
	vkil_api *vkilapi;
	vkil_context *vkilctx = NULL;
	uint32_t data;
	int c, ret;
	void *ptr;
	int ber = 0, count = MAX_BER_WAIT_MS;

	if (argc != 7 && argc != 9) {
		printf("Invalid number of args\n");
		print_usage();
		return 0;
	}

	ctx->dev_id = "0";
	while ((c = getopt(argc, argv, "p:l:d:b:")) != -1) {
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

		case 'b':
			ber = strtoul(optarg, NULL, 0);
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


	if (!ber)
		goto end;

	memset(buffer_metadata.data, 0, buffer_metadata.size);
	data = cfg->lane;

	printf("Trying to extrapolate for BER at 1e-12\n");
	printf("This may take several minutes...\n");
	ret = vkilapi->get_parameter(
				vkilctx,
				VK_PARAM_PCIE_BER,
				&data,
				VK_CMD_RUN | VK_CMD_OPT_BLOCKING);
	if (ret < 0) {
		printf("PCIe BER failed:%d\n", ret);
		goto end;
	}
	buffer_metadata.prefix.handle = data;

	printf("BER in Progress:");
	do {
		ret = vkilapi->get_parameter(
					vkilctx,
					VK_PARAM_PCIE_BER_SIZE,
					&data,
					VK_CMD_RUN | VK_CMD_OPT_BLOCKING);
		if (ret < 0) {
			printf("PCIe get BER size failed:%d\n", ret);
			goto end;
		}
		count--;
		if (count % 5 == 0)
			printf("#");
		else if (count % 5 == 1)
			printf("/\b");
		else if (count % 5 == 2)
			printf("|\b");
		else if (count % 5 == 3)
			printf("\\\b");
		else
			printf("-\b");
		fflush(stdout);
		sleep(1);
	} while (count && data == 0); /* Returns zero if BER is not ready */

	printf("\n");
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

	ptr = buffer_metadata.data;
	ber_read_data((uint32_t *)ptr, buffer_metadata.size);
	display_pcie_ber();

end:
	if (vkilctx) {
		ctx->ilctx = vkilctx;
		pcie_eye_vkil_deinit(ctx);
		pcie_eye_vkil_destroy_api(ctx);
	}
	if (ctx->buffer)
		free(ctx->buffer);
	return 0;
}
