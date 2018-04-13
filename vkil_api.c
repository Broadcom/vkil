#include <stdio.h>
 
 
void vk_foo(void)
{
    puts("Hello, I'm a shared library");
}

void vk_send_packet(void *data)
{
    printf("[Valkyrie Integration Layer] sending a packet...\n");
}

void vk_receive_frame(void *data)
{
    printf("[Valkyrie Integration Layer] receiving a frame...\n");
}