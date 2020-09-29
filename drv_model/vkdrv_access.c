// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright 2018-2020 Broadcom.
 */

#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include "vkdrv_access.h"

typedef struct _vkdrv_ctx {
	void *lib_handle;
	int (*vkdrv_open)(const char *dev_name, int flags);
	int (*vkdrv_close)(int fd);
	ssize_t (*vkdrv_write)(int fd, const void *buf, size_t nbytes);
	ssize_t (*vkdrv_read)(int fd, void *buf, size_t nbytes);
} vkdrv_ctx;

static vkdrv_ctx vkdrv;

int vkdrv_open(const char *dev_name, int flags)
{
	vkdrv.lib_handle = dlopen("libvksim.so", RTLD_LAZY);
	if (!vkdrv.lib_handle)
		goto fail;

	vkdrv.vkdrv_open  = dlsym(vkdrv.lib_handle, "vkdrv_open");
	if (!vkdrv.vkdrv_open)
		goto fail;

	vkdrv.vkdrv_close = dlsym(vkdrv.lib_handle, "vkdrv_close");
	if (!vkdrv.vkdrv_close)
		goto fail;

	vkdrv.vkdrv_read  = dlsym(vkdrv.lib_handle, "vkdrv_read");
	if (!vkdrv.vkdrv_read)
		goto fail;

	vkdrv.vkdrv_write = dlsym(vkdrv.lib_handle, "vkdrv_write");
	if (!vkdrv.vkdrv_write)
		goto fail;

	return vkdrv.vkdrv_open(dev_name, flags);

fail:
	return -EINVAL;
};

int vkdrv_close(int fd)
{
	int ret = vkdrv.vkdrv_close(fd);

	dlclose(vkdrv.lib_handle);
	return ret;
}

ssize_t vkdrv_write(int fd, const void *buf, size_t nbytes)
{
	return vkdrv.vkdrv_write(fd, buf, nbytes);
}

ssize_t vkdrv_read(int fd, void *buf, size_t nbytes)
{
	return vkdrv.vkdrv_read(fd, buf, nbytes);
}
