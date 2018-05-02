#ifndef vkil_backend_h__
#define vkil_backend_h__

#include <stdint.h>
#include <string.h>

#ifndef VK_EOL
#define VK_EOL "endoflist"
#endif

// this declaration file is to be called by the valkyrie card driver
typedef enum _vkil_status_t
{
    VK_STATE_UNLOADED = 0, // no hw is loaded
    VK_STATE_READY = 1,
    VK_STATE_IDLE = 1,
    VK_STATE_RUN = 2,
    VK_STATE_FLUSH  = 3,
    VK_STATE_ERROR  = 0xFF,
} vkil_status_t;


typedef struct _vkil_drvtable{
   // function carried by vk_comm_from_host
   int32_t (*init)(vkil_context_essential essential);
   int32_t (*deinit)(uint32_t handle);
   int32_t (*set_parameter)(const uint32_t handle, const int32_t field, const int32_t value);  // static parameters
   int32_t (*get_parameter)(const uint32_t handle, const int32_t field, void *value); // set only in idling mode
   int32_t (*upload_buffer)(const uint32_t component_handle, const uint32_t buffer_handle, const vkil_command_t cmd);
   int32_t (*send_buffer)(const uint32_t component_handle, const uint32_t buffer_handle, const vkil_command_t cmd);
   int32_t (*copy_buffer)(const uint32_t component_handle, const uint32_t buffer_handle, const vkil_command_t cmd);

   // function carried by vk_comm_to_host
   int32_t (*init_done)(const uint32_t handle, vkil_status_t * status, void ** hw_handle);
   int32_t (*deinit_done)(const uint32_t handle, vkil_status_t * status, int32_t * err_code);
   int32_t (*parameter_set)(const uint32_t handle, vkil_status_t * status, int32_t * err_code);
   int32_t (*parameter_got)(const uint32_t handle, vkil_status_t * status, uint32_t * value);
   int32_t (*received_buffer)(const uint32_t component_handle, vkil_status_t * status, void **buffer_handle);
} vkil_drvtable;



typedef struct _host2vk_msg
{
    uint8_t function_id;    // this refers to a function listed in
			                // vkil_drv structure or more generally
                            // specifies a message type 
                            // (subsequent message fields can be considered 
                            // function_id dependent)
    uint8_t size;           // message size is 16*(1+size) bytes
    uint16_t queue_id:4 ;   // this provide the input queue index
				            // Low, Hi, more queue index for future proof.
    uint16_t msg_id:12 ;    // unique message identifier in the given queue
    uint32_t context_id;    // context_id is an handle
			                // allowing the HW to retrieve the
			                // the context (session id, component,...)
                            // in which the message apply
    uint32_t args[2];       // argument list taken by the function
} host2vk_msg;


typedef struct _vk2host_msg
{
    uint8_t function_id;    // this refers to a function listed in
			                // vkil_drv structure
    uint8_t size;           // message size is 16*(1+size) bytes
    uint16_t queue_id:4 ;   // this match the host2vk_msg (queue_id,
    uint16_t msg_id:12 ;    // msg_id) to allow to match the response
                            // to a request
    uint32_t context_id;
    uint32_t hw_status;     // return hw status
			                // if ERROR, error_code carries in arg
    uint32_t arg;           // return argument (depend on function)
} vk2host_msg;

static int32_t vkil_get_function_id(const char * functionname)
{
    static char * vkil_fun_list[] = {
                        "undefined",
                        "init",
                        "deinit",
                        "set_parameter",
                        "get_parameter",
                        "send_buffer",
                        "copy_buffer",
                        "init_done",
                        "deinit_done",
                        "parameter_set",
                        "parameter_got",
                        "received_buffer",
                        "copied_buffer",
                        VK_EOL};
    int32_t i=0;
    do{
        i++;
        if (!strcmp(vkil_fun_list[i],functionname))
            return i;
    } while (strcmp(vkil_fun_list[i],VK_EOL));
    return 0;
};

#endif  // vkil_backend_h__