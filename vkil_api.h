#ifndef vkil_api_h__
#define vkil_api_h__
 
extern void vk_foo(void);

enum vkil_role_type 
{
   VK_DECODER = 1,
   VK_ENCODER = 2,
   VK_SCALER  = 3,
};

enum vkil_command_type 
{
   VK_CMD_NONE = 0,
   VK_CMD_IDLE = 1,
   VK_CMD_RUN = 2,
   VK_CMD_FLUSH  = 3,
   VK_CMD_MAX = 0xFF
};

enum vkil_status_type 
{
   VK_STATE_UNLOADED = 0, // no hw is loaded
   VK_STATE_READY = 1,
   VK_STATE_IDLE = 1,
   VK_STATE_RUN = 2,
   VK_STATE_FLUSH  = 3,
   VK_STATE_ERROR  = 0xFF,
};


// this structure is copied thru PCIE bridge and is currenlty limited to 16 bytes
typedef struct _vkil_context_essential
{
   uint32_t   handle;     // host opaque handle
   int8_t     card_id;    // 255 should be plenty enough
                          // oxff mean the card_id is automatically determined by the driver
   int8_t     queue_id;   // low, high priority 
   int16_t    session_id; // allow the HW, to pool all the context pertaining to a single session
                          // in case of the component die, it is expected the hw kills all the context pertaining to the session
                         // all the component belonging to the session
   enum       vkil_role_type component_role;
}vkil_context_essential;



typedef struct _vkil_context
{
   vkil_context_essential context_essential;
   void       *priv_data; // component role format dependent, 
}vkil_context;


typedef struct _VkIl {
   int32_t (*init)(void ** handle, const vkil_context * context);  
   int32_t (*deinit)(void *handle);
   int32_t  (*set_parameter)(const void *handle, const int32_t field, const void *value);  // static parameters
   int32_t  (*get_parameter)(const void *handle, const int32_t field, void **value); // set only in idling mode
   // SetConfig(*handle, field, *value);  // dynamic parameters
   // GetConfig(*handle, field, &value);  // currently not required
   // SendCommand(*handle, cmd); 
   // cmd are typically used to put the component in different states: e.g. “run”, “flush”, “idle”.
   int32_t (*send_buffer)(const void *component_handle, const void *buffer_handle, const enum vkil_commandtype cmd);

   // Receivebuffer_cb(*component_handle, *buffer_handle);
   int32_t (*receive_buffer)(const void *component_handle, void **buffer_handle);

   // start dma operation 
   int32_t (*upload_buffer)(const void *component_handle, const void *host_buffer, const enum vkil_commandtype cmd);
   int32_t (*download_buffer)(const void *component_handle, const void *host_buffer);

   // poll dma operation status
   int32_t (*uploaded_buffer)(const void *component_handle, const void *host_buffer);
   int32_t (*downloaded_buffer)(const void *component_handle, const void *host_buffer);
   
   // int32_t (*uploaded_buffer_cb)(const void *component_handle, const void *host_buffer)
   // int32_t (*downloaded_buffer_cb)(const void *component_handle, const void *host_buffer)

   // int32_t (*pool_new)(void ** handle, const int32_t element_size,const int32_t element_num);
   // int32_t (*pool_delete)(void *handle);
}VkIl;

 
#endif  // vkil_api_h__