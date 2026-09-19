# libdav1d

DAV1D_VERSION := 1.5.4
DAV1D_URL := $(VIDEOLAN)/dav1d/$(DAV1D_VERSION)/dav1d-$(DAV1D_VERSION).tar.xz
DAV1D_MIRROR := https://ftp.osuosl.org/pub/videolan/dav1d/$(DAV1D_VERSION)/dav1d-$(DAV1D_VERSION).tar.xz

PKGS += dav1d
ifeq ($(call need_pkg,"dav1d"),)
PKGS_FOUND += dav1d
endif

DAV1D_CONF = -D enable_tests=false -D enable_tools=false

$(TARBALLS)/dav1d-$(DAV1D_VERSION).tar.xz:
	$(call download,$(DAV1D_URL)) || $(call download,$(DAV1D_MIRROR)) || \
		$(call download,$(CONTRIB_VIDEOLAN)/dav1d/dav1d-$(DAV1D_VERSION).tar.xz)
#	$(call download_git,$(DAV1D_GITURL),,$(DAV1D_HASH))

.sum-dav1d: dav1d-$(DAV1D_VERSION).tar.xz

dav1d: dav1d-$(DAV1D_VERSION).tar.xz .sum-dav1d
	$(UNPACK)
	$(MOVE)

.dav1d: dav1d crossfile.meson
	$(MESONCLEAN)
	$(MESON) $(DAV1D_CONF)
	+$(MESONBUILD)
	touch $@
