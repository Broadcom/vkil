#include <stdio.h>
#include "vkil_api.h"

void vk_foo(void)
{
    puts("Hello, I'm a shared library");
}

vkil_api *api;
vkil_context *ctx;
