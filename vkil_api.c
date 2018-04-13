#include <stdio.h>
 
 
void vk_foo(void)
{
    puts("Hello, I'm a shared library");
}

void vkil_send_frame(void *data)
{
    printf("[Valkyrie Integration Layer] sending a frame...\n");
}

void vkil_receive_frame(void *data)
{
    printf("[Valkyrie Integration Layer] receiving a frame...\n");
}

void vkil_send_packet(void *data)
{
    printf("[Valkyrie Integration Layer] sending a packet...\n");
}

void vkil_receive_packet(void *data)
{
    printf("[Valkyrie Integration Layer] receiving a packet...\n");
}
