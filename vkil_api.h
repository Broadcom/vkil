#ifndef vkil_api_h__
#define vkil_api_h__

#include <stdint.h>


typedef struct _vkil_buffer_surface
{
    uint32_t handle;
    uint32_t user_data_tag;
    uint32_t flags;
    uint16_t visible_frame_height; // dimension of visible part of the surface
    uint16_t visible_frame_width;
    uint16_t format; // pixel fromat
    uint16_t reserved0;
    uint32_t plane_top[ 3 ]; /* Y,Cb,Cr top field */
    uint32_t plane_bot[ 3 ]; /* bottom field (interlace only) */
    uint32_t stride[ 3 ]; /* Stride between rows, in bytes */
    uint16_t max_frame_width; /* Size of largest frame allowed to put in */
    uint16_t max_frame_height; /* this buffer. */
    uint16_t xoffset; // Luma x crop
    uint16_t yoffset; // Luma y crop
} vkil_buffer_surface;


typedef struct _vkil_buffer_bitstream
{
    uint32_t handle;
    uint32_t user_data_tag;
    uint32_t flags;
    uint32_t bitstream_alloc_bytes; /* Length of allocated buffer */
    uint32_t bitstream_offset; /* Byte offset from start to first byte */
    uint32_t bitstream_filled_len; /* Number of bytes in the buffer */
    uint32_t bitstream_buf_addr; /* Pointer to buffer start */
    uint32_t reserved; /* padding */
} vkil_buffer_bitstream;

typedef enum _vkil_role_t{
    VK_GENERIC    = 0,
    VK_DECODER    = 1,
    VK_ENCODER    = 2,
    VK_SCALER	  = 3,
    VK_ROLE_MAX   = 0xFF
} vkil_role_t;

typedef enum _vkil_command_t
{
    VK_CMD_NONE     = 0,
    VK_CMD_IDLE     = 1,
    VK_CMD_RUN      = 2,
    VK_CMD_FLUSH    = 3,
    VK_CMD_UPLOAD   = 4,
    VK_CMD_DOWNLOAD = 5,
    VK_CMD_MAX = 0xFF
} vkil_command_t;

typedef enum _vkil_status_t
{
    VK_STATE_UNLOADED = 0, // no hw is loaded
    VK_STATE_READY = 1,
    VK_STATE_IDLE = 1,
    VK_STATE_RUN = 2,
    VK_STATE_FLUSH  = 3,
    VK_STATE_ERROR  = 0xFF,
} vkil_status_t;

// this structure is copied thru PCIE bridge and is currenlty limited to 16 bytes
typedef struct _vkil_context_essential
{
    uint32_t    handle;     // host opaque handle
    int8_t      card_id;    // 255 should be plenty enough
                            // oxff mean the card_id is automatically determined by the driver
    int8_t      queue_id;   // low, high priority
    int16_t     session_id; // allow the HW, to pool all the context pertaining to a single session
                            // in case of the component die, it is expected the hw kills all the context pertaining to the session
                            // all the component belonging to the session
    vkil_role_t component_role;
} vkil_context_essential;

typedef struct _vkil_context
{
    vkil_context_essential context_essential;
    void       *priv_data; // component role format dependent,
} vkil_context;

typedef struct _vkil_api {
    int32_t (*init)(void ** handle);
    int32_t (*deinit)(void *handle);
    int32_t  (*set_parameter)(const void *handle, const int32_t field, const void *value);  // static parameters
    int32_t  (*get_parameter)(const void *handle, const int32_t field, void **value); // set only in idling mode
    // SetConfig(*handle, field, *value);  // dynamic parameters
    // GetConfig(*handle, field, &value);  // currently not required
    // SendCommand(*handle, cmd);
    // cmd are typically used to put the component in different states: e.g. “run”, “flush”, “idle”.
    int32_t (*send_buffer)(const void *component_handle, const void *buffer_handle, const vkil_command_t cmd);

    // Receivebuffer_cb(*component_handle, *buffer_handle);
    int32_t (*receive_buffer)(const void *component_handle, void **buffer_handle);

    // start dma operation
    int32_t (*upload_buffer)(const void *component_handle, const void *host_buffer, const vkil_command_t cmd);
    int32_t (*download_buffer)(const void *component_handle, const void *host_buffer);

    // poll dma operation status
    int32_t (*uploaded_buffer)(const void *component_handle, const void *host_buffer);
    int32_t (*downloaded_buffer)(const void *component_handle, const void *host_buffer);
   
    // int32_t (*uploaded_buffer_cb)(const void *component_handle, const void *host_buffer)
    // int32_t (*downloaded_buffer_cb)(const void *component_handle, const void *host_buffer)

    // int32_t (*pool_new)(void ** handle, const int32_t element_size,const int32_t element_num);
    // int32_t (*pool_delete)(void *handle);
} vkil_api;


#endif  // vkil_api_h__