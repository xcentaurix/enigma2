#pragma once

#include <EGL/egl.h>

class INativeWindowProvider {
public:
	virtual ~INativeWindowProvider() = default;

	// Initialises the platform-specific windowing system.
	virtual bool init(int width, int height) = 0;

	// Returns the native display (e.g. wl_display for Wayland).
	virtual EGLNativeDisplayType getNativeDisplay() = 0;

	// True if this provider surfaces via a pixmap (eglCreatePlatformPixmapSurfaceEXT)
	// rather than a window (eglCreateWindowSurface) - see getNativePixmap(). Some
	// platforms (Dreambox's VC5/BEGL stack) only implement the pixmap path.
	virtual bool usesPixmapSurface() const { return false; }

	// Returns the native window (e.g. wl_egl_window for Wayland). Only called when
	// usesPixmapSurface() is false.
	virtual EGLNativeWindowType getNativeWindow() { return (EGLNativeWindowType)0; }

	// Returns a pointer to the platform's native pixmap descriptor (its concrete
	// type is platform-defined, e.g. dmegl_pixmap_handle for Dreambox) for use with
	// eglCreatePlatformPixmapSurfaceEXT. Only called when usesPixmapSurface() is true.
	virtual void* getNativePixmap() { return nullptr; }

	// Called once per frame after rendering instead of eglSwapBuffers() when
	// usesPixmapSurface() is true, since eglSwapBuffers() is only defined for
	// window surfaces. Default is a no-op; a provider whose pixmap already IS
	// the live display memory (no separate present/blit step) can leave this
	// as-is once it has ensured the GPU work is actually complete (glFinish()).
	virtual void presentPixmap() {}

	// Cleans up platform-specific resources.
	virtual void cleanup() = 0;
};
