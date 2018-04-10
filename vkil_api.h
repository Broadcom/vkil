#ifndef vkapi_api_h__
#define vkapi_api_h__
 
extern void vk_foo(void);

enum vkil_role_type 
{
   VK_DECODER = 1,
   VK_ENCODER = 2,
   VK_SCALER  = 3,
};

enum vkil_command_type 
{
   VK_NONE = 0,
   VK_IDLE = 1,
   VK_RUN = 2,
   VK_FLUSH  = 3,
};

typedef struct _vkil_context
{
   enum vkil_role_type component_role;
   int32_t    session_id;
   int32_t    card_id;
   void       *priv_data; // component role format dependent, 
}vkil_context;


typedef struct _VkIl {
   int32_t (*init)(void ** handle, const VkContext * context);  
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

 
#endif  // foo_h__