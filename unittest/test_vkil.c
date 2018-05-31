/*
 * Copyright 2018 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation (the "GPL").
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 (GPLv2) for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 (GPLv2) along with this source code.
 */

#include <assert.h>
#include <stdio.h>
#include "vkil_api.h"

static vkil_api *ilapi;
static vkil_context *ilctx;

void test_vkil_create_api(void)
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

void test_vkil_init(void)
{
	ilapi->init((void **) &ilctx);
	assert(ilctx);
}

void test_vkil_init2(void)
{
	ilapi->init((void **) &ilctx);
	assert(ilctx);
}

void test_vkil_get_parameter(void)
{

}

void test_vkil_set_parameter(void)
{

}

void test_vkil_send_buffer(void)
{

}

void test_vkil_receive_buffer(void)
{

}

void test_vkil_upload_buffer(void)
{

}

void test_vkil_download_buffer(void)
{

}

void test_vkil_uploadedd_buffer(void)
{

}

void test_vkil_downloaded_buffer(void)
{

}

void test_vkil_deinit(void)
{
	ilapi->deinit((void **) &ilctx);
	assert(!ilctx);
}

void test_vkil_destroy_api(void)
{
	vkil_destroy_api((void **) &ilapi);
	assert(!ilapi);
}

int main(void)
{
	ilapi = NULL;
	ilctx = NULL;
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
