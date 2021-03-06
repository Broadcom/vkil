// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright 2018-2020 Broadcom.
 */

#include <assert.h>
#include <stdio.h>
#include "vkil_api.h"

static vkil_api * ilapi;
static vkil_context *ilctx;

void test_vkil_create_api(void)
{
	ilapi = vkil_create_api();
	assert(ilapi);
	assert(ilapi->init);
	assert(ilapi->deinit);
	assert(ilapi->set_parameter);
	assert(ilapi->get_parameter);
	assert(ilapi->transfer_buffer);
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

void test_vkil_transfer_buffer(void)
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
	test_vkil_transfer_buffer();
	test_vkil_deinit();
	test_vkil_destroy_api();
	printf("Passed!\n");
	return 0;
}
