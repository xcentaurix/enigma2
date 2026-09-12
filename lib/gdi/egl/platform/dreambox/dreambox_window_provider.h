#pragma once

#include <EGL/dreameglplatform.h>
#include <lib/gdi/egl/inative_window_provider.h>

// EGL native-pixmap provider for Dreambox's VC5/BEGL stack (libvc5dream.so /
// libv3ddriver.so). This platform only implements eglCreatePlatformPixmapSurfaceEXT
// with a struct dmegl_pixmap_handle (see EGL/dreameglplatform.h, shipped by
// Dream Property GmbH in the libvc5dream-dev package) - there is no fbdev_window
// or other native-window surface type here, unlike the Amlogic/Mali-fbdev backend.
//
// Rather than allocating a separate offscreen pixmap and then needing an
// undocumented ioctl to composite it onto the visible display, this provider
// describes the *existing*, already-visible framebuffer memory (the same one
// fbClass/gFBDC already renders the CPU/2D path into) as the pixmap. GLES then
// renders directly into display-visible memory and no separate present/blit
// step is required - at the cost of no double-buffering yet (a later, separate
// improvement once a documented path for that exists).
class DreamboxWindowProvider : public INativeWindowProvider {
private:
	dmegl_pixmap_handle m_pixmap;

public:
	DreamboxWindowProvider();
	virtual ~DreamboxWindowProvider();

	// INativeWindowProvider
	bool init(int width, int height) override;
	EGLNativeDisplayType getNativeDisplay() override;
	bool usesPixmapSurface() const override { return true; }
	void* getNativePixmap() override;
	void presentPixmap() override;
	void cleanup() override;
};
