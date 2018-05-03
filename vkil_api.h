#ifndef vkil_api_h__
#define vkil_api_h__

#include <stdint.h>

// this declaration file is to be called by  the vkapi layer (embedded into ffmpeg)

typedef struct _vk_buffer_surface
{
    uint32_t handle;
    uint32_t user_data_tag;
    uint32_t flags;
    uint16_t max_frame_width; /* Size of largest frame allowed to put in */
    uint16_t max_frame_height; /* this buffer. */
    uint16_t xoffset; // Luma x crop
    uint16_t yoffset; // Luma y crop
    uint16_t visible_frame_height; // dimension of visible part of the surface
    uint16_t visible_frame_width;
    uint16_t format; // pixel fromat
    uint16_t reserved0;
    uint32_t stride[ 2 ]; /* Stride between rows, in bytes */
    uint64_t plane_top[ 2 ]; /* Y,Cb,Cr top field */
    uint64_t plane_bot[ 2 ]; /* bottom field (interlace only) */
} vk_buffer_surface;



typedef struct _vk_buffer_packet
{
    uint32_t handle;
    uint32_t user_data_tag;
    uint32_t flags;
    uint32_t size; /* size of packet in byte */
    uint64_t data; /* Pointer to buffer start */
} vk_buffer_packet;



typedef enum _vkil_role_t{
    VK_GENERIC    = 0,
    VK_DECODER    = 1,
    VK_ENCODER    = 2,
    VK_SCALER	  = 3,
    VK_ROLE_MAX   = 0x0F
} vkil_role_t;


typedef enum _vkil_command_t
{
    VK_CMD_NONE     = 0,
    VK_CMD_IDLE     = 1,
    VK_CMD_RUN      = 2,
    VK_CMD_FLUSH    = 3,
    VK_CMD_UPLOAD   = 4,
    VK_CMD_DOWNLOAD = 5,
    VK_CMD_MAX = 0x7F,      // command mask
    VK_CMD_BLOCKING = 0x80, // additional command flag
} vkil_command_t;

typedef enum _vkil_parameter_t {
    VK_PARAM_NONE                   = 0,
    VK_PARAM_AVAILABLE_LOAD         = 2, // % returned 
    VK_PARAM_AVAILABLE_LOAD_HI      = 3,

    VK_PARAM_VIDEO_CODEC            = 16, // 0 undefined, 'h264', 'h265', 'vp9 '
    VK_PARAM_VIDEO_PROFILEANDLVEL   = 17,
    VK_PARAM_VIDEO_SIZE             = 32,

    VK_PARAM_MAX = 0x0FFF,
} vkil_parameter_t;


// this structure is copied thru PCIE bridge and is currenlty limited to 12 bytes
typedef struct _vkil_context_essential
{
    uint32_t    handle;     // host opaque handle: this is defined by the vk card (expected to be the address on the valkyrie card so 32 bits is expected to be enough)
    vkil_role_t component_role;
    int8_t      card_id;    // 255 should be plenty enough
                            // oxff mean the card_id is automatically determined by the driver
    int8_t      queue_id;   // low, high priority
    int16_t     session_id; // allow the HW, to pool all the context pertaining to a single session
                            // in case of the component die, it is expected the hw kills all the context pertaining to the session
                            // all the component belonging to the session
} vkil_context_essential;


typedef struct _vkil_context
{
    vkil_context_essential context_essential;
    void       *priv_data; // component role format dependent,
} vkil_context;

typedef struct _vkil_api {
    int32_t (*init)(void ** handle);
    int32_t (*deinit)(void *handle);
    //  field is build as ((vkil_parameter_t)<<4) | (vkil_role_t))
    int32_t  (*set_parameter)(const void *handle, const int32_t field, const void *value);  // static and dynamic parameters
    int32_t  (*get_parameter)(const void *handle, const int32_t field, void **value); // set only in idling mode
    // SendCommand(*handle, cmd);
    // cmd are typically used to put the component in different states: e.g. �run�, �flush�, �idle�.
    int32_t (*send_buffer)(const void *component_handle, const void *buffer_handle, const vkil_command_t cmd);

    // Receivebuffer_cb(*component_handle, *buffer_handle);
    int32_t (*receive_buffer)(const void *component_handle, void **buffer_handle, const vkil_command_t cmd);

    // start dma operation
    int32_t (*upload_buffer)(const void *component_handle, const void *host_buffer, const vkil_command_t cmd);
    int32_t (*download_buffer)(const void *component_handle, void **host_buffer, const vkil_command_t cmd);

    // poll dma operation status
    int32_t (*uploaded_buffer)(const void *component_handle, const void *host_buffer, const vkil_command_t cmd);
    int32_t (*downloaded_buffer)(const void *component_handle, const void *host_buffer, const vkil_command_t cmd);
   
    // int32_t (*uploaded_buffer_cb)(const void *component_handle, const void *host_buffer)
    // int32_t (*downloaded_buffer_cb)(const void *component_handle, const void *host_buffer)

    // int32_t (*pool_new)(void ** handle, const int32_t element_size,const int32_t element_num);
    // int32_t (*pool_delete)(void *handle);
} vkil_api;

extern void* vkil_create_api(void);

extern int vkil_destroy_api(void* ilapi);

#endif  // vkil_api_h__