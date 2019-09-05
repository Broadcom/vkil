/* SPDX-License-Identifier: GPL-2.0 OR Apache-2.0 */
/*
 * Copyright(c) 2018 Broadcom
 */

#ifndef VK_PARAMETERS_H
#define VK_PARAMETERS_H

#include <stdint.h>

#define VK_SCL_MAX_OUTPUTS  4

#define VK_NEW_CTX  0 /**< indicate a request for new context */
#define VK_INFO_CTX 0 /**< general info request without context */
#define VK_BUF_EOS  0 /**< indicate an end of stream (no more buffer) */


/**
 * @brief enumeration of different component role
 */
enum _vk_role_t {
	VK_INFO     = 0,
	VK_DMA      = 1,
	VK_DECODER  = 2,
	VK_ENCODER  = 3,
	VK_SCALER   = 4,
	VK_ROLE_MAX = 0xF /**< the role is encoded on 4 bits */
};

typedef enum _vk_role_t vk_role_t;
typedef enum _vk_role_t vkil_role_t;

/**
 * @brief structure used by the card to allocate a context
 *
 * it is expected to be passed to the card  in a host2vk init message
 * for this reason the structure size is limited to 8 bytes
 * (keep message size to 1)
 */
struct _vk_context_essential {
	uint32_t handle; /**< host opaque handle defined by the vk card */
	uint32_t queue_id:4; /**< queue id */
	uint32_t component_role:4; /**< vk_role_t encoded on 4 bits */
	/*
	 * host pid so that on VK it could have linkage
	 * max is 2^22 according to man 5 proc
	 */
	uint32_t pid:24;
};

typedef struct _vk_context_essential vk_context_essential;
/* the below is an obsolete naming, prefer vk_context_essential */
typedef struct _vk_context_essential vkil_context_essential;

/**
 * @brief base available command sent to the HW
 */
typedef enum _vk_base_command_t {
	VK_CMD_BASE_NONE      = 0, /**< no explicit command */
	VK_CMD_BASE_IDLE      = 1, /**< standby mode */
	VK_CMD_BASE_RUN       = 2, /**< request to start processing */
	VK_CMD_BASE_FLUSH     = 3, /**< flush at end of operation */
	VK_CMD_BASE_UPLOAD    = 4, /**< memory transfer to vk hw   */
	VK_CMD_BASE_DOWNLOAD  = 5, /**< memory transfer from vk hw */
	VK_CMD_BASE_VERIFY_LB = 6, /**< command to verify buffer + LB */
	VK_CMD_BASE_MAX       = 7
} vk_base_command_t;

/* shift to get to cmd */
#define VK_CMD_BASE_SHIFT     8

/**
 * @brief available command sent to the HW
 */
enum _vk_command_t {
	/** no explicit command */
	VK_CMD_NONE        = VK_CMD_BASE_NONE      << VK_CMD_BASE_SHIFT,
	/** standby mode */
	VK_CMD_IDLE        = VK_CMD_BASE_IDLE      << VK_CMD_BASE_SHIFT,
	/** request to start processing */
	VK_CMD_RUN         = VK_CMD_BASE_RUN       << VK_CMD_BASE_SHIFT,
	/** flush at end of operation */
	VK_CMD_FLUSH       = VK_CMD_BASE_FLUSH     << VK_CMD_BASE_SHIFT,
	/** memory transfer to vk hw */
	VK_CMD_UPLOAD      = VK_CMD_BASE_UPLOAD    << VK_CMD_BASE_SHIFT,
	/** memory transfer from vk hw */
	VK_CMD_DOWNLOAD    = VK_CMD_BASE_DOWNLOAD  << VK_CMD_BASE_SHIFT,
	/** command to verify buffer + LB */
	VK_CMD_VERIFY_LB   = VK_CMD_BASE_VERIFY_LB << VK_CMD_BASE_SHIFT,
};

typedef enum  _vk_command_t vk_command_t;
typedef enum  _vk_command_t vkil_command_t;

/* shift to get to first option bit, ie VK_CMD_OPT_CB */
#define VK_CMD_OPTS_SHIFT    14
#define VK_CMD_OPTS_TOT       4

/** means callback command */
#define VK_CMD_OPT_CB       (0x1 << VK_CMD_OPTS_SHIFT)
/** means a blocking command */
#define VK_CMD_OPT_BLOCKING (0x2 << VK_CMD_OPTS_SHIFT)
/** means to collect operation time */
#define VK_CMD_OPT_GET_TIME (0x4 << VK_CMD_OPTS_SHIFT)
/** means host performing a DMA loopback */
#define VK_CMD_OPT_DMA_LB   (0x8 << VK_CMD_OPTS_SHIFT)
#define VK_CMD_OPTS_MASK    (((1 << VK_CMD_OPTS_TOT) - 1) << VK_CMD_OPTS_SHIFT)

/** number of planes to up/download */
#define VK_CMD_PLANES_MASK   0x000F
/** command mask */
#define VK_CMD_MASK          (0xF << VK_CMD_BASE_SHIFT)
/** command mask for load: command + options that are allowed to pass down */
#define VK_CMD_LOAD_MASK     (VK_CMD_MASK | VK_CMD_OPT_DMA_LB)

enum _vk_status_t {
	VK_STATE_OK = 0,
	VK_STATE_UNLOADED = 1, /**< no hw ctx is loaded */
	VK_STATE_READY = 2,
	VK_STATE_IDLE = 3,
	VK_STATE_RUN = 4,
	VK_STATE_FLUSH = 5,
	VK_STATE_ERROR  = 0xFF,
};

typedef enum _vk_status_t vk_status_t;
typedef enum _vk_status_t vkil_status_t;

enum vk_mve_reconstruct_mode {
	MVE_RECONS_OFF = 0,
	MVE_RECONS_REF_FRAMES_ONLY,
	MVE_RECONS_ALL_FRAMES,
};

enum vk_video_standard {
	/* a zero value is considered as invalid */
	VK_V_STANDARD_UNKNOWN = 0,
	VK_V_STANDARD_H264,
	VK_V_STANDARD_HEVC,
	VK_V_STANDARD_VP9,
};

/**
 * profile is coded on 15 bits,
 * values are directly mapped from mve_protocol_def.h
 * (used by the MALi codec HW engine
 */
enum vk_video_profile {
	/* a zero value is considered as invalid */
	VK_V_PROFILE_UNKNOWN                   = 0,
	/* h264 profile */
	VK_V_PROFILE_H264_BASELINE             = 1,
	VK_V_PROFILE_H264_CONSTRAINED_BASELINE = 1,
	VK_V_PROFILE_H264_MAIN                 = 2,
	VK_V_PROFILE_H264_HIGH                 = 3,
	VK_V_PROFILE_H264_EXTENDED             = 4,
	/* hevc profile */
	VK_V_PROFILE_HEVC_MAIN                 = 1,
	VK_V_PROFILE_HEVC_MAIN_STILL           = 2,
	VK_V_PROFILE_HEVC_MAIN_INTRA           = 3,
	VK_V_PROFILE_HEVC_MAIN10               = 4,
	/* vp9 profile */
	VK_V_PROFILE_VP9_0                     = 1, /* 8bits 420 */
	VK_V_PROFILE_VP9_1                     = 2, /* 8bits 422 and 444 */
	VK_V_PROFILE_VP9_2                     = 3, /* 10-12bits 420 */
	VK_V_PROFILE_VP9_3                     = 4, /* 10-12bits 422 and 444 */
	VK_V_PROFILE_MAX = 0xFFFF
};

/**
 * level is coded on 16 bits,
 * values are directly mapped from mve_protocol_def.h
 * (used by the MALi codec HW engine
 */
enum vk_video_level {
	/* a zero value is considered as invalid */
	VK_V_LEVEL_UNKNOWN =  0,
	/* h264 level */
	VK_V_LEVEL_H264_1  =  1,
	VK_V_LEVEL_H264_1b =  2,
	VK_V_LEVEL_H264_11 =  3,
	VK_V_LEVEL_H264_12 =  4,
	VK_V_LEVEL_H264_13 =  5,
	VK_V_LEVEL_H264_2  =  6,
	VK_V_LEVEL_H264_21 =  7,
	VK_V_LEVEL_H264_22 =  8,
	VK_V_LEVEL_H264_3  =  9,
	VK_V_LEVEL_H264_31 = 10,
	VK_V_LEVEL_H264_32 = 11,
	VK_V_LEVEL_H264_4  = 12,
	VK_V_LEVEL_H264_41 = 13,
	VK_V_LEVEL_H264_42 = 14,
	VK_V_LEVEL_H264_5  = 15,
	VK_V_LEVEL_H264_51 = 16,
	VK_V_LEVEL_H264_52 = 17,
	VK_V_LEVEL_H264_6  = 18,
	VK_V_LEVEL_H264_61 = 19,
	VK_V_LEVEL_H264_62 = 20,
	/* hevc level */
	VK_V_LEVEL_HEVC_1  = 1,
	VK_V_LEVEL_HEVC_2  = 2,
	VK_V_LEVEL_HEVC_21 = 3,
	VK_V_LEVEL_HEVC_3  = 4,
	VK_V_LEVEL_HEVC_31 = 5,
	VK_V_LEVEL_HEVC_4  = 6,
	VK_V_LEVEL_HEVC_41 = 7,
	VK_V_LEVEL_HEVC_5  = 8,
	VK_V_LEVEL_HEVC_51 = 9,
	VK_V_LEVEL_HEVC_52 = 10,
	VK_V_LEVEL_HEVC_6  = 11,
	VK_V_LEVEL_HEVC_61 = 12,
	VK_V_LEVEL_HEVC_62 = 13,
	/* vp9 level */
	VK_V_LEVEL_VP9_1  =  1,
	VK_V_LEVEL_VP9_11 =  2,
	VK_V_LEVEL_VP9_2  =  3,
	VK_V_LEVEL_VP9_21 =  4,
	VK_V_LEVEL_VP9_3  =  5,
	VK_V_LEVEL_VP9_31 =  6,
	VK_V_LEVEL_VP9_4  =  7,
	VK_V_LEVEL_VP9_41 =  8,
	VK_V_LEVEL_VP9_5  =  8,
	VK_V_LEVEL_VP9_51 = 10,

	VK_V_LEVEL_MAX = 0xFFFF
};

enum vk_scaler_filter {
	/* a zero value is considered as invalid */
	VK_S_FILTER_UNKNOWN = 0,
	VK_S_FILTER_NEAREST,    /**< Nearest neighbour */
	VK_S_FILTER_LINEAR,     /**< Linear */
	VK_S_FILTER_CUBIC,      /**< Cubic convolution kernel */
	VK_S_FILTER_CATMULL,    /**< Cubic Catmull-Rom spline */
	VK_S_FILTER_MAX = 0xFFFF
};

#define VK_INPUT_PORT  0
#define VK_OUTPUT_PORT 1

typedef union vk_size_ {
	struct {
		/*
		 * the bit organization is designed to match the ssim and
		 * scaler HW register, allow to copy directly of the 32 bits
		 * vk_size into the revelevant ssim/scaler registers.
		 *
		 * for mve engine, it is copied to an mve c structure,
		 * and there is no predefined order for the width and height
		 */
		uint32_t  width:16; /**< is to be the 16 lsb */
		uint32_t height:16; /**< is to be the 16 msb */
	};
	uint32_t size;
} vk_size;

typedef union vk_port_id_ {
	struct {
		uint32_t id:7;     /**< indicate input/output number idx */
		uint32_t direction:1; /**< indicate if input or output   */
	};
	uint32_t map;
} vk_port_id;

typedef struct _vk_port {
	vk_port_id port_id; /** port identifiant */
	int32_t handle; /** handle to the port (typically a buffer pool) */
} vk_port;

#define VK_CFG_FLAG_ENABLE 1


typedef struct _vk_ctu_stats {
	/* number of 8x8 blocks in the CTU that are intra */
	uint8_t  intra_count;
	uint8_t  reserved;
	uint16_t bit_estimate;
	uint16_t luma_mean;
	uint16_t luma_cplx;
	uint16_t rmv_x; /**< rough motion vector estimate in ple unit */
	uint16_t rmv_y;
} vk_ctu_stats;

typedef struct _vk_surface_stats {
	uint8_t encoder_stats_type; /**< encoder stats type  1 = FULL STATS*/
	uint8_t frame_type;  /**<  0=I 1=P 2=B 3=D (non ref frame)  */
	uint8_t used_as_reference;  /**< 0=No, 1=Yes */
	uint8_t qp;  /**< base quantizer used for the frame */
	uint32_t picture_count; /**< display order picture count    */
	uint16_t num_cols; /**< no of columns (each 32 pixels wide) */
	uint16_t num_rows; /**< no of rows (each 32 pixels high) */
	/* any frame can be derived by referencing to 2 other frames */
	uint32_t ref_pic_count[2]; /**< display order pic cnt of references */
} vk_surface_stats;

#define VK_SCL_MAX_PHASES    32
#define VK_SCL_MAX_HOR_COEFS  8
#define VK_SCL_MAX_VER_COEFS  4
#define VK_SCL_MAX_VIDEO_CMPT 2 /**< luma and chroma component */
#define VK_SCL_MAX_DIRECTION  2 /**< horizontal and vertical   */

/**
 * filter coefficents configuration organization:
 * filter N <0-3> |
 *		  | vertical max phases[0:0x7], fract[0x8:0xF]
 *		  | horizontal max phases[0x10:0x17], fract[0x18:0x1F]
 * Horizontal	|
 *		| filter N <0-3> | step [0:0x1F]
 *				 | yield [0:0x1F]
 * Vertical	|
 *		| filter N <0-3> | step [0:0x1F]
 *				 | yield [0:0x1F]
 *
 * number of 32 bits configuration registers: 4 + (2 * 4 * 2)
 *
 * filter coefficient buffer organization:
 * Horizontal filter |- luma    |
 *				|- N <0-3> |-
 *					   |- phase K <0 - 31> -|
 *								|- coef <0 - 7>
 *		     |- chroma	|
 *				|- N <0-3> |-
 *					   |- phase K <0 - 31> -|
 *								|- coef <0 - 7>
 * Vertical filter   |- luma    |
 *				|- N <0-3> |-
 *					   |- phase K <0 - 31> -|
 *								|- coef <0 - 3>
 *		     |- chroma	|
 *				|- N <0-3> |-
 *					   |- phase K <0 - 31> -|
 *								|- coef <0 - 3>
 *
 * number of 16 bits coefficients = (2 * 4 * 32 * 8) +  (2 * 4 * 32 * 4)
 */
typedef struct _vk_scl_custom_filter {
	/**
	 * index of the last phase ( == number of phases -1)
	 * number of fractional bits in the coefficients (range [0, 16])
	 * 16 MSB for horizontal direction
	 * 16 LSB for vertical direction
	 */
	uint32_t config[VK_SCL_MAX_OUTPUTS];
	uint32_t step_yield[VK_SCL_MAX_DIRECTION][VK_SCL_MAX_OUTPUTS][2];
	/**
	 * horizontal coef stored in 16 bits fix point,
	 * fractional part defined by config values
	 */
	uint16_t hor_coefs[VK_SCL_MAX_VIDEO_CMPT][VK_SCL_MAX_OUTPUTS]
			  [VK_SCL_MAX_PHASES][VK_SCL_MAX_HOR_COEFS];
	/**
	 * vertical coef stored in 16 bits fix point,
	 * fractional part defined by config values
	 */
	uint16_t ver_coefs[VK_SCL_MAX_VIDEO_CMPT][VK_SCL_MAX_OUTPUTS]
			  [VK_SCL_MAX_PHASES][VK_SCL_MAX_VER_COEFS];
} vk_scl_custom_filter;

typedef struct _vk_vars_cfg {
	int32_t flags;
	int32_t size; /**< bytes required to store a frame qpmap */
} vk_vars_cfg;

/**
 * bpr is block param record.
 * This struct represents qp_delta for one
 * 32*32 block(per ctu) in the frame.
 * For each quad there are 4 4-bit values.
 * The values are in the offset as mentioned in defines.
 * the values can range from -8 to +7 for each 4-bit set.
 * qpmap will have (width/32) * (height/32) num of records.
 */
typedef struct _vk_qpmap_bpr {
	uint16_t qp_delta;
	/*
	 * One value for each 16*16 (i.e. 4 bit values).
	 * Offsets of each 16x16 values position in qp_delta.
	 */
		#define TOP_LEFT_POS  0
		#define TOP_RIGHT_POS 4
		#define BOT_LEFT_POS  8
		#define BOT_RIGHT_POS 12
	uint8_t force;
		#define BPR_FORCE_NONE  0
		#define BPR_FORCE_QP    1
		#define BPR_FORCE_32x32 2
		#define BPR_FORCE_RB    4
	uint8_t reserved;
} vk_qpmap_bpr;

/**
 * parameterization of the qp delta map generator
 * the algorithm take in input a varainec map, and genertae a qpmap.
 */
typedef struct _vk_adaptqp_cfg {
	int32_t flags;
	int32_t a; /**< Value used in qpdelta =  (a * log2(variance)) + b */
	int32_t b; /**< Value used in qpdelta =  (a * log2(variance)) + b */
	uint8_t split_thresh;
	int8_t bpr_force;
	int8_t last_qpd_mode;
	int8_t sig_cost_threshold_qp;
	int8_t qpd_sum_disable_threshold_qp;
	uint8_t reserved[3]; /* Padding added by compiler */
	uint32_t sig_cost_threshold_bpp; /**< (8.24) fixed point format */
	uint32_t qpd_sum_disable_threshold_bpp; /**< (8.24) fixed point */
	int32_t qpd_sum_threshold;
} vk_adaptqp_cfg;

typedef struct _vk_qpmap_cfg {
	int32_t flags;
	int32_t size; /**< bytes required to store a frame qpmap */
} vk_qpmap_cfg;

typedef struct _vk_stats_cfg {
	int32_t flags;
	int32_t size; /**< bytes required to store a frame stats */
} vk_stats_cfg;

typedef struct _vk_ssim_cfg {
	int32_t flags;
	 /**< log2 of super block size in 4x4pels unit */
	uint8_t log_sb_plus1;
	uint8_t padding[3];
	vk_size size; /**< depreciated field */
} vk_ssim_cfg;

/** vk_buffer_surface format type including pixel depth (encoded on 16 bits) */
typedef enum _vk_format_type {
	VK_FORMAT_UNDEF = 0,
	VK_FORMAT_AFBC  = 1,      /**< hw surface 10 bits  */
	VK_FORMAT_YOL2  = 2,      /**< hw surface 10 bits  */
	VK_FORMAT_NV12  = 3,      /**< sw surface 8 bits  */
	VK_FORMAT_NV21  = 4,      /**< sw surface 8 bits  */
	VK_FORMAT_P010  = 5,      /**< sw surface 10 bits */
} vk_format_type;

/** vk gop type  */
typedef enum _vk_gop_type {
	VK_GOP_UNDEF          = 0, /**< not defined                      */
	VK_GOP_BIDIRECTIONAL  = 1, /**< bframes + reorder                */
	VK_GOP_LOWDELAY       = 2, /**< low delay (no frame reordering)  */
	VK_GOP_PYRAMID        = 3, /**< B frame can be used as reference */
	VK_GOP_MAX            = 4, /**< MAX                              */
	VK_GOP_DEF            = VK_GOP_PYRAMID,
} vk_gop_type;

/** rate control mode.  Note: 0->4 is a direct 1-to-1 mapping to MVE FW */
typedef enum _vk_rc_mode {
	VK_RC_OFF   = 0, /**< off */
	VK_RC_STD   = 1, /**< standard rate control */
	VK_RC_VBR   = 2, /**< variable bit rate     */
	VK_RC_CBR   = 3, /**< constant bit rate     */
	VK_RC_QTY   = 4, /**< quality */
	VK_RC_FRAME = 5, /**< per frame qp */
	VK_RC_MAX   = 6,
	VK_RC_DEF   = VK_RC_QTY,
} vk_rc_mode;

typedef struct _vk_enc_cfg {
	uint32_t standard;  /**< video standard */
	vk_size  size;
	uint16_t profile;
	uint16_t level;
	uint32_t bitrate;   /**< target bitrate used by the rate control */
	uint16_t format;    /**< input frame format */
	uint8_t  gop_type;
	/**
	 * Hypothetical Ref Decoder buffer size relative to bitrate,
	 * size = n x bitrate, used when rate-control is on
	 */
	uint8_t  n_hrd;
	uint16_t gop_size;
	uint32_t fps;       /** frame per second */
	uint8_t  bitdepth;  /**< if 8 , 10 bits - if 0, encoder choose */
	uint8_t  nbframes_plus1; /**< input from FFMPEG, starting from 1 */

	/** qp for intra frame not used if rate control on */
	uint8_t  qpi;
	uint8_t  dqpp;      /**< qp for P frame, if 0, inferred from qpi */
	uint8_t  dqpb;      /**< qp for ref B frame, if 0, inferred from qpp */

	/** qp for non ref frame, if 0, inferred from qpb */
	uint8_t  dqpd;
	uint8_t  rc_mode;   /**< rate control mode, if zero fixed qp */
	uint8_t  min_qp;    /**< min qp used by rate control */
	uint8_t  max_qp;    /**< max qp used by rate control */
	uint8_t  no_repeatheaders; /**< header data for all/first sync frame */
	vk_ssim_cfg ssim_cfg;
	vk_stats_cfg stats_cfg;
	vk_qpmap_cfg qpmap_cfg;
	vk_vars_cfg varmap_cfg;
	vk_adaptqp_cfg adaptqp_cfg;
} vk_enc_cfg;

/** filter type */
typedef enum _vk_scl_filter_type {
	VK_SCL_FLTR_UNKNOWN = 0,  /**< Unknown filter */
	VK_SCL_FLTR_NEAREST,      /**< Nearest neighbour */
	VK_SCL_FLTR_LINEAR,       /**< Linear */
	VK_SCL_FLTR_CUBIC,        /**< Cubic convolution kernel */
	VK_SCL_FLTR_CATMULL,      /**< Cubic Catmull-Rom spline */
	VK_SCL_FLTR_CUSTOM,       /**< Cubic Catmull-Rom spline */
	VK_SCL_FLTR_MAX = 0xFFFF, /**< the filed is encoded on 16 bits */
} vk_scl_filter_type;

/** scaler configuration */
typedef struct _vk_scl_cfg {
	uint32_t filter; /*depreciated parameter */
	uint32_t filter_luma:16;   /**< filter type in Luma component */
	uint32_t filter_chroma:16; /**< filter type in chroma component */
	vk_size  input_size;    /**< input size in pixels   */
	uint16_t in_format;     /**< surface input format   */
	uint16_t out_format;    /**< surface output format  */
	uint8_t  noutputs;      /**< number of video output */
	vk_vars_cfg  vars_cfg;  /**< variance maps */
	vk_qpmap_cfg qpmap_cfg; /**< qp maps */
	vk_size  output_size[VK_SCL_MAX_OUTPUTS]; /**< output sizes in pels */
	uint32_t custom_filter_handle;
} vk_scl_cfg;

/**
 * scaler input assignment.
 * they are explicitly labelled, since they are required to be matching
 * between the card and the host
 */
typedef enum _vk_scl_input {
	VK_SCL_VIDEO_IN     = 0, /**< video surface input */
	VK_SCL_FILTER_COEFS = 1, /**< custom filter configuration input */
	VK_SCL_NUM_INPUTS   = 2,
} vk_scl_input;

/*
 * Flash device types supported.
 * Supported flash types are QSPI/NAND.
 * NOTE: Current implementation supports only QSPI flash type
 */
typedef enum _vk_info_flash_type {
	VK_INFO_FLASH_TYPE_INVALID = 0,
	VK_INFO_FLASH_TYPE_QSPI = 0x10,
	VK_INFO_FLASH_TYPE_NAND = 0x20,
} vk_info_flash_type;

/**
 * Flash image configuration.
 * The flash image configuration is used to pass the data required for flashing
 * between the card and the host
 */
typedef struct _vk_flash_image_config {
	uint32_t image_type;
	uint32_t image_size;
	uint32_t write_offset;
	uint32_t buffer_handle;
} vk_flash_image_cfg;

typedef enum _vkil_parameter_t {
	VK_PARAM_NONE                   = 0,

	/* general monitoring parameters */
	VK_PARAM_POWER_STATE            = 1, /**< power level */
	VK_PARAM_TEMPERATURE            = 2,
	VK_PARAM_AVAILABLE_LOAD         = 3, /**< available load */
	VK_PARAM_AVAILABLE_LOAD_HI      = 4,

	/* INFO sub-component parameters */
	VK_PARAM_FLASH_IMAGE_CONFIG     = 5,
	VK_PARAM_PCIE_EYE_DIAGRAM       = 6,
	VK_PARAM_PCIE_EYE_SIZE          = 7,

	/* component configuration parameters */
	VK_PARAM_VIDEO_CODEC            = 16, /**< 0 means undefined */
	VK_PARAM_VIDEO_PROFILEANDLEVEL  = 17, /**< 0 means undefined */
	VK_PARAM_CODEC_CONFIG           = 18, /**< set/get codec config */

	VK_PARAM_VIDEO_SIZE             = 32, /**< 0 means undefined */
	VK_PARAM_VIDEO_FORMAT           = 33, /**< 0 means undefined */
	VK_PARAM_VIDEO_ENC_CONFIG       = 48,
	VK_PARAM_VIDEO_ENC_GOP_TYPE     = 49,
	VK_PARAM_VIDEO_DEC_FPS          = 50,

	VK_PARAM_PORT                    = 64, /**< set input/output cmpt pad */
	/* get the size of a pool associated to vk_port_id passed in arg[1] */
	VK_PARAM_POOL_SIZE               = 65,
	/* get the max lag for the module */
	VK_PARAM_MAX_LAG                 = 66,
	/* get the minimum required lag ensuring the generation of an output */
	VK_PARAM_MIN_LAG                 = 67,

	/* scaler configuration parameters */
	VK_PARAM_SCALER_FILTER          = 80, /**< 0 means undefined */
	VK_PARAM_SCALER_FORMAT          = 81, /**< 0 means undefined */
	/* set vk_scl_custom_filter buffer */
	VK_PARAM_SCALER_CUST_FILTER_HANDLE = 82,
	VK_PARAM_VIDEO_SCL_CONFIG       = 83,

	/* meta configuration parameters */
	VK_PARAM_VARMAP_SIZE            = 120,
	VK_PARAM_QPMAP_SIZE             = 121,
	VK_PARAM_SSIMMAP_SIZE           = 122,

	VK_PARAM_NEED_MORE_INPUT        = 160,
	VK_PARAM_IS_STREAM_INTERLACE    = 161,

	VK_PARAM_MAX = 0x0FFF,
} vkil_parameter_t;

#endif
