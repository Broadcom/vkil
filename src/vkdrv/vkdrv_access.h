/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright(c) 2018 Broadcom
 */

#ifndef VKDRV_ACCESS_H__
#define VKDRV_ACCESS_H__

/* use user space simulation only */
#define vkdrv_open    vkdrv_sim_open
#define vkdrv_close   vkdrv_sim_close
#define vkdrv_read    vkdrv_sim_read
#define vkdrv_write   vkdrv_sim_write

extern int vkdrv_open(const char *dev_name, int flags);
extern int vkdrv_close(int fd);
extern ssize_t vkdrv_write(int fd, const void *buf, size_t nbytes);
extern ssize_t vkdrv_read(int fd, void *buf, size_t nbytes);
#endif
