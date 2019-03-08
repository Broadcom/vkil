lib_LTLIBRARIES = libvkil.la

AUTOMAKE_OPTIONS = subdir-objects

include_HEADERS = \
    vkil_api.h

libvkil_la_SOURCES = \
    vkdrv/vkdrv_access.c \
    vkdrv/vkdrv_access.h \
    vkil_api.c \
    vkil_backend.c \
    vkil_error.h \
    vkil_internal.h \
    vkil_utils.c \
    vkil_utils.h

libvkil_la_LDFLAGS = -lvksim
