AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/include \
	-include Python.h \
	-include $(top_builddir)/enigma2_config.h

AM_CXXFLAGS = \
	$(LIBSDL_CFLAGS)

bin_PROGRAMS = enigma2 e3-compd

enigma2_SOURCES = \
	bsod.cpp \
	bsod.h \
	enigma.cpp \
	version_info.cpp \
	version_info.h \
	version.h

enigma2_LDADD_WHOLE = \
	$(top_builddir)/lib/actions/libenigma_actions.a \
	$(top_builddir)/lib/base/libenigma_base.a \
	$(top_builddir)/lib/components/libenigma_components.a \
	$(top_builddir)/lib/driver/libenigma_driver.a \
	$(top_builddir)/lib/mmi/libenigma_mmi.a \
	$(top_builddir)/lib/dvb/libenigma_dvb.a \
	$(top_builddir)/lib/dvb_ci/libenigma_dvb_ci.a \
	$(top_builddir)/lib/gdi/libenigma_gdi_ipc.a \
	$(top_builddir)/lib/gdi/libenigma_gdi.a \
	$(top_builddir)/lib/gui/libenigma_gui.a \
	$(top_builddir)/lib/python/libenigma_python.a \
	$(top_builddir)/lib/nav/libenigma_nav.a \
	$(top_builddir)/lib/network/libenigma_network.a \
	$(top_builddir)/lib/service/libenigma_service.a

# Compositor-process binary for the Enigma3 compositor-split (see the
# plan's "Milestone 1" design). Always links the shared-memory opcode
# transport (libenigma_gdi_ipc.a: pthread + POSIX shm). When this build has
# HAVE_EGL and HAVE_DREAMBOX_EGL (the same flags gate gEGLDC/
# DreamboxWindowProvider elsewhere, see lib/gdi/Makefile.inc), e3-compd.cpp
# also drives a real gEGLDC, which means whole-archive-linking the same
# libenigma_gdi.a enigma2 itself links - and, since that pulls in
# unrelated-to-e3-compd code too (epng.cpp/picload.cpp's image decoders,
# in particular), the external libs those need as well. A build without
# those flags only needs libenigma_gdi_ipc.a; the extra libs below are
# harmless (unused symbols, not linked in) in that case since automake
# doesn't fail on an -l flag for a library nothing from this binary
# actually references.
e3_compd_SOURCES = \
	e3-compd.cpp

e3_compd_LDADD_WHOLE = \
	$(top_builddir)/lib/gdi/libenigma_gdi_ipc.a \
	$(top_builddir)/lib/gdi/libenigma_gdi.a

e3_compd_LDADD = \
	@BASE_LIBS@ \
	@LIBGIF_LIBS@ \
	@LIBJPEG_LIBS@ \
	@PTHREAD_LIBS@ \
	@EGL_LIBS@ \
	@GLES2_LIBS@ \
	-lrt

# @BASE_LIBS@ is copied from enigma2_LDADD below on the assumption it
# carries generic deps (freetype et al) that libenigma_gdi.a's non-EGL
# sources (font.cpp in particular) need too, since no dedicated
# FREETYPE_LIBS-style variable is used anywhere in this file either -
# unverified, since this couldn't be build-tested here; if e3-compd fails
# to link with undefined references once you do build it, check what
# @BASE_LIBS@ actually expands to before adding anything else.

e3-compd$(EXEEXT): $(e3_compd_OBJECTS) $(e3_compd_DEPENDENCIES) $(e3_compd_LDADD_WHOLE)
	$(AM_V_CXXLD)$(CXXLD) $(AM_CXXFLAGS) $(CXXFLAGS) $(AM_LDFLAGS) $(LDFLAGS) -o $@ \
		$(e3_compd_OBJECTS) \
		-Wl,--whole-archive $(e3_compd_LDADD_WHOLE) -Wl,--no-whole-archive $(e3_compd_LDADD) $(LIBS)

enigma2_LDADD = \
	@BASE_LIBS@ \
	@LIBGIF_LIBS@ \
	@LIBJPEG_LIBS@ \
	@LIBVUGLES2_LIBS@ \
	@LIBSDL_LIBS@ \
	@LIBXINE_LIBS@ \
	@PTHREAD_LIBS@ \
	@PYTHON_LIBS@ \
	@LIBDDVD_LIBS@ \
	@AVAHI_LIBS@ \
	@LIBHIACCEL_LIBS@ \
	@EGL_LIBS@ \
	@GLES2_LIBS@ \
	-ltuxtxt -ltuxtxt32bpp -lcrypt -lrt -ldl

enigma2_LDFLAGS = -Wl,--export-dynamic

if HAVE_GIT_DIR
GIT_DIR = $(top_srcdir)/.git
GIT = git --git-dir=$(GIT_DIR)

if HAVE_FAKE_GIT_DIR
ENIGMA2_COMMIT_DATE = `grep '^CommitDate:' $(GIT_DIR)/last_commit_info | cut -d' ' -f2`
ENIGMA2_BRANCH = `cat $(GIT_DIR)/branch`
else
ENIGMA2_COMMIT_DATE = `$(GIT) log --no-color -n 1 --pretty=format:%cd --date=short`
ENIGMA2_BRANCH = `$(GIT) branch --no-color 2>/dev/null | sed -e '/^[^*]/d' -e 's/* \(.*\)/\1/'`
ENIGMA2_REV = `$(GIT) describe --abbrev=7 --always --long --tags`
ENIGMA2_COMMIT_HASH = `$(GIT) rev-parse --verify HEAD`
endif
endif

BUILT_SOURCES = version-intermediate.h
.INTERMEDIATE: version-intermediate.h
.PHONY: version-intermediate.h
version-intermediate.h:
	$(AM_V_GEN)touch $@
	-$(AM_V_at)[ -n "$(ENIGMA2_COMMIT_DATE)" ] && echo "#define ENIGMA2_COMMIT_DATE \"$(ENIGMA2_COMMIT_DATE)\"" >> $@
	-$(AM_V_at)[ -n "$(ENIGMA2_BRANCH)" ] && echo "#define ENIGMA2_BRANCH \"$(ENIGMA2_BRANCH)\"" >> $@
	-$(AM_V_at)[ -n "$(ENIGMA2_REV)" ] && echo "#define ENIGMA2_REV \"$(ENIGMA2_REV)\"" >> $@
	-$(AM_V_at)[ -n "$(ENIGMA2_COMMIT_HASH)" ] && echo "#define ENIGMA2_COMMIT_HASH \"$(ENIGMA2_COMMIT_HASH)\"" >> $@
	$(AM_V_at)([ -f version.h ] && diff -q version.h $@ >/dev/null) || $(INSTALL_HEADER) $@ version.h

enigma2$(EXEEXT): $(enigma2_OBJECTS) $(enigma2_DEPENDENCIES) $(enigma2_LDADD_WHOLE)
	$(AM_V_CXXLD)$(CXXLD) $(AM_CXXFLAGS) $(CXXFLAGS) $(AM_LDFLAGS) $(LDFLAGS) -o $@ $(enigma2_LDFLAGS) $(enigma2_OBJECTS) \
		-Wl,--whole-archive $(enigma2_LDADD_WHOLE) -Wl,--no-whole-archive $(enigma2_LDADD) $(LIBS)
