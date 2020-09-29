bin_PROGRAMS       = test_vkil test_vkdrv test_dma_lb

test_vkil_SOURCES  = test_vkil.c
test_vkil_CFLAGS   = -I$(top_srcdir)/src
test_vkil_LDADD    = $(top_builddir)/src/libvkil.la

test_vkdrv_SOURCES  = test_vkdrv.c
test_vkdrv_CFLAGS   = -I$(top_srcdir)/src -I$(top_srcdir)/src/vkutil/host
test_vkdrv_LDADD    = $(top_builddir)/src/libvkil.la

test_dma_lb_SOURCES  = test_dma_lb.c
test_dma_lb_CFLAGS   = -I$(top_srcdir)/src -I$(top_srcdir)/src/vkutil/host
test_dma_lb_LDADD    = $(top_builddir)/src/libvkil.la
