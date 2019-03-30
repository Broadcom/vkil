lib_LTLIBRARIES = libvkil.la

AUTOMAKE_OPTIONS = subdir-objects

include_HEADERS = \
    vk_buffers.h \
    vk_logger.h \
    vk_parameters.h \
    vkil_api.h \
    vkil_backend.h

libvkil_la_SOURCES = \
    vkutil/vk_utils.c \
    vkutil/host/vk_logger.c \
    vkil_api.c \
    vkil_backend.c \
    vkil_utils.c

if VKDRV_USERMODEL
    VKIL_FLAGS = -DVKDRV_USERMODEL -I$(top_srcdir)/src
    VKIL_ADD   = $(top_builddir)/drv_model/libvkdrv.la
endif

libvkil_la_CPPFLAGS = $(VKIL_FLAGS)
libvkil_la_LIBADD = $(VKIL_ADD)

