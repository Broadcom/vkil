/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright(c) 2018 Broadcom
 */

#ifndef VKDRV_ACCESS_H
#define VKDRV_ACCESS_H

/*
 * if we use a user space model of the driver
 * we need to overload kernel call functions
 * to simulate open,close,read,write.
 */
#define open(x, y) _Generic(x, default : vkdrv_open)(x, y)
#define read(x, y, z) _Generic(x, default : vkdrv_read)(x, y, z)
#define write(x, y, z) _Generic(x, default : vkdrv_write)(x, y, z)
#define close(x) _Generic(x, default : vkdrv_close)(x)

extern int vkdrv_open(const char *dev_name, int flags);
extern int vkdrv_close(int fd);
extern ssize_t vkdrv_write(int fd, const void *buf, size_t nbytes);
extern ssize_t vkdrv_read(int fd, void *buf, size_t nbytes);
#endif
