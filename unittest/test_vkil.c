#include "../vkil_api.h"
#include <stdio.h>
#include <assert.h>

vkil_api     * ilapi = NULL;
vkil_context * ilctx = NULL;

void test_vkil_create_api()
{
    ilapi = vkil_create_api();
    assert(ilapi);
    assert(ilapi->init);
    assert(ilapi->deinit);
    assert(ilapi->set_parameter);
    assert(ilapi->get_parameter);
    assert(ilapi->send_buffer);
    assert(ilapi->receive_buffer);
    assert(ilapi->upload_buffer);
    assert(ilapi->download_buffer);
    assert(ilapi->uploaded_buffer);
    assert(ilapi->downloaded_buffer);
}

void test_vkil_init()
{
    ilapi->init((void**) &ilctx);
    assert(ilctx);
}

void test_vkil_init2()
{
    ilapi->init((void**) &ilctx);
    // assert(ilctx);
}

void test_vkil_get_parameter()
{

}

void test_vkil_set_parameter()
{

}

void test_vkil_send_buffer()
{

}

void test_vkil_receive_buffer()
{

}

void test_vkil_upload_buffer()
{

}

void test_vkil_download_buffer()
{

}

void test_vkil_uploadedd_buffer()
{

}

void test_vkil_downloaded_buffer()
{

}

void test_vkil_deinit()
{
    ilapi->deinit((void**) &ilctx);
    assert(!ilctx);
}

void test_vkil_destroy_api()
{
    vkil_destroy_api((void**) &ilapi);
    assert(!ilapi);
}

int main() 
{
    test_vkil_create_api();
    test_vkil_init();
    test_vkil_init2();
    test_vkil_get_parameter();
    test_vkil_set_parameter();
    test_vkil_send_buffer();
    test_vkil_receive_buffer();
    test_vkil_upload_buffer();
    test_vkil_download_buffer();
    test_vkil_uploadedd_buffer();
    test_vkil_downloaded_buffer();
    test_vkil_deinit();
    test_vkil_destroy_api();
    printf("Passed!\n");
    return 0;
}
